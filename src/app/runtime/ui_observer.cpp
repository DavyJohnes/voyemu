#include "ui_observer.hpp"
#include "check.hpp"
#include "error.hpp"
#include "profile_catalog.hpp"
#include "ui_observer_recovery.hpp"
#include "ui_observer_shutdown.hpp"
#include "ui_observer_wire.hpp"
#include <fstream>
namespace voyemu {
namespace {
std::string clean(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }

    return value;
}
} // namespace

void clear_ui_observer_metadata(const std::filesystem::path &directory,
                                const std::string &session) {
    for (const auto *name : {"ui-observer.json", "ui-observer-pending.json"}) {
        const auto file = directory / name;

        if (!std::filesystem::exists(file) && !std::filesystem::is_symlink(file)) {
            continue;
        }

        const auto metadata = read_private_json(file, 4096);

        if (metadata.value("schema", 0) != 1 ||
            metadata.value("session_id", std::string{}) != session) {
            throw VoyemuError("TREE_OWNER", "refusing to remove foreign observer metadata");
        }

        if (!std::filesystem::remove(file)) {
            throw VoyemuError("TREE_OWNER", "observer metadata removal failed");
        }
    }
}

UiObserver::UiObserver(std::filesystem::path directory, std::string session, Json android, Adb adb,
                       OpenShell shell, OpenObserver observer)
    : directory_(std::move(directory)), session_(std::move(session)), auth_(random_token()),
      observer_id_(random_token()), android_(std::move(android)), adb_(std::move(adb)),
      open_shell_(std::move(shell)), open_observer_(std::move(observer)) {
    if (!hex_text(session_, 32)) {
        throw VoyemuError("TEST_SESSION", "invalid observer session");
    }

    guest_ = "/data/local/tmp/voyemu-ui-" + observer_id_;
}

UiObserver::~UiObserver() {
    try {
        stop();
    } catch (...) { /* Ignored: destructors must not throw. */
    }
}

Json UiObserver::exchange_owned(const Json &connection, Json message, int timeout_ms) {
    return exchange_ui_observer(connection, std::move(message), auth_, timeout_ms, [&] {
        return open_observer_(observer_id_, timeout_ms);
    });
}

Json UiObserver::exchange(const Json &connection, Json message, int timeout_ms) {
    if (timeout_ms < 1 || timeout_ms > 35000) {
        throw VoyemuError("TEST_ARGUMENT", "bounded observer transport required");
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::unique_lock lock(mutex_, std::defer_lock);

    if (!lock.try_lock_until(deadline)) {
        throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "observer lifecycle deadline elapsed");
    }

    if (connection_.empty() || connection != connection_) {
        throw VoyemuError("TREE_STALE", "observer lifetime changed");
    }

    const auto observer_id = observer_id_, auth = auth_;
    lock.unlock();
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();

    if (remaining <= 0) {
        throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "observer deadline elapsed");
    }

    auto response = exchange_ui_observer(
            connection, std::move(message), auth, static_cast<int>(remaining), [&] {
                return open_observer_(observer_id, static_cast<int>(remaining));
            });

    if (!lock.try_lock_until(deadline)) {
        throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "observer lifecycle deadline elapsed");
    }

    if (connection != connection_) {
        throw VoyemuError("TREE_STALE", "observer lifetime changed during exchange");
    }

    return response;
}

void UiObserver::start_helper(const std::vector<std::string> &args) {
    cancel_helper();
    auto stream = open_shell_(args);
    {
        std::lock_guard lock(helper_mutex_);
        helper_terminal_.reset();
    }
    helper_ = std::jthread([this, stream = std::move(stream)](std::stop_token stop) mutable {
        try {
            std::ofstream log;
            std::size_t written = 0;
            constexpr std::size_t log_limit = 128 * 1024;

            while (!stop.stop_requested()) {
                auto state = stream.poll();

                if (!state.output.empty() && written < log_limit) {
                    if (!log.is_open()) {
                        log.open(directory_ / "ui-observer.log",
                                 std::ios::binary | std::ios::trunc);
                    }

                    const auto count = std::min(state.output.size(), log_limit - written);

                    if (log) {
                        log.write(state.output.data(), static_cast<std::streamsize>(count));
                        log.flush();
                    }

                    written += count;
                }

                if (state.terminal) {
                    std::lock_guard lock(helper_mutex_);
                    helper_terminal_ = std::move(state.terminal);

                    return;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (...) { /* Ignored: a failed local capture cannot prove guest shutdown. */
        }

        std::lock_guard lock(helper_mutex_);
        helper_terminal_ = ProcessResult{1, {}, false};
    });
}

bool UiObserver::helper_alive() {
    std::lock_guard lock(helper_mutex_);

    return helper_.joinable() && !helper_terminal_;
}

int UiObserver::helper_exit_code() {
    std::lock_guard lock(helper_mutex_);

    return helper_terminal_ ? helper_terminal_->exit_code : 1;
}

void UiObserver::cancel_helper() {
    if (!helper_.joinable()) {
        return;
    }

    helper_.request_stop();
    helper_.join();
}

Json UiObserver::connection(int timeout_ms, bool recover) {
    if (timeout_ms < 1 || timeout_ms > 30000) {
        throw VoyemuError("TEST_ARGUMENT", "bounded observer preparation required");
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::unique_lock owned(mutex_, std::defer_lock);

    if (!owned.try_lock_until(deadline)) {
        throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "observer lifecycle deadline elapsed");
    }

    if (retired_) {
        throw VoyemuError("TREE_STALE", "observer lease was retired");
    }

    const auto remaining = [&]() {
        const auto value = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();

        if (value <= 0) {
            throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "observer preparation deadline elapsed");
        }

        return static_cast<int>(value);
    };
    const auto call = [&](const std::vector<std::string> &args, int maximum = 5000) {
        auto result = adb_(args, std::min(maximum, remaining()));

        if (result.timed_out) {
            throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "owned ADB deadline elapsed");
        }

        if (result.exit_code) {
            throw VoyemuError("TREE_PREPARE", "owned ADB operation failed");
        }

        return clean(result.output);
    };
    const auto handshake = [&](const Json &connection) {
        const auto hello = exchange_owned(connection, {{"op", "hello"}},
                                          std::min(2000, remaining()));

        if (!hello.value("ok", false)) {
            throw VoyemuError("TREE_STALE",
                              "observer handshake unavailable; " + observer_failure_summary(hello));
        }

        if (!hello.contains("boot_id") || !hello.at("boot_id").is_string() ||
            !UiObserverRecovery::valid_boot(hello.at("boot_id").get_ref<const std::string &>()) ||
            !hello.contains("observer_pid") || !hello.at("observer_pid").is_number_unsigned() ||
            hello.at("observer_pid").get<std::uint64_t>() < 2 ||
            hello.at("observer_pid").get<std::uint64_t>() > UINT32_MAX ||
            !hello.contains("api_level") || !hello.at("api_level").is_number_unsigned()) {
            throw VoyemuError("TREE_PROTOCOL", "observer lifetime proof required");
        }

        if (connection.contains("boot_id") &&
            (hello.at("boot_id") != connection.at("boot_id") ||
             hello.at("observer_pid") != connection.at("observer_pid"))) {
            throw VoyemuError("TREE_STALE", "observer lifetime changed");
        }

        auto verified = connection;
        verified["boot_id"] = hello.at("boot_id");
        verified["observer_pid"] = hello.at("observer_pid");
        verified["api_level"] = hello.at("api_level");

        return verified;
    };
    bool retired = false;
    Json recovery = Json::object();
    const auto transport_failure = [](const std::exception &error) {
        return std::string_view(error.what()).starts_with("TREE_TRANSPORT_UNKNOWN:") ||
               std::string_view(error.what()).starts_with("TREE_TIMEOUT_UNKNOWN:");
    };

    if (!connection_.empty()) {
        try {
            connection_ = handshake(connection_);

            return connection_;
        } catch (const std::exception &error) {
            if (!recover || !transport_failure(error)) {
                throw;
            }
        }

        // A fresh stream can recover transport without replacing an authenticated observer.
        try {
            connection_ = handshake(connection_);

            return connection_;
        } catch (const std::exception &error) {
            if (!transport_failure(error)) {
                throw;
            }
        }

        const auto previous = connection_;
        const auto previous_boot = previous.value("boot_id", std::string{});
        const auto previous_pid = previous.value("observer_pid", 0u);
        unsigned probes = 0;
        UiObserverRecovery::retire(
                previous_boot, previous_pid,
                [&] {
                    UiObserverRecovery::Observation observed{
                            call({"shell", "cat", "/proc/sys/kernel/random/boot_id"}),
                            call({"shell", "ps", "-A", "-o", "PID"})};
                    recovery = {
                            {"schema", 1},
                            {"stage", "retirement_probe"},
                            {"probe", ++probes},
                            {"previous_pid", previous_pid},
                            {"previous_boot_valid", UiObserverRecovery::valid_boot(previous_boot)},
                            {"observed_boot_valid", UiObserverRecovery::valid_boot(observed.boot)},
                            {"boot_changed", previous_boot != observed.boot},
                            {"inventory_bytes", observed.inventory.size()},
                            {"host_helper_alive", helper_alive()}};
                    try {
                        recovery["previous_present"] = !UiObserverRecovery::previous_gone(
                                previous_boot, observed.boot, previous_pid, observed.inventory);
                        recovery["inventory_valid"] = true;
                    } catch (const std::exception &) {
                        recovery["inventory_valid"] = false;
                    }

                    write_private_json(directory_ / "ui-observer-recovery.json", recovery);

                    return observed;
                },
                [&] {
                    remaining();
                    cancel_helper();

                    return !helper_alive();
                });
        retired = true;
        std::filesystem::remove(directory_ / "ui-observer-pending.json");
        try {
            call({"shell", "rm", "-rf", guest_}, 1000);
        } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
        }

        connection_.clear();
        auth_ = random_token();
        observer_id_ = random_token();
        guest_ = "/data/local/tmp/voyemu-ui-" + observer_id_;
    }

    const auto retained = [](const std::filesystem::path &file) {
        return std::filesystem::exists(file) || std::filesystem::is_symlink(file);
    };
    const bool retained_metadata = !retired && (retained(directory_ / "ui-observer.json") ||
                                                retained(directory_ / "ui-observer-pending.json"));
    recovery["schema"] = 1;
    recovery["stage"] = "launch_fence";
    recovery["retired"] = retired;
    recovery["host_helper_alive"] = helper_alive();
    recovery["retained_metadata"] = retained_metadata;
    write_private_json(directory_ / "ui-observer-recovery.json", recovery);
    UiObserverRecovery::launch(helper_alive(), retained_metadata);
    const auto definition = android_.value("ui_observer", Json::object());

    if (!definition.contains("dex") || !definition.contains("sha256")) {
        throw VoyemuError("TEST_UNSUPPORTED", "profile UI observer is not prepared");
    }

    const auto dex = std::filesystem::path(definition.at("dex").get<std::string>());
    const auto pin = definition.at("sha256").get<std::string>();

    if (!dex.is_absolute() || std::filesystem::is_symlink(dex) ||
        !std::filesystem::is_regular_file(dex) || pin.size() != 64 || sha256_file(dex) != pin) {
        throw VoyemuError("LAUNCH_PIN", "owned UI observer DEX mismatch");
    }

    const auto uid = call({"shell", "id", "-u"});

    if (uid != "0" && uid != "2000") {
        throw VoyemuError("TREE_OWNER", "shell or root observer required");
    }

    call({"shell", "mkdir", "-p", guest_});
    call({"shell", "chmod", "700", guest_});
    call({"push", dex.string(), guest_ + "/classes.dex"}, 10000);

    if (!call({"shell", "sha256sum", guest_ + "/classes.dex"}).starts_with(pin + " ")) {
        throw VoyemuError("LAUNCH_PIN", "guest observer DEX mismatch");
    }

    const std::string command =
            "CLASSPATH=" + android_shell_quote(guest_ + "/classes.dex") +
            " exec app_process /system/bin com.voyemu.api.ObserverMain " + observer_id_ + " " +
            auth_ +
            // The API30 guest observer takes the platform of the profile that binds it.
            (definition.contains("profile")
                     ? " " + android_shell_quote(
                                     embedded_profile(definition.at("profile").get<std::string>())
                                             .at("definition")
                                             .at("platform")
                                             .get<std::string>())
                     : "");
    // Retain the attempted guest lifetime even if it never reaches its first authenticated reply.
    write_private_json(
            directory_ / "ui-observer-pending.json",
            {{"schema", 1}, {"session_id", session_}, {"stage", "guest_launch_attempt"}});
    start_helper({"shell", command});
    connection_ = {{"observer_id", observer_id_}};

    while (std::chrono::steady_clock::now() < deadline) {
        try {
            connection_ = handshake(connection_);

            return connection_;
        } catch (const std::exception &error) {
            if (!transport_failure(error) || !helper_alive()) {
                throw;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(50, remaining())));
        }
    }

    throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "observer startup deadline elapsed");
}

void UiObserver::retire_for_boot(const std::string &previous, const std::string &boot) {
    // The Session holds its state lock here. An active setup may be waiting
    // for a generation fence, so retry the boot transition without waiting.
    std::unique_lock owned(mutex_, std::try_to_lock);

    if (!owned.owns_lock()) {
        throw VoyemuError("ANDROID_IDENTITY_NOT_READY",
                          "observer retirement waits for owned setup");
    }

    if (!UiObserverShutdown::replaced_boot(previous, boot) ||
        (connection_.contains("boot_id") && connection_.at("boot_id") != previous)) {
        throw VoyemuError("TREE_STALE", "observer retirement requires a different verified boot");
    }

    // The old guest process cannot survive a verified Linux boot change. Retire
    // only our retained stream; never address a reused guest PID or path.
    cancel_helper();
    connection_.clear();
    retired_ = true;
    std::filesystem::remove(directory_ / "ui-observer-pending.json");
}

Json UiObserver::shutdown_status() {
    std::lock_guard owned(mutex_);

    return shutdown_status_;
}

bool UiObserver::stop() {
    std::lock_guard owned(mutex_);

    if (retired_) {
        return true;
    }

    bool closed = false;
    shutdown_status_ = {{"stage", "close"},
                        {"closed_ack", false},
                        {"helper_terminal", false},
                        {"helper_exit_code", nullptr},
                        {"timed_out", false}};

    if (!connection_.empty()) {
        try {
            closed = exchange_owned(connection_, {{"op", "close"}}, 2000).value("closed", false);
        } catch (const std::exception &error) {
            const std::string_view message(error.what());
            shutdown_status_["close_error"] =
                    message.starts_with("TREE_TIMEOUT_UNKNOWN:") ? "timeout" : "transport";
        }
    }

    shutdown_status_["closed_ack"] = closed;
    shutdown_status_["stage"] = "helper_exit";
    // Guest cleanup allows two seconds for workers and one for callbacks, in
    // addition to Binder/ViewServer cleanup and the adb shell exit handshake.
    const auto until = std::chrono::steady_clock::now() + UiObserverShutdown::grace;

    while (UiObserverShutdown::waiting(std::chrono::steady_clock::now(), until, helper_alive())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const bool terminal = !helper_alive();
    shutdown_status_["helper_terminal"] = terminal;

    if (terminal) {
        shutdown_status_["helper_exit_code"] = helper_exit_code();
    }

    shutdown_status_["timed_out"] = !terminal;
    const bool verified = UiObserverShutdown::verified(closed, terminal, helper_exit_code());
    cancel_helper();

    if (verified) {
        try {
            adb_({"shell", "rm", "-rf", guest_}, 2000);
        } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
        }

        std::filesystem::remove(directory_ / "ui-observer-pending.json");
        connection_.clear();
        retired_ = true;
        shutdown_status_["stage"] = "complete";
    }

    return verified;
}
} // namespace voyemu
