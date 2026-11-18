#pragma once
#include "model.hpp"
#include "process.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
namespace voyemu {
// Called only after the owner has verified observer shutdown.
void clear_ui_observer_metadata(const std::filesystem::path &directory, const std::string &session);
// Both the owned supervisor channel and the preparation harness feed the same observer protocol.
class UiObserverStream {
public:
    struct Poll {
        bool opened;
        std::string output;
        std::optional<ProcessResult> terminal;
    };

    template <class Stream>
    explicit UiObserverStream(std::shared_ptr<Stream> stream)
        : poll_([stream] {
              auto state = stream->poll();

              return Poll{state.opened, std::move(state.output), std::move(state.terminal)};
          }),
          write_([stream](std::string_view bytes) {
              return stream->write(bytes);
          }) {}

    Poll poll() {
        return poll_();
    }

    std::size_t write(std::string_view bytes) {
        return write_(bytes);
    }

private:
    std::function<Poll()> poll_;
    std::function<std::size_t(std::string_view)> write_;
};
class UiObserver {
public:
    using Adb = std::function<ProcessResult(const std::vector<std::string> &, int)>;
    using OpenShell = std::function<UiObserverStream(const std::vector<std::string> &)>;
    using OpenObserver = std::function<UiObserverStream(std::string_view, int)>;
    UiObserver(std::filesystem::path directory, std::string session, Json android, Adb adb,
               OpenShell shell, OpenObserver observer);
    ~UiObserver();
    Json connection(int timeout_ms = 5000, bool recover = false);
    bool stop();
    void retire_for_boot(const std::string &previous, const std::string &boot);
    Json shutdown_status();
    Json exchange(const Json &connection, Json message, int timeout_ms);

private:
    std::timed_mutex mutex_;
    std::filesystem::path directory_;
    std::string session_, auth_, guest_, observer_id_;
    Json android_, connection_;
    Json shutdown_status_ = Json::object();
    bool retired_ = false;
    Adb adb_;
    OpenShell open_shell_;
    OpenObserver open_observer_;
    std::mutex helper_mutex_;
    std::optional<ProcessResult> helper_terminal_;
    std::jthread helper_;
    Json exchange_owned(const Json &connection, Json message, int timeout_ms);
    void start_helper(const std::vector<std::string> &args);
    bool helper_alive();
    int helper_exit_code();
    void cancel_helper();
};
} // namespace voyemu
