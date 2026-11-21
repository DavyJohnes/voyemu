#include "provider_registry.hpp"
#include "error.hpp"
#include "provider_files.hpp"
#include <chrono>
#include <regex>
#include <set>
#include <stdexcept>
namespace voyemu::runtime {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("PROVIDER_INVALID", "invalid provider");
}

void keys(const Json &j, std::set<std::string> required, std::set<std::string> optional = {}) {
    if (!j.is_object()) {
        invalid();
    }

    for (auto i = j.begin(); i != j.end(); ++i) {
        if (!required.count(i.key()) && !optional.count(i.key())) {
            invalid();
        }
    }

    for (const auto &k : required) {
        if (!j.contains(k)) {
            invalid();
        }
    }
}

bool text(const Json &j, std::size_t bound = 256) {
    return j.is_string() && !j.get_ref<const std::string &>().empty() &&
           j.get_ref<const std::string &>().size() <= bound;
}

std::string read_file(const std::filesystem::path &path, std::size_t limit,
                      bool private_file = false) {
    return detail::provider_read(path, limit, private_file);
}

void pin_definition(const Json &j) {
    keys(j, {"path", "sha256"});

    if (!text(j["path"], 4096) || !text(j["sha256"], 64) ||
        !std::regex_match(j["sha256"].get<std::string>(), std::regex("[0-9a-f]{64}"))) {
        invalid();
    }
}

void pin(const Json &j) {
    pin_definition(j);

    if (sha256_bytes(read_file(j["path"].get<std::string>(), 256 * 1024 * 1024)) !=
        j["sha256"].get<std::string>()) {
        invalid();
    }
}

using PinLeases = std::vector<std::unique_ptr<detail::ProviderPinnedFile>>;

void pin(const Json &j, PinLeases &leases) {
    pin_definition(j);
    auto file = std::make_unique<detail::ProviderPinnedFile>(j["path"].get<std::string>(),
                                                             256 * 1024 * 1024, false);

    if (sha256_bytes(file->text()) != j["sha256"].get<std::string>()) {
        invalid();
    }

    leases.push_back(std::move(file));
}
} // namespace

void ensure_provider_jobs_directory(const std::filesystem::path &jobs) {
    try {
        detail::ProviderDirectory held(jobs, true, true);
    } catch (const VoyemuError &) {
        throw VoyemuError("PROVIDER_JOBS", "provider job limit reached");
    }
}

void ProviderRegistry::load(const Json &m) {
    keys(m, {"schema", "id", "version", "worker", "runtime", "capabilities"}, {"files"});

    if (m["schema"] != 1 || !text(m["id"], 64) ||
        !std::regex_match(m["id"].get<std::string>(), std::regex("[a-z][a-z0-9_-]{0,63}")) ||
        !text(m["version"], 128) || !m["capabilities"].is_object() ||
        m["capabilities"].dump().size() > 8192) {
        invalid();
    }

    auto id = m["id"].get<std::string>();
    static const std::set<std::string> reserved = {"ui",    "can",     "system",
                                                   "state", "capture", "logs"};

    if (reserved.count(id) || entries_.count(id)) {
        invalid();
    }

    pin(m["worker"]);
    pin(m["runtime"]);

    if (m.contains("files")) {
        if (!m["files"].is_array() || m["files"].size() > 16) {
            invalid();
        }

        for (const auto &f : m["files"]) {
            pin(f);
        }
    }

    entries_.emplace(id, m);
}

bool ProviderRegistry::contains(const std::string &id) const {
    return entries_.count(id) != 0;
}

Json ProviderRegistry::capabilities() const {
    Json r = Json::array();

    for (const auto &[id, m] : entries_) {
        r.push_back({{"id", id}, {"version", m["version"]}, {"capabilities", m["capabilities"]}});
    }

    return r;
}

Json ProviderRegistry::collect(const std::string &id, const ProviderContext &c, int remaining_ms,
                               const ProviderRunner &runner,
                               const std::function<bool()> &fence) const {
    auto unavailable = [&](const char *code) {
        return Json{{"schema", 1},
                    {"source", id},
                    {"status", "unavailable"},
                    {"boot_id", c.boot_id},
                    {"errors", Json::array({Json{{"code", code}}})}};
    };
    auto found = entries_.find(id);

    if (found == entries_.end()) {
        return unavailable("PROVIDER_NOT_FOUND");
    }

    const auto start = std::chrono::steady_clock::now();
    std::filesystem::path job;
    struct Cleanup {
        std::filesystem::path &p;

        ~Cleanup() {
            if (!p.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        }
    } cleanup{job};
    try {
        if (remaining_ms <= 0 || !runner || !fence || !fence()) {
            return unavailable("PROVIDER_DEADLINE_OR_FENCE");
        }

        // The pinned files stay held until the provider run ends where the host can hold them.
        PinLeases leases;
        const auto verify = [&](const Json &value) {
            pin(value, leases);
        };
        const auto &m = found->second;
        verify(m["worker"]);
        verify(m["runtime"]);

        if (m.contains("files")) {
            for (const auto &f : m["files"]) {
                verify(f);
            }
        }

        if (c.request_id.empty() || c.request_id.size() > 128 || c.id.empty() ||
            c.profile.empty() || c.guest_serial.empty() || c.boot_id.empty()) {
            invalid();
        }

        detail::ProviderDirectory held_jobs(jobs_, true);
        auto candidate = jobs_ / ("provider-" + random_token());
        detail::ProviderDirectory held_job(candidate, true, true, true);
        job = candidate;
        auto request = job / "request.json", result = job / "result.json",
             pending = job / "request.pending";
        Json payload = {{"schema", 1},
                        {"op", "collect"},
                        {"request_id", c.request_id},
                        {"source_id", id},
                        {"session",
                         {{"id", c.id},
                          {"profile", c.profile},
                          {"guest_serial", c.guest_serial},
                          {"boot_id", c.boot_id}}},
                        {"capabilities", m["capabilities"]}};
        auto bytes = payload.dump();

        if (bytes.size() > 16384) {
            invalid();
        }

        detail::provider_request(pending, request, bytes);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();

        if (elapsed >= remaining_ms) {
            return unavailable("PROVIDER_TIMEOUT");
        }

        ProcessSpec spec;
        spec.executable = m["runtime"]["path"].get<std::string>();
        spec.arguments = {m["worker"]["path"].get<std::string>(), "--request", request.string(),
                          "--response", result.string()};
        spec.directory = job;
        spec.output = job / "worker.log";
        auto execution = runner(spec, remaining_ms - static_cast<int>(elapsed));

        if (execution.timed_out ||
            std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(remaining_ms)) {
            return unavailable("PROVIDER_TIMEOUT");
        }

        if (execution.exit_code != 0) {
            return unavailable("PROVIDER_WORKER_FAILED");
        }

        auto r = Json::parse(read_file(result, 1200000, true));
        keys(r,
             {"schema", "request_id", "source", "status", "boot_id", "source_instance", "coverage",
              "observed", "revision"},
             {"data", "errors", "cursor"});

        if (r["schema"] != 1 || r["request_id"] != c.request_id || r["source"] != id ||
            r["boot_id"] != c.boot_id || !text(r["source_instance"], 1024) ||
            !text(r["revision"], 1024) || !text(r["coverage"], 64) || !r["status"].is_string()) {
            invalid();
        }

        auto status = r["status"].get<std::string>();

        if (status != "ready" && status != "absent" && status != "unavailable" &&
            status != "unsupported") {
            invalid();
        }

        if (status == "ready" && (!r.contains("data") || !r["data"].is_object())) {
            invalid();
        }

        keys(r["observed"], {"clock", "start", "end"});

        if (r["observed"]["clock"] != "host-monotonic-ns") {
            invalid();
        }

        for (auto key : {"start", "end"}) {
            if (!text(r["observed"][key], 32) ||
                !std::regex_match(r["observed"][key].get<std::string>(),
                                  std::regex("[0-9]{1,32}"))) {
                invalid();
            }
        }

        if (r.contains("cursor") && !text(r["cursor"], 1024)) {
            invalid();
        }

        if (!fence()) {
            return unavailable("PROVIDER_FENCE_CHANGED");
        }

        if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(remaining_ms)) {
            return unavailable("PROVIDER_TIMEOUT");
        }

        return r;
    } catch (...) {
        return unavailable("PROVIDER_PROTOCOL_OR_PIN_INVALID");
    }
}
} // namespace voyemu::runtime
