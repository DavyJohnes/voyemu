#include "qnx_boot_progress.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <map>
#include <optional>
#include <vector>
namespace voyemu {
namespace {
constexpr std::uint64_t ram = 0x40000000, ram_bytes = 0x200000000ULL;
// Historical readback, never asserted to be the current guest register value.
constexpr std::uint64_t candidate_ttbr1 = 0x50d1f070, candidate_tcr = 0x16b5183519;

std::uint64_t number(std::string_view value, std::size_t offset, unsigned bytes) {
    std::uint64_t result = 0;

    for (unsigned i = 0; i < bytes; ++i) {
        result |= std::uint64_t(static_cast<unsigned char>(value[offset + i])) << (8 * i);
    }

    return result;
}

bool kernel_address(std::uint64_t address) {
    return address >= 0xffffff8000000000ULL;
}

std::optional<std::string> bytes(const QnxPhysicalReader &read, std::uint64_t address,
                                 std::size_t count) {
    if (!count || count > 0x40000 || address < ram || address - ram > ram_bytes - count) {
        return {};
    }

    std::string output(count, 0);
    try {
        if (!read(address, std::as_writable_bytes(std::span(output)))) {
            return {};
        }
    } catch (const std::exception &) {
        return {};
    }

    return output;
}

struct TranslationProof {
    std::uint64_t physical_page;
    std::array<std::uint64_t, 4> entries{}, descriptors{};
    unsigned count = 0;
};
using TranslationCache = std::map<std::pair<std::uint64_t, std::uint64_t>, TranslationProof>;

std::optional<TranslationProof> translation_proof(const QnxPhysicalReader &read, std::uint64_t root,
                                                  std::uint64_t address, unsigned first_level) {
    TranslationProof proof{};
    std::array<std::uint64_t, 4> visited{};
    auto table = root;

    for (unsigned level = first_level; level < 4; ++level) {
        const auto depth = level - first_level;

        if (std::find(visited.begin(), visited.begin() + depth, table) != visited.begin() + depth) {
            return {};
        }

        visited[depth] = table;
        const unsigned shift = 39 - 9 * level;
        const auto index = (address >> shift) & (level ? 511 : 1), entry = table + index * 8;
        auto before = bytes(read, entry, 8);

        if (!before) {
            return {};
        }

        const auto descriptor = number(*before, 0, 8), type = descriptor & 3,
                   next = descriptor & std::uint64_t(0x0000fffffffff000ULL);
        proof.entries[depth] = entry;
        proof.descriptors[depth] = descriptor;
        proof.count = depth + 1;
        auto after = bytes(read, entry, 8);

        if (!after || *after != *before) {
            return {};
        }

        if (level < 3 && type == 3) {
            if (next < ram || next - ram > ram_bytes - 4096) {
                return {};
            }

            table = next;
            continue;
        }

        if ((type != 1 && type != 3) || level == 0 || (level == 3 && type != 3) ||
            !(descriptor & (1ULL << 10))) {
            return {};
        }

        const auto mask = (1ULL << shift) - 1;

        if (next & mask) {
            return {};
        }

        const auto physical = next + (address & mask);

        if (physical < ram || physical - ram >= ram_bytes) {
            return {};
        }

        for (unsigned prior = 0; prior < proof.count; ++prior) {
            auto stable = bytes(read, proof.entries[prior], 8);

            if (!stable || number(*stable, 0, 8) != proof.descriptors[prior]) {
                return {};
            }
        }

        proof.physical_page = physical & ~4095ULL;

        return proof;
    }

    return {};
}

bool stable_proof(const QnxPhysicalReader &read, const TranslationProof &proof) {
    for (unsigned i = 0; i < proof.count; ++i) {
        auto descriptor = bytes(read, proof.entries[i], 8);

        if (!descriptor || number(*descriptor, 0, 8) != proof.descriptors[i]) {
            return false;
        }
    }

    return true;
}

std::optional<std::string> translated_bytes(const QnxPhysicalReader &read, TranslationCache &cache,
                                            std::uint64_t root, std::uint64_t address,
                                            std::size_t count, unsigned first_level) {
    if (!count || count > 0x40000 || address > UINT64_MAX - count) {
        return {};
    }

    std::string output;
    output.reserve(count);

    while (count) {
        const auto key = std::pair{root, address & ~4095ULL};
        auto at = cache.find(key);

        if (at == cache.end()) {
            if (cache.size() >= 512) {
                return {};
            }

            auto proof = translation_proof(read, root, address, first_level);

            if (!proof) {
                return {};
            }

            at = cache.emplace(key, *proof).first;
        } else if (!stable_proof(read, at->second)) {
            cache.erase(at);

            return {};
        }

        const auto chunk = std::min<std::size_t>(count, 4096 - (address & 4095));
        // Data is always read freshly. Only the descriptor proof is retained within this snapshot.
        auto data = bytes(read, at->second.physical_page + (address & 4095), chunk);

        if (!data || !stable_proof(read, at->second)) {
            cache.erase(at);

            return {};
        }

        output += *data;
        address += chunk;
        count -= chunk;
    }

    return output;
}
} // namespace

std::optional<std::uint64_t> qnx_boot_candidate_physical(const QnxPhysicalReader &read,
                                                         std::uint64_t address) {
    if (!kernel_address(address)) {
        return {};
    }

    auto proof = translation_proof(read, candidate_ttbr1, address, 0);

    if (!proof) {
        return {};
    }

    return proof->physical_page + (address & 4095);
}

Json read_qnx_boot_progress(const QnxPhysicalReader &source,
                            std::span<const QnxBootProcessTarget> targets) {
    const auto started = std::chrono::steady_clock::now(),
               deadline = started + std::chrono::milliseconds(100);
    unsigned read_calls = 0;
    bool budget_exhausted = false, guard_reads = false;
    const QnxPhysicalReader read = [&](std::uint64_t address, std::span<std::byte> output) {
        const auto now = std::chrono::steady_clock::now();

        if (read_calls >= 4096 || now >= deadline ||
            (!targets.empty() && !guard_reads &&
             (read_calls >= 3968 || now >= deadline - std::chrono::milliseconds(10)))) {
            budget_exhausted = true;

            return false;
        }

        ++read_calls;

        return source(address, output);
    };
    TranslationCache translations;
    const auto kernel_bytes = [&](const QnxPhysicalReader &reader, std::uint64_t address,
                                  std::size_t count) -> std::optional<std::string> {
        if (!kernel_address(address)) {
            return {};
        }

        return translated_bytes(reader, translations, candidate_ttbr1, address, count, 0);
    };
    const auto user_bytes = [&](const QnxPhysicalReader &reader, std::uint64_t root,
                                std::uint64_t address,
                                std::size_t count) -> std::optional<std::string> {
        if (address >= (1ULL << 39) || count > (1ULL << 39) - address || root % 4096 ||
            root < ram || root - ram > ram_bytes - 4096) {
            return {};
        }

        return translated_bytes(reader, translations, root, address, count, 1);
    };
    const auto kernel_name = [&](std::uint64_t address) -> std::optional<std::string> {
        std::string value;

        while (value.size() < 128) {
            const auto count = std::min<std::size_t>(128 - value.size(), 4096 - (address & 4095));
            auto part = kernel_bytes(read, address, count);

            if (!part) {
                return {};
            }

            const auto zero = part->find(char(0));

            if (zero != std::string::npos) {
                value.append(*part, 0, zero + 1);

                return value;
            }

            value += *part;
            address += count;
        }

        return value;
    };
    Json report = {{"status", "unknown"},
                   {"mapping", "unverified"},
                   {"candidate_ttbr1", candidate_ttbr1},
                   {"candidate_tcr", candidate_tcr},
                   {"candidate_registers_source", "historical_readback_not_current_registers"},
                   {"processes", Json::array()}};
    auto finish = [&]() {
        report["physical_read_calls"] = read_calls;
        report["translation_proof_entries"] = translations.size();
        report["read_elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - started)
                                            .count();
        report["read_budget_exhausted"] = budget_exhausted;

        if (!targets.empty()) {
            report["target_sampling_partial"] = budget_exhausted ||
                                                report.value("thread_report_truncated", false);
        }

        return report;
    };
    auto unknown = [&](const char *reason) {
        report["reason"] = reason;
        report["processes"] = Json::array();

        return finish();
    };

    if (auto ring = bytes(read, 0x50010000, 1056);
        ring && number(*ring, 0, 4) == 0xbeefeeee && number(*ring, 4, 4) >= 1024 &&
        number(*ring, 4, 4) <= 0x3ffe0 && number(*ring, 8, 4) < number(*ring, 4, 4)) {
        report["ring_cursor"] = number(*ring, 8, 4);
        const auto count = std::min<std::size_t>(number(*ring, 8, 4), 1024);
        std::string text = ring->substr(32, count);
        text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
        report["ring_first1024"] = text;
        // Capture the bounded ring in chronological order, retaining the old prefix field.
        const auto capacity = number(*ring, 4, 4), cursor = number(*ring, 8, 4);

        if (auto contents = bytes(read, 0x50010020, capacity); contents) {
            auto header = bytes(read, 0x50010000, 32);

            if (header && header->compare(0, 12, *ring, 0, 12) == 0) {
                std::string console = contents->substr(cursor);
                console.append(contents->substr(0, cursor));
                console.erase(std::remove(console.begin(), console.end(), '\0'), console.end());
                report["ring_console"] = console;
                report["ring_snapshot_status"] = "stable_header";
            } else {
                report["ring_snapshot_status"] = "header_changed";
            }
        } else {
            report["ring_snapshot_status"] = "read_unavailable";
        }
    }

    // OEM procnto PID lookup and process-name diagnostic, pinned firmware code.
    struct Fingerprint {
        std::uint64_t address;
        std::size_t bytes;
        const char *sha;
    };
    constexpr std::array fingerprints{
            Fingerprint{0xffffff80600b7d48ULL, 0x48,
                        "8965d0e534cfa0f46caf331e0542e18412bb35e60d4346e0427229d15c213cc7"},
            Fingerprint{0xffffff80600726f8ULL, 0x24,
                        "15ad0a14704633e7f00eb81183df87aba4299ee41409b4eea02420da4c1ae881"}};

    for (const auto &pin : fingerprints) {
        auto code = kernel_bytes(read, pin.address, pin.bytes);

        if (!code || sha256_bytes(*code) != pin.sha) {
            return unknown("kernel_code_unverified");
        }
    }

    auto vector = kernel_bytes(read, 0xffffff8060126320ULL, 24);

    if (!vector) {
        return unknown("process_vector_unavailable");
    }

    const auto table = number(*vector, 8, 8), capacity = number(*vector, 16, 2);

    if (!capacity || capacity > 4096 || table % 8 || !kernel_address(table)) {
        return unknown("process_vector_geometry");
    }

    const auto count = std::min<std::size_t>(capacity, 64);
    auto slots = kernel_bytes(read, table, count * 8);

    if (!slots) {
        return unknown("process_slots_unavailable");
    }

    struct ProcessContext {
        std::size_t slot;
        std::uint64_t pid, address, name = 0;
    };
    std::vector<ProcessContext> contexts;
    bool procnto = false;
    unsigned skipped = 0;
    report["unverified_entries"] = Json::array();
    report["skip_reasons"] = Json::object();
    auto skip = [&](std::size_t index, const char *reason, std::optional<std::uint64_t> pid = {}) {
        ++skipped;
        auto &counters = report["skip_reasons"];
        counters[reason] = counters.value(reason, 0u) + 1;
        Json entry = {{"slot", index}, {"reason", reason}};

        if (pid) {
            entry["observed_pid_with_index_guard"] = *pid;
        }

        report["unverified_entries"].push_back(std::move(entry));
    };
    std::vector<std::size_t> indices;

    if (targets.empty()) {
        for (std::size_t index = 0; index < count; ++index) {
            indices.push_back(index);
        }
    } else {
        if (targets.size() > 16) {
            return unknown("target_count");
        }

        if (count <= 1) {
            return unknown("procnto_identity_unverified");
        }

        indices.push_back(1);

        for (const auto &target : targets) {
            const auto index = target.pid & 0xfff;

            if (target.pid <= 1 || index >= count || target.name.empty() ||
                target.name.size() >= 128 ||
                std::any_of(target.name.begin(), target.name.end(),
                            [](unsigned char c) {
                                return c < 32 || c >= 127;
                            }) ||
                std::find(indices.begin(), indices.end(), index) != indices.end()) {
                return unknown("target_geometry");
            }

            indices.push_back(index);
        }
    }

    report["process_sampling"] = targets.empty() ? "bounded_vector" : "validated_targets_only";

    if (!targets.empty()) {
        report["read_guard_reserve_ms"] = 10;
        report["read_guard_reserve_calls"] = 128;
    }

    for (const auto index : indices) {
        const auto address = number(*slots, index * 8, 8);

        if (!address || address % 4 || !kernel_address(address)) {
            continue;
        }

        auto before = kernel_bytes(read, address, 512);

        if (!before) {
            skip(index, "process_read");
            continue;
        }

        const auto pid = number(*before, 8, 4), name = number(*before, 0x1a0, 8);

        if ((pid & 0xfff) != index) {
            skip(index, "pid_index");
            continue;
        }

        if (targets.empty()) {
            contexts.push_back({index, pid, address});
        }

        if (!kernel_address(name)) {
            skip(index, "nameptr", pid);
            continue;
        }

        auto text = kernel_name(name);

        if (!text) {
            skip(index, "name_read", pid);
            continue;
        }

        const auto zero = text->find('\0');

        if (zero == std::string::npos) {
            skip(index, "name_terminator", pid);
            continue;
        }

        text->resize(zero);

        if (text->empty()) {
            skip(index, "name_empty", pid);
            continue;
        }

        if (std::any_of(text->begin(), text->end(), [](unsigned char c) {
                return c < 32 || c >= 127;
            })) {
            skip(index, "name_non_ascii", pid);
            continue;
        }

        auto after = kernel_bytes(read, address, 512),
             slot = kernel_bytes(read, table + index * 8, 8);

        if (!after || !slot) {
            skip(index, "stability_read_unavailable", pid);
            continue;
        }

        if (number(*slot, 0, 8) != address || number(*after, 8, 4) != pid ||
            number(*after, 0x1a0, 8) != name) {
            skip(index, "race", pid);
            continue;
        }

        if (!targets.empty()) {
            if (index != 1) {
                const auto hint = std::find_if(targets.begin(), targets.end(),
                                               [&](const auto &target) {
                                                   return (target.pid & 0xfff) == index;
                                               });

                if (hint == targets.end() || hint->pid != pid || hint->name != *text) {
                    skip(index, "target_identity", pid);
                    continue;
                }
            }

            auto name_after = kernel_name(name);

            if (!name_after || name_after->size() <= zero || name_after->substr(0, zero) != *text ||
                (*name_after)[zero] != 0) {
                skip(index, "target_name_race", pid);
                continue;
            }

            contexts.push_back({index, pid, address, name});
        }

        if (pid == 1 && *text == "proc/boot/procnto-smp-instr") {
            procnto = true;
        }

        report["processes"].push_back({{"slot", index}, {"pid", pid}, {"name", *text}});
    }

    auto final_vector = kernel_bytes(read, 0xffffff8060126320ULL, 24);

    if (!final_vector || number(*final_vector, 8, 8) != table ||
        number(*final_vector, 16, 2) != capacity) {
        return unknown("process_vector_changed");
    }

    if (!procnto) {
        return unknown("procnto_identity_unverified");
    }

    report["status"] = "verified";
    report["mapping"] = "candidate_page_tables_validated_by_code_and_pid1";
    report["process_vector_capacity"] = capacity;
    report["process_slots_sampled"] = indices.size();
    report["skipped_unverified_entries"] = skipped;
    constexpr std::array thread_pins{
            Fingerprint{0xffffff80600a42b4ULL, 0x18,
                        "d927f40baa355dbb8088ef9300668fda19154f105d554cedde087ff968a52ac2"},
            Fingerprint{0xffffff80600ca7e0ULL, 0x4c,
                        "2be39955d578e15bb44700ed2277f843ead265c9f99fde5f380482bc7f6a9f52"},
            Fingerprint{0xffffff80600b32b8ULL, 0x38,
                        "70241334a9e23d54649ccff803a462eec83e373fe7e2622d34c41a82df67fb00"},
            Fingerprint{0xffffff80600b4418ULL, 0x14,
                        "4ec7846e5938c4f21cf92e2e194c238a100924a55492dd059983bbd771d0fb99"}};

    for (const auto &pin : thread_pins) {
        auto code = kernel_bytes(read, pin.address, pin.bytes);

        if (!code || sha256_bytes(*code) != pin.sha) {
            report["thread_layout"] = "unverified";

            return finish();
        }
    }

    report["thread_layout"] = "live_code_verified";
    report["thread_processes"] = Json::array();
    unsigned thread_entries = 0;
    // OEM debug_thread_t blocked.connect export and state dispatch; no peer inference from state
    // alone.
    constexpr std::array peer_pins{
            Fingerprint{0xffffff80600b32f0ULL, 0xc,
                        "0e619d88f5c77112930ea44118eb9d0f60ac8e2238a146d3fda0ee149fa6eae3"},
            Fingerprint{0xffffff80600b3410ULL, 0x18,
                        "8878e2ebde319dad8e70ffafa63546b2042013e59e058d956caabe402ed42348"},
            Fingerprint{0xffffff80601133b8ULL, 0xe,
                        "38df1a9ba35f07368f056b1c81586aa89ffd1d506dcc9617d4fc864407a3b27e"},
            Fingerprint{0xffffff80600b35c8ULL, 0x38,
                        "f3f41e26216697c98c46316e4e3838358d5810f270dc34efff744163ef6df021"}};
    bool peer_layout = true;

    for (const auto &pin : peer_pins) {
        auto code = kernel_bytes(read, pin.address, pin.bytes);

        if (!code || sha256_bytes(*code) != pin.sha) {
            peer_layout = false;
            break;
        }
    }

    report["reply_peer_layout"] = peer_layout ? "live_code_verified" : "unverified";
    auto parent_code = kernel_bytes(read, 0xffffff80600b2f84ULL, 0x10);
    const bool parent_layout =
            parent_code &&
            sha256_bytes(*parent_code) ==
                    "fbe22cd7b503f180df71db882c8cc77005819125d1bbabb51218d12fab1ea830";
    report["parent_layout"] = parent_layout ? "live_code_verified" : "unverified";
    constexpr std::array context_pins{
            Fingerprint{0xffffff80600a301cULL, 0x68,
                        "b58cc26ce90b2de2360984679f2eeb684c5bb6f50d542e2e45f5c442cab999c7"},
            Fingerprint{0xffffff80600b3330ULL, 0x14,
                        "10c089d5f77e4ed3e232047c6799ff24b511ac0936004f6127b8cf3f4b0a6531"}};
    bool context_layout = true;

    for (const auto &pin : context_pins) {
        auto code = kernel_bytes(read, pin.address, pin.bytes);

        if (!code || sha256_bytes(*code) != pin.sha) {
            context_layout = false;
            break;
        }
    }

    report["saved_reply_context_layout"] = context_layout ? "live_code_verified" : "unverified";

    if (!targets.empty()) {
        report["saved_blocked_context_layout"] = context_layout ? "live_code_verified"
                                                                : "unverified";
    }

    constexpr std::array request_pins{
            Fingerprint{0xffffff806008bf88ULL, 0x24,
                        "394fc572d176858d431f10744abd81f541f7467480ce713884c8637b6043168a"},
            Fingerprint{0xffffff80601128c8ULL, 8,
                        "dd0afcda564d0acc6e8788100e9c171fb036ab737bf1c8de3a9d69f290e728d3"},
            Fingerprint{0xffffff806008a2a8ULL, 0x2c,
                        "6ceece448b06ce79d0f2ae070b3c5a0def963c29652f5ca0799f130ed3786f9d"},
            Fingerprint{0xffffff806009a18cULL, 0x34,
                        "b549c98fb9ff99568dbc4c528e45e51ccc6833ddec15efbef7272ebf009509d4"},
            Fingerprint{0xffffff8060098f00ULL, 0xbc,
                        "1c67a7147903916706cd384c4d4b1d4bde401cc896e3a0ccde7331850df9f7e3"},
            Fingerprint{0xffffff8060098948ULL, 0x54,
                        "190f5aebaf96e252fc782774dd4662fd7cc4b2236fba096c4604f9b4989d834a"}};
    bool request_layout = true;

    for (const auto &pin : request_pins) {
        auto code = kernel_bytes(read, pin.address, pin.bytes);

        if (!code || sha256_bytes(*code) != pin.sha) {
            request_layout = false;
            break;
        }
    }

    report["reply_request_layout"] = request_layout ? "live_code_verified" : "unverified";
    // QNX7 official SAT parser/sys/states.h numeric values, not inferred from last syscall.
    constexpr std::array state_names{"STATE_DEAD",      "STATE_RUNNING",    "STATE_READY",
                                     "STATE_STOPPED",   "STATE_SEND",       "STATE_RECEIVE",
                                     "STATE_REPLY",     "STATE_STACK",      "STATE_WAITTHREAD",
                                     "STATE_WAITPAGE",  "STATE_SIGSUSPEND", "STATE_SIGWAITINFO",
                                     "STATE_NANOSLEEP", "STATE_MUTEX",      "STATE_CONDVAR",
                                     "STATE_JOIN",      "STATE_INTR",       "STATE_SEM",
                                     "STATE_WAITCTX",   "STATE_NET_SEND",   "STATE_NET_REPLY"};
    // Sample clients before procnto so its larger thread vector cannot consume the shared budget
    // first.
    std::stable_partition(contexts.begin(), contexts.end(), [](const auto &context) {
        return context.pid != 1;
    });

    for (const auto &context : contexts) {
        if (budget_exhausted || thread_entries >= 256) {
            report["thread_report_truncated"] = true;
            break;
        }

        Json observed = {{"observed_pid_with_index_guard", context.pid},
                         {"threads", Json::array()},
                         {"status", "unknown"}};
        auto process = kernel_bytes(read, context.address, 0xb8),
             process_slot = kernel_bytes(read, table + context.slot * 8, 8);

        if (!process || !process_slot) {
            observed["reason"] = budget_exhausted ? "read_budget" : "process_read_unavailable";
            report["thread_processes"].push_back(std::move(observed));
            continue;
        }

        if (number(*process, 8, 4) != context.pid ||
            number(*process_slot, 0, 8) != context.address) {
            observed["reason"] = "process_race";
            report["thread_processes"].push_back(std::move(observed));
            continue;
        }

        const auto parent = number(*process, 0x98, 8), memory_space = number(*process, 0x10, 8);
        std::optional<std::uint64_t> parent_pid;

        if (parent_layout && kernel_address(parent)) {
            auto first = kernel_bytes(read, parent, 12);

            if (first) {
                const auto pid = number(*first, 8, 4), index = pid & 0xfff;

                if (index < count) {
                    auto slot = kernel_bytes(read, table + index * 8, 8),
                         last = kernel_bytes(read, parent, 12);

                    if (slot && last && number(*slot, 0, 8) == parent &&
                        number(*last, 8, 4) == pid) {
                        parent_pid = pid;
                    }
                }
            }
        }

        const auto thread_table = number(*process, 0xa8, 8),
                   thread_capacity = number(*process, 0xb0, 2),
                   free_count = number(*process, 0xb2, 2);

        if (!thread_capacity || thread_capacity > 4096 || free_count >= thread_capacity ||
            thread_table % 8 || !kernel_address(thread_table)) {
            observed["reason"] = "thread_vector_geometry";
            report["thread_processes"].push_back(std::move(observed));
            continue;
        }

        const auto thread_count = std::min<std::size_t>(thread_capacity, 16);
        auto thread_slots = kernel_bytes(read, thread_table, thread_count * 8);

        if (!thread_slots) {
            observed["reason"] = "thread_slots_unavailable";
            report["thread_processes"].push_back(std::move(observed));
            continue;
        }

        observed["thread_vector_capacity"] = thread_capacity;
        observed["slots_sampled"] = thread_count;
        observed["skip_reasons"] = Json::object();

        if (thread_capacity > 16) {
            observed["truncated"] = true;
        }

        auto thread_skip = [&](const char *reason) {
            auto &reasons = observed["skip_reasons"];
            reasons[reason] = reasons.value(reason, 0u) + 1;
        };

        for (std::size_t slot = 0; slot < thread_count; ++slot) {
            if (budget_exhausted || thread_entries >= 256) {
                observed["truncated"] = true;
                report["thread_report_truncated"] = true;
                break;
            }

            const auto raw = number(*thread_slots, slot * 8, 8);

            if (!raw || (raw & 1)) {
                continue;
            }

            const auto address = raw & ~3ULL;

            if (!kernel_address(address)) {
                thread_skip("pointer");
                continue;
            }

            auto before = kernel_bytes(read, address, 0x58);

            if (!before) {
                thread_skip("thread_read");
                continue;
            }

            if (number(*before, 0x48, 8) != context.address) {
                thread_skip("owner");
                continue;
            }

            if (number(*before, 0x50, 4) != slot) {
                thread_skip("tid_index");
                continue;
            }

            const auto state = number(*before, 0x13, 1);
            auto after = kernel_bytes(read, address, 0x58),
                 current_slot = kernel_bytes(read, thread_table + slot * 8, 8);

            if (!after || !current_slot) {
                thread_skip(budget_exhausted ? "read_budget" : "stability_read_unavailable");
                continue;
            }

            if (number(*current_slot, 0, 8) != raw || number(*after, 0x48, 8) != context.address ||
                number(*after, 0x50, 4) != slot || number(*after, 0x13, 1) != state) {
                thread_skip("race");
                continue;
            }

            Json thread = {
                    {"tid", slot + 1}, {"state_raw", state}, {"owner_and_index_verified", true}};

            if (state < state_names.size()) {
                thread["state"] = state_names[state];
            }

            if (!targets.empty() && context.pid != 1 && context_layout &&
                (state == 5 || (state >= 12 && state <= 15))) {
                // The pinned common EL0 trap save stores this context independently of the later
                // blocked state.
                Json saved = {{"status", "unknown"},
                              {"source", "kernel_saved_context_not_live_cpu"}};
                auto before_context = kernel_bytes(read, address + 0x310, 0x110),
                     after_context = kernel_bytes(read, address + 0x310, 0x110),
                     last = kernel_bytes(read, address, 0x58),
                     last_slot = kernel_bytes(read, thread_table + slot * 8, 8);

                if (!before_context || !after_context || !last || !last_slot) {
                    saved["reason"] = budget_exhausted ? "read_budget"
                                                       : "stability_read_unavailable";
                } else if (*before_context != *after_context ||
                           number(*last, 0x48, 8) != context.address ||
                           number(*last, 0x50, 4) != slot || number(*last, 0x13, 1) != state ||
                           number(*last_slot, 0, 8) != raw) {
                    saved["reason"] = "race";
                } else if (number(*before_context, 0x108, 8) & 0xf) {
                    saved["reason"] = "not_saved_el0";
                } else if (!number(*before_context, 0x100, 8) ||
                           number(*before_context, 0x100, 8) % 4 ||
                           kernel_address(number(*before_context, 0x100, 8))) {
                    saved["reason"] = "saved_pc_geometry";
                } else {
                    saved["status"] = "verified";
                    saved["pc"] = number(*before_context, 0x100, 8);
                    saved["lr"] = number(*before_context, 0xf0, 8);
                    saved["sp"] = number(*before_context, 0xf8, 8);
                    saved["pstate"] = number(*before_context, 0x108, 8);

                    for (unsigned reg = 0; reg < 5; ++reg) {
                        saved["x" + std::to_string(reg)] =
                                number(*before_context, static_cast<std::size_t>(reg) * 8, 8);
                    }

                    auto text_hashes = [&]() -> Json {
                        auto fail = [](const char *reason) {
                            return Json{{"status", "unknown"}, {"reason", reason}};
                        };

                        if (!request_layout) {
                            return fail("user_walk_layout_unverified");
                        }

                        const auto pc = number(*before_context, 0x100, 8),
                                   lr = number(*before_context, 0xf0, 8);

                        if (pc < 16 || lr < 16 || pc >= (1ULL << 39) - 16 ||
                            lr >= (1ULL << 39) - 16 || pc % 4 || lr % 4) {
                            return fail("user_window_geometry");
                        }

                        auto owner = kernel_bytes(read, context.address, 0x1a8);

                        if (!owner || number(*owner, 8, 4) != context.pid ||
                            number(*owner, 0x10, 8) != memory_space ||
                            number(*owner, 0x1a0, 8) != context.name) {
                            return fail("owner");
                        }

                        if (!kernel_address(memory_space)) {
                            return fail("memory_pointer");
                        }

                        auto memory = kernel_bytes(read, memory_space, 0x70);

                        if (!memory || number(*memory, 0x68, 8) != context.address) {
                            return fail("memory_owner");
                        }

                        const auto arch_address = number(*memory, 0x60, 8);

                        if (!kernel_address(arch_address)) {
                            return fail("arch_pointer");
                        }

                        auto arch = kernel_bytes(read, arch_address, 0x20);

                        if (!arch || number(*arch, 8, 8) != context.address) {
                            return fail("arch_owner");
                        }

                        const auto ttbr = number(*arch, 0x18, 8),
                                   root = ttbr & std::uint64_t(0x0000fffffffff000ULL);

                        if (ttbr & 0xffe) {
                            return fail("root_geometry");
                        }

                        auto pc_before = user_bytes(read, root, pc - 16, 32),
                             lr_before = user_bytes(read, root, lr - 16, 32);
                        Json caller;
                        std::optional<std::string> frame_before, frame_after, caller_before,
                                caller_after;
                        std::uint64_t caller_address = 0;
                        // Exact pinned libc whole-prologue fingerprints prove a single 16-byte
                        // frame, never a generic unwind.
                        const auto sp = number(*before_context, 0xf8, 8),
                                   fp = number(*before_context, 0xe8, 8);
                        const bool caller_scope =
                                slot == 0 && state == 14 &&
                                std::any_of(targets.begin(), targets.end(),
                                            [&](const auto &target) {
                                                return target.pid == context.pid &&
                                                       target.name == "usr/bin/rpcd";
                                            });

                        if (caller_scope && pc >= 0x501b8 && lr >= 0x31cec &&
                            pc - 0x501b8 == lr - 0x31cec && pc_before && lr_before &&
                            sha256_bytes(*pc_before) ==
                                    "31f4104848e3c69a9a00e830c46aad5c1a2914ec289a6fada26dc551d426c812" &&
                            sha256_bytes(*lr_before) ==
                                    "250ef40b77e5fe6c0fbac2f769d11eff6454317d309ccbd8a204acb665b4aa18" &&
                            sp == fp && sp && sp % 16 == 0 && sp < (1ULL << 39) - 16) {
                            caller = {{"status", "unknown"}, {"reason", "frame_unavailable"}};
                            frame_before = user_bytes(read, root, sp, 16);

                            if (frame_before) {
                                caller_address = number(*frame_before, 8, 8);

                                if (caller_address >= 16 && caller_address < (1ULL << 39) - 16 &&
                                    caller_address % 4 == 0) {
                                    caller_before = user_bytes(read, root, caller_address - 16, 32);
                                    caller_after = user_bytes(read, root, caller_address - 16, 32);
                                    caller["reason"] = "caller_text_unavailable";
                                } else {
                                    caller["reason"] = "caller_geometry";
                                }
                            }

                            frame_after = user_bytes(read, root, sp, 16);

                            if (frame_before && frame_after && *frame_before != *frame_after) {
                                return fail("saved_caller_race");
                            }

                            if (caller_before && caller_after && *caller_before != *caller_after) {
                                return fail("saved_caller_race");
                            }

                            if (frame_before && frame_after && caller_before && caller_after) {
                                caller = {{"status", "verified"},
                                          {"source",
                                           "exact_libc_prologue_single_saved_frame_not_live"},
                                          {"return_address", caller_address},
                                          {"frame_sha256", sha256_bytes(*frame_before)},
                                          {"text",
                                           {{"address", caller_address - 16},
                                            {"bytes", 32},
                                            {"sha256", sha256_bytes(*caller_before)}}}};
                            }

                            // One further frame is allowed only for this pinned msgque_send call
                            // site and complete frame prologue.
                            if (frame_before && frame_after && caller_before && caller_after &&
                                sha256_bytes(*caller_before) ==
                                        "ed36d3bb715063e4c03489f8ab3e3d174b84d563487a78dd8201150aad91fed5") {
                                Json next = {{"status", "unknown"},
                                             {"reason", "msgque_frame_geometry"}};

                                if (caller_address >= 0x12a8 &&
                                    (caller_address - 0x12a8) % 4096 == 0 &&
                                    sp < (1ULL << 39) - 0x40 &&
                                    number(*frame_before, 0, 8) == sp + 16) {
                                    const auto base = caller_address - 0x12a8;
                                    auto prologue = user_bytes(read, root, base + 0x1210, 24);
                                    next["reason"] = "msgque_prologue_unverified";

                                    if (prologue &&
                                        sha256_bytes(*prologue) ==
                                                "39eab9efa745e3b8be9d34516223550a69588d1156c817da289f7b00c35401b7") {
                                        auto second_frame = user_bytes(read, root, sp + 16, 0x30);
                                        next["reason"] = "msgque_frame_unavailable";

                                        if (second_frame) {
                                            const auto next_address = number(*second_frame, 8, 8);
                                            next["reason"] = "next_caller_geometry";

                                            if (next_address >= 16 &&
                                                next_address < (1ULL << 39) - 16 &&
                                                next_address % 4 == 0) {
                                                auto next_code = user_bytes(read, root,
                                                                            next_address - 16, 32),
                                                     next_code_after = user_bytes(
                                                             read, root, next_address - 16, 32),
                                                     second_after = user_bytes(read, root, sp + 16,
                                                                               0x30),
                                                     prologue_after = user_bytes(read, root,
                                                                                 base + 0x1210, 24),
                                                     first_after = user_bytes(read, root, sp, 16),
                                                     call_after = user_bytes(
                                                             read, root, caller_address - 16, 32);

                                                if ((next_code && next_code_after &&
                                                     *next_code != *next_code_after) ||
                                                    (second_after &&
                                                     *second_frame != *second_after) ||
                                                    (prologue_after &&
                                                     *prologue != *prologue_after) ||
                                                    (first_after &&
                                                     *frame_before != *first_after) ||
                                                    (call_after && *caller_before != *call_after)) {
                                                    return fail("saved_second_caller_race");
                                                }

                                                next["reason"] = "next_caller_unavailable";

                                                if (next_code && next_code_after && second_after &&
                                                    prologue_after && first_after && call_after) {
                                                    next = {{"status", "verified"},
                                                            {"source",
                                                             "exact_msgque_prologue_second_saved_frame_not_live"},
                                                            {"return_address", next_address},
                                                            {"frame_sha256",
                                                             sha256_bytes(*second_frame)},
                                                            {"text",
                                                             {{"address", next_address - 16},
                                                              {"bytes", 32},
                                                              {"sha256",
                                                               sha256_bytes(*next_code)}}}};
                                                }
                                            }
                                        }
                                    }
                                }

                                caller["next_saved_caller"] = std::move(next);
                            }
                        }

                        auto pc_after = user_bytes(read, root, pc - 16, 32),
                             lr_after = user_bytes(read, root, lr - 16, 32);
                        auto arch_after = kernel_bytes(read, arch_address, 0x20),
                             memory_after = kernel_bytes(read, memory_space, 0x70),
                             owner_after = kernel_bytes(read, context.address, 0x1a8),
                             context_after = kernel_bytes(read, address + 0x310, 0x110),
                             thread_after = kernel_bytes(read, address, 0x58),
                             thread_slot_after = kernel_bytes(read, thread_table + slot * 8, 8),
                             process_slot_after = kernel_bytes(read, table + context.slot * 8, 8),
                             epoch = kernel_bytes(read, 0xffffff8060126320ULL, 24),
                             name = kernel_name(context.name);

                        if (!pc_before || !lr_before || !pc_after || !lr_after || !arch_after ||
                            !memory_after || !owner_after || !context_after || !thread_after ||
                            !thread_slot_after || !process_slot_after || !epoch || !name) {
                            return fail(budget_exhausted ? "read_budget" : "user_text_unavailable");
                        }

                        const auto hint = std::find_if(targets.begin(), targets.end(),
                                                       [&](const auto &target) {
                                                           return target.pid == context.pid;
                                                       });

                        if (*pc_before != *pc_after || *lr_before != *lr_after ||
                            *context_after != *before_context ||
                            number(*arch_after, 8, 8) != context.address ||
                            number(*arch_after, 0x18, 8) != ttbr ||
                            number(*memory_after, 0x60, 8) != arch_address ||
                            number(*memory_after, 0x68, 8) != context.address ||
                            number(*owner_after, 8, 4) != context.pid ||
                            number(*owner_after, 0x10, 8) != memory_space ||
                            number(*owner_after, 0x1a0, 8) != context.name ||
                            number(*thread_after, 0x48, 8) != context.address ||
                            number(*thread_after, 0x50, 4) != slot ||
                            number(*thread_after, 0x13, 1) != state ||
                            number(*thread_slot_after, 0, 8) != raw ||
                            number(*process_slot_after, 0, 8) != context.address ||
                            number(*epoch, 8, 8) != table || number(*epoch, 16, 2) != capacity ||
                            hint == targets.end() || name->find(char(0)) != hint->name.size() ||
                            name->substr(0, hint->name.size()) != hint->name) {
                            return fail("race");
                        }

                        Json text = {{"status", "verified"},
                                     {"scope",
                                      "stable_owner_user_text_hashes_not_caller_classification"},
                                     {"geometry", "candidate_39bit_4KiB_not_current_TCR"},
                                     {"root_physical", root},
                                     {"pc",
                                      {{"address", pc - 16},
                                       {"bytes", 32},
                                       {"sha256", sha256_bytes(*pc_before)}}},
                                     {"lr",
                                      {{"address", lr - 16},
                                       {"bytes", 32},
                                       {"sha256", sha256_bytes(*lr_before)}}}};

                        if (!caller.is_null()) {
                            text["single_saved_caller"] = std::move(caller);
                        }

                        return text;
                    };
                    saved["user_text"] = text_hashes();
                }

                thread["saved_blocked_context"] = std::move(saved);
            }

            if (state == 6 && peer_layout) {
                Json peer = {{"status", "unknown"}};
                auto first = kernel_bytes(read, address, 0xec);

                if (!first) {
                    peer["reason"] = budget_exhausted ? "read_budget" : "thread_read";
                } else if (number(*first, 0x48, 8) != context.address ||
                           number(*first, 0x50, 4) != slot || number(*first, 0x13, 1) != state) {
                    peer["reason"] = "thread_race";
                } else {
                    const auto connection = number(*first, 0xa0, 8);

                    if (!kernel_address(connection) || connection % 8) {
                        peer["reason"] = "connection_pointer";
                    } else if (auto connection_before = kernel_bytes(read, connection, 0x34)) {
                        if (number(*connection_before, 0xe, 2) & 1) {
                            peer["reason"] = "special_connection";
                        } else {
                            auto connection_after = kernel_bytes(read, connection, 0x34),
                                 last = kernel_bytes(read, address, 0xec),
                                 slot_after = kernel_bytes(read, thread_table + slot * 8, 8);

                            if (!connection_after || !last || !slot_after) {
                                peer["reason"] = budget_exhausted ? "read_budget"
                                                                  : "stability_read_unavailable";
                            } else if (*connection_before != *connection_after ||
                                       number(*last, 0xa0, 8) != connection ||
                                       number(*last, 0xe8, 4) != number(*first, 0xe8, 4) ||
                                       number(*last, 0x48, 8) != context.address ||
                                       number(*last, 0x50, 4) != slot ||
                                       number(*last, 0x13, 1) != state ||
                                       number(*slot_after, 0, 8) != raw) {
                                peer["reason"] = "race";
                            } else {
                                peer = {{"status", "verified"},
                                        {"nd", number(*connection_before, 0x28, 4)},
                                        {"pid", number(*connection_before, 0x2c, 4)},
                                        {"coid",
                                         static_cast<std::int32_t>(number(*first, 0xe8, 4))},
                                        {"chid", static_cast<std::int32_t>(
                                                         number(*connection_before, 0x30, 4))}};
                            }
                        }
                    } else {
                        peer["reason"] = "connection_read";
                    }
                }

                if (peer["status"] == "verified" && context_layout) {
                    // This is the kernel-saved EL0 context of a REPLY-blocked thread, never a live
                    // CPU PC.
                    Json saved = {{"status", "unknown"},
                                  {"source", "kernel_saved_context_not_live_cpu"}};
                    auto context_before = kernel_bytes(read, address + 0x310, 0x110),
                         context_after = kernel_bytes(read, address + 0x310, 0x110);
                    auto last = kernel_bytes(read, address, 0xec),
                         last_slot = kernel_bytes(read, thread_table + slot * 8, 8);
                    const auto connection = number(*first, 0xa0, 8);
                    auto last_connection = kernel_bytes(read, connection, 0x34);

                    if (!context_before || !context_after || !last || !last_slot ||
                        !last_connection) {
                        saved["reason"] = budget_exhausted ? "read_budget"
                                                           : "stability_read_unavailable";
                    } else if (*context_before != *context_after ||
                               number(*last, 0x48, 8) != context.address ||
                               number(*last, 0x50, 4) != slot || number(*last, 0x13, 1) != state ||
                               number(*last, 0xa0, 8) != connection ||
                               number(*last, 0xe8, 4) != number(*first, 0xe8, 4) ||
                               number(*last_slot, 0, 8) != raw ||
                               number(*last_connection, 0xe, 2) & 1 ||
                               number(*last_connection, 0x28, 4) !=
                                       peer["nd"].get<std::uint64_t>() ||
                               number(*last_connection, 0x2c, 4) !=
                                       peer["pid"].get<std::uint64_t>() ||
                               static_cast<std::int32_t>(number(*last_connection, 0x30, 4)) !=
                                       peer["chid"].get<std::int32_t>()) {
                        saved["reason"] = "race";
                    } else if (number(*context_before, 0x108, 8) & 0xf) {
                        saved["reason"] = "not_saved_el0";
                    } else if (!number(*context_before, 0x100, 8) ||
                               number(*context_before, 0x100, 8) % 4 ||
                               kernel_address(number(*context_before, 0x100, 8))) {
                        saved["reason"] = "saved_pc_geometry";
                    } else {
                        saved["status"] = "verified";
                        saved["pc"] = number(*context_before, 0x100, 8);
                        saved["lr"] = number(*context_before, 0xf0, 8);
                        saved["sp"] = number(*context_before, 0xf8, 8);
                        saved["pstate"] = number(*context_before, 0x108, 8);

                        for (unsigned reg = 0; reg < 5; ++reg) {
                            saved["x" + std::to_string(reg)] =
                                    number(*context_before, static_cast<std::size_t>(reg) * 8, 8);
                        }

                        if (request_layout) {
                            auto request = [&]() -> Json {
                                auto fail = [](const char *reason) {
                                    return Json{{"status", "unknown"}, {"reason", reason}};
                                };
                                const auto pc = number(*context_before, 0x100, 8),
                                           lr = number(*context_before, 0xf0, 8),
                                           message = number(*context_before, 8, 8);

                                if (pc < 0x50540) {
                                    return fail("caller_geometry");
                                }

                                const auto base = pc - 0x50540;

                                if (base % 4096 || lr != base + 0x2ca78 ||
                                    number(*context_before, 16, 8) != UINT64_MAX - 23 ||
                                    number(*context_before, 24, 8) ||
                                    number(*context_before, 32, 8) ||
                                    static_cast<std::int32_t>(number(*context_before, 0, 8)) !=
                                            peer["coid"].get<std::int32_t>()) {
                                    return fail("caller_arguments");
                                }

                                auto generic = kernel_bytes(read, 0xffffff8060125890ULL, 8);

                                if (!generic || number(*generic, 0, 8) != 0xffffff806008a2a8ULL) {
                                    return fail("generic_dispatch");
                                }

                                auto dispatch = kernel_bytes(read, 0xffffff806011c010ULL, 8);

                                if (!dispatch || !kernel_address(number(*dispatch, 0, 8))) {
                                    return fail("arch_dispatch");
                                }

                                const auto dispatch_address = number(*dispatch, 0, 8);
                                auto functions = kernel_bytes(read, dispatch_address, 24);

                                if (!functions ||
                                    number(*functions, 0, 8) != 0xffffff8060098f00ULL ||
                                    number(*functions, 16, 8) != 0xffffff8060098948ULL) {
                                    return fail("arch_dispatch");
                                }

                                auto owner = kernel_bytes(read, context.address, 0x78);

                                if (!owner || number(*owner, 8, 4) != context.pid ||
                                    number(*owner, 0x10, 8) != memory_space) {
                                    return fail("owner");
                                }

                                const auto memory_address = number(*owner, 0x10, 8);

                                if (!kernel_address(memory_address)) {
                                    return fail("memory_pointer");
                                }

                                auto memory = kernel_bytes(read, memory_address, 0x70);

                                if (!memory || number(*memory, 0x68, 8) != context.address) {
                                    return fail("memory_owner");
                                }

                                const auto arch_address = number(*memory, 0x60, 8);

                                if (!kernel_address(arch_address)) {
                                    return fail("arch_pointer");
                                }

                                auto arch = kernel_bytes(read, arch_address, 0x20);

                                if (!arch || number(*arch, 8, 8) != context.address) {
                                    return fail("arch_owner");
                                }

                                const auto ttbr = number(*arch, 0x18, 8),
                                           root = ttbr & std::uint64_t(0x0000fffffffff000ULL);

                                if (ttbr & 0xffe) {
                                    return fail("root_geometry");
                                }

                                auto wrapper = user_bytes(read, root, base + 0x50530, 28),
                                     caller = user_bytes(read, root, base + 0x2ca28, 112);

                                if (!wrapper || !caller ||
                                    sha256_bytes(*wrapper) !=
                                            "4f3daed8536cd25a42049f9f5245e3db27ab5968b91a1cc3692f05a402e18227" ||
                                    sha256_bytes(*caller) !=
                                            "3477b244783f759a8beca19fb1649cfc6d0fff7aaabf816a3562cabaefedf92e") {
                                    return fail("user_code_unverified");
                                }

                                auto header = user_bytes(read, root, message, 24),
                                     header_after = user_bytes(read, root, message, 24),
                                     wrapper_after = user_bytes(read, root, base + 0x50530, 28),
                                     caller_after = user_bytes(read, root, base + 0x2ca28, 112);
                                auto arch_after = kernel_bytes(read, arch_address, 0x20),
                                     memory_after = kernel_bytes(read, memory_address, 0x70),
                                     owner_after = kernel_bytes(read, context.address, 0x78),
                                     saved_after = kernel_bytes(read, address + 0x310, 0x110),
                                     thread_after = kernel_bytes(read, address, 0xec),
                                     slot_after = kernel_bytes(read, thread_table + slot * 8, 8),
                                     connection_after = kernel_bytes(read, connection, 0x34),
                                     dispatch_after = kernel_bytes(read, 0xffffff806011c010ULL, 8),
                                     functions_after = kernel_bytes(read, dispatch_address, 24),
                                     generic_after = kernel_bytes(read, 0xffffff8060125890ULL, 8);

                                if (!header || !header_after || !wrapper_after || !caller_after ||
                                    !arch_after || !memory_after || !owner_after || !saved_after ||
                                    !thread_after || !slot_after || !connection_after ||
                                    !dispatch_after || !functions_after || !generic_after) {
                                    return fail(budget_exhausted ? "read_budget"
                                                                 : "stability_read_unavailable");
                                }

                                if (*header != *header_after || *wrapper != *wrapper_after ||
                                    *caller != *caller_after || *saved_after != *context_before ||
                                    *dispatch_after != *dispatch ||
                                    *functions_after != *functions || *generic_after != *generic ||
                                    number(*arch_after, 8, 8) != context.address ||
                                    number(*arch_after, 0x18, 8) != ttbr ||
                                    number(*memory_after, 0x60, 8) != arch_address ||
                                    number(*memory_after, 0x68, 8) != context.address ||
                                    number(*owner_after, 8, 4) != context.pid ||
                                    number(*owner_after, 0x10, 8) != memory_address ||
                                    number(*thread_after, 0x48, 8) != context.address ||
                                    number(*thread_after, 0x50, 4) != slot ||
                                    number(*thread_after, 0x13, 1) != state ||
                                    number(*thread_after, 0xa0, 8) != connection ||
                                    number(*thread_after, 0xe8, 4) != number(*first, 0xe8, 4) ||
                                    number(*slot_after, 0, 8) != raw ||
                                    number(*connection_after, 0xe, 2) & 1 ||
                                    number(*connection_after, 0x28, 4) !=
                                            peer["nd"].get<std::uint64_t>() ||
                                    number(*connection_after, 0x2c, 4) !=
                                            peer["pid"].get<std::uint64_t>() ||
                                    static_cast<std::int32_t>(number(*connection_after, 0x30, 4)) !=
                                            peer["chid"].get<std::int32_t>()) {
                                    return fail("race");
                                }

                                if (number(*header, 0, 2) != 0x41 || number(*header, 2, 2) != 1) {
                                    return fail("message_header");
                                }

                                std::string hex;
                                constexpr char digits[] = "0123456789abcdef";

                                for (unsigned char byte : *header) {
                                    hex += digits[byte >> 4];
                                    hex += digits[byte & 15];
                                }

                                return {{"status", "verified"},
                                        {"request", "mprotect_r"},
                                        {"source", "owner_links_and_two_live_user_code_sha"},
                                        {"geometry", "candidate_39bit_4k_not_current_tcr_readback"},
                                        {"message_hex", hex},
                                        {"type", 0x41},
                                        {"subtype", 1},
                                        {"prot", number(*header, 4, 4)},
                                        {"address", number(*header, 8, 8)},
                                        {"length", number(*header, 16, 8)}};
                            };
                            thread["reply_request"] = request();
                        }
                    }

                    thread["saved_reply_context"] = std::move(saved);
                }

                thread["reply_peer"] = std::move(peer);
            }

            observed["threads"].push_back(std::move(thread));
            ++thread_entries;
        }

        guard_reads = !targets.empty();
        auto final_process = kernel_bytes(read, context.address, 0xb8),
             final_slot = kernel_bytes(read, table + context.slot * 8, 8),
             final_table = kernel_bytes(read, 0xffffff8060126320ULL, 24);

        if (!final_process || !final_slot || !final_table) {
            observed["threads"] = Json::array();
            observed["reason"] = budget_exhausted ? "read_budget" : "process_read_unavailable";
        } else if (number(*final_slot, 0, 8) != context.address ||
                   number(*final_table, 8, 8) != table || number(*final_table, 16, 2) != capacity ||
                   number(*final_process, 8, 4) != context.pid ||
                   number(*final_process, 0x10, 8) != memory_space ||
                   number(*final_process, 0x98, 8) != parent ||
                   number(*final_process, 0xa8, 8) != thread_table ||
                   number(*final_process, 0xb0, 2) != thread_capacity ||
                   number(*final_process, 0xb2, 2) != free_count) {
            observed["threads"] = Json::array();
            observed["reason"] = "process_vector_race";
        } else {
            observed["status"] = "verified";
        }

        if (observed["status"] == "verified" && !targets.empty() && context.pid != 1) {
            const auto hint = std::find_if(targets.begin(), targets.end(), [&](const auto &target) {
                return target.pid == context.pid;
            });
            auto identity = kernel_bytes(read, context.address, 0x1a8),
                 name = kernel_name(context.name),
                 epoch = kernel_bytes(read, 0xffffff8060126320ULL, 24),
                 slot = kernel_bytes(read, table + context.slot * 8, 8);

            if (!identity || !name || !epoch || !slot) {
                observed["threads"] = Json::array();
                observed["status"] = "unknown";
                observed["reason"] = budget_exhausted ? "read_budget"
                                                      : "target_identity_unavailable";
            } else if (hint == targets.end() || number(*identity, 8, 4) != context.pid ||
                       number(*identity, 0x1a0, 8) != context.name ||
                       name->find(char(0)) != hint->name.size() ||
                       name->substr(0, hint->name.size()) != hint->name ||
                       number(*slot, 0, 8) != context.address || number(*epoch, 8, 8) != table ||
                       number(*epoch, 16, 2) != capacity) {
                observed["threads"] = Json::array();
                observed["status"] = "unknown";
                observed["reason"] = "target_identity_race";
            }
        }

        if (observed["status"] == "verified" && parent_pid) {
            auto parent_after = kernel_bytes(read, parent, 12),
                 parent_slot = kernel_bytes(read, table + (*parent_pid & 0xfff) * 8, 8);

            if (parent_after && parent_slot && number(*parent_after, 8, 4) == *parent_pid &&
                number(*parent_slot, 0, 8) == parent) {
                observed["parent_pid_with_index_guard"] = *parent_pid;
            }
        }

        report["thread_processes"].push_back(std::move(observed));
        guard_reads = false;
    }

    report["thread_entries_observed"] = thread_entries;

    return finish();
}
} // namespace voyemu
