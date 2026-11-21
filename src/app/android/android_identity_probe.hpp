#pragma once
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <stdexcept>
namespace voyemu {
struct AndroidIdentityProbeOwner {
    std::string epoch;
    std::uint64_t generation{};
    std::int64_t sdk_pid{}, server_pid{};
    std::string route;
    bool operator==(const AndroidIdentityProbeOwner &) const = default;
};
// One retained client supplies bootstrap/recovery backpressure; it never owns SDK lifetime.
class AndroidIdentityProbe {
    ChildProcess client_;
    AndroidIdentityProbeOwner owner_;
    std::filesystem::path output_;
    std::chrono::steady_clock::time_point deadline_{}, retry_{};
    bool pending_ = false;
    std::size_t output_limit_ = 4096;
    struct FileIdentity {
        std::uint64_t device{}, file{};
        bool operator==(const FileIdentity &) const = default;
    };
    FileIdentity slot_identity_{}, output_identity_{};

public:
    struct Arrival {
        std::chrono::system_clock::time_point utc;
        std::chrono::steady_clock::time_point steady;
        bool before_terminal = false;
    };

private:
    std::optional<Arrival> arrival_;
    std::string arrival_output_;

public:
    struct OutputObservation {
        std::size_t bytes{};
        std::string sha256;
        bool sampled = false, complete = false, changed = false;
    };

private:
    OutputObservation output_observation_;
    std::filesystem::file_time_type arrival_write_{};
    bool arrival_changed_ = false;

    void observe_arrival(const std::string &text,
                         const std::function<bool(const std::string &)> &complete,
                         bool before_terminal) {
        const auto sampled = Arrival{std::chrono::system_clock::now(), Clock::now(),
                                     before_terminal};
        const auto write = std::filesystem::last_write_time(output_);
        const bool frame_complete = complete && complete(text);

        output_observation_ = {text.size(), sha256_bytes(text), true, frame_complete, false};

        if (arrival_ && (text != arrival_output_ || write != arrival_write_)) {
            arrival_.reset();
            arrival_output_.clear();
            arrival_changed_ = true;
        }

        output_observation_.changed = arrival_changed_;

        if (!arrival_ && !arrival_changed_ && frame_complete) {
            arrival_ = sampled;
            arrival_output_ = text;
            arrival_write_ = write;
        }
    }

    static FileIdentity identity(const std::filesystem::path &path, bool directory) {
        const auto status = std::filesystem::symlink_status(path);

        if (std::filesystem::is_symlink(status) ||
            (directory ? !std::filesystem::is_directory(status)
                       : !std::filesystem::is_regular_file(status))) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "replaced output slot");
        }

        const auto object = os::owned_file_object(path, directory);

        if (!object) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "ordinary owned output required");
        }

        return {object->volume, object->index};
    }

    void retire_output() {
        if (output_.empty()) {
            return;
        }

        if (client_.alive()) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE_CLEANUP",
                              "cannot retire output of a live retained client");
        }

        const auto slot = output_.parent_path();

        if (identity(slot, true) != slot_identity_) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "output slot identity changed");
        }

        ensure_private_directory(slot);
        const auto status = std::filesystem::symlink_status(output_);

        if (std::filesystem::exists(status)) {
            if (identity(output_, false) != output_identity_) {
                throw VoyemuError("ANDROID_IDENTITY_PROBE", "output identity changed");
            }

            if (!std::filesystem::remove(output_)) {
                throw VoyemuError("ANDROID_IDENTITY_PROBE", "output retirement failed");
            }
        }

        if (!std::filesystem::is_empty(slot) || !std::filesystem::remove(slot)) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "output slot retirement failed");
        }

        output_.clear();
        slot_identity_ = {};
        output_identity_ = {};
    }

    void terminate_verified() {
        client_.terminate(0);

        if (client_.alive()) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE_CLEANUP",
                              "retained client termination unverified");
        }
    }

public:
    using Clock = std::chrono::steady_clock;

    bool pending() const {
        return pending_;
    }

    std::optional<Arrival> arrival() const {
        return arrival_;
    }

    const OutputObservation &output_observation() const {
        return output_observation_;
    }

    bool eligible(Clock::time_point now = Clock::now()) const {
        return !pending_ && now >= retry_;
    }

    std::int64_t pid() const {
        return client_.pid();
    }

    const std::filesystem::path &output() const {
        return output_;
    }

    void cancel() {
        terminate_verified();
        retire_output();
        pending_ = false;
        owner_ = {};
        deadline_ = {};
        retry_ = {};
        arrival_.reset();
        arrival_output_.clear();
        arrival_changed_ = false;
        output_observation_ = {};
    }

    bool launch(const AndroidIdentityProbeOwner &owner, ProcessSpec process,
                Clock::time_point now = Clock::now(),
                std::chrono::milliseconds budget = std::chrono::seconds(120),
                std::size_t output_limit = 4096) {
        if (budget < std::chrono::milliseconds(1) || budget > std::chrono::seconds(120) ||
            output_limit < 1 || output_limit > 512 * 1024) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "finite capture budget required");
        }

        if (pending_) {
            if (owner != owner_) {
                cancel();
                throw VoyemuError("ANDROID_ADB_OWNER", "identity probe generation changed");
            }

            return false;
        }

        if (now < retry_) {
            return false;
        }

        retire_output();
        arrival_.reset();
        arrival_output_.clear();
        arrival_changed_ = false;
        output_observation_ = {};

        if (owner.epoch.empty() || owner.generation == 0 || owner.sdk_pid < 2 ||
            owner.server_pid < 2 || process.output.empty() ||
            std::filesystem::exists(process.output) ||
            std::filesystem::is_symlink(process.output)) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "fresh owned output required");
        }

        const auto slot = process.output.parent_path() / ("probe-" + random_token());

        if (!std::filesystem::create_directory(slot)) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "fresh private slot required");
        }

        ensure_private_directory(slot);
        process.output = slot / "output.log";
        slot_identity_ = identity(slot, true);
        client_ = ChildProcess::launch(process);
        owner_ = owner;
        output_ = std::move(process.output);
        deadline_ = now + budget;
        output_limit_ = output_limit;
        pending_ = true;
        output_identity_ = identity(output_, false);

        return true;
    }

    std::optional<ProcessResult>
    poll(const AndroidIdentityProbeOwner &owner,
         const std::function<std::string(const std::filesystem::path &)> &read,
         Clock::time_point now = Clock::now(),
         const std::function<bool(const std::string &)> &complete = {},
         const std::function<void(const std::string &)> &observe = {}) {
        if (!pending_) {
            return std::nullopt;
        }

        if (owner != owner_) {
            cancel();
            throw VoyemuError("ANDROID_ADB_OWNER", "stale identity probe completion");
        }

        try {
            if (identity(output_.parent_path(), true) != slot_identity_ ||
                identity(output_, false) != output_identity_) {
                throw VoyemuError("ANDROID_IDENTITY_PROBE", "output incarnation changed");
            }
        } catch (...) {
            terminate_verified();
            pending_ = false;
            retry_ = now + std::chrono::seconds(2);
            throw;
        }

        if (client_.alive()) {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(output_, error);

            if (error || !std::filesystem::is_regular_file(status) ||
                std::filesystem::hard_link_count(output_) != 1 ||
                std::filesystem::file_size(output_) > output_limit_) {
                terminate_verified();
                pending_ = false;
                retry_ = now + std::chrono::seconds(2);
                throw VoyemuError("ANDROID_IDENTITY_PROBE",
                                  "pending output missing, linked or oversized");
            }

            if (complete || observe) {
                try {
                    auto text = read(output_);

                    if (text.size() > output_limit_) {
                        throw VoyemuError("ANDROID_IDENTITY_PROBE", "output exceeds bound");
                    }

                    if (identity(output_.parent_path(), true) != slot_identity_ ||
                        identity(output_, false) != output_identity_) {
                        throw VoyemuError("ANDROID_IDENTITY_PROBE", "output incarnation changed");
                    }

                    if (observe) {
                        observe(text);
                    }

                    observe_arrival(text, complete, true);
                } catch (...) {
                    terminate_verified();
                    pending_ = false;
                    retry_ = now + std::chrono::seconds(2);
                    throw;
                }
            }

            if (now < deadline_) {
                return std::nullopt;
            }

            terminate_verified();
            pending_ = false;
            retry_ = now + std::chrono::seconds(2);

            return ProcessResult{1, {}, true};
        }

        pending_ = false;
        retry_ = now + std::chrono::seconds(2);
        auto text = read(output_);

        if (text.size() > output_limit_) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "output exceeds bound");
        }

        if (identity(output_.parent_path(), true) != slot_identity_ ||
            identity(output_, false) != output_identity_) {
            throw VoyemuError("ANDROID_IDENTITY_PROBE", "output incarnation changed");
        }

        if (observe) {
            observe(text);
        }

        observe_arrival(text, complete, false);

        return ProcessResult{client_.exit_code(), std::move(text), false};
    }
};
// Nested lifecycle calls share proof only inside their enclosing operation.
class AndroidIdentityScope {
    unsigned &depth_;
    std::uint64_t &operation_;

public:
    AndroidIdentityScope(unsigned &depth, std::uint64_t &operation)
        : depth_(depth), operation_(operation) {
        if (depth_++ == 0) {
            ++operation_;
        }
    }

    ~AndroidIdentityScope() {
        --depth_;
    }

    AndroidIdentityScope(const AndroidIdentityScope &) = delete;
};
} // namespace voyemu
