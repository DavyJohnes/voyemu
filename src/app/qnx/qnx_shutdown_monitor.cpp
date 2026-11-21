#include "qnx_shutdown_monitor.hpp"
#include "error.hpp"
#include "qnx_shutdown_channel.hpp"
#include <chrono>
#include <functional>
#include <stdexcept>
namespace voyemu {
void qnx_shutdown_channel_require(bool yes) {
    if (!yes) {
        throw VoyemuError("QNX_SHUTDOWN_MONITOR", "owned QMP channel unverified");
    }
}

struct QnxShutdownMonitor::Impl {
    Json endpoint;
    std::unique_ptr<QnxShutdownChannel> channel;
    std::uint64_t generation = 0;
    std::int64_t pid = 0;
    bool subscribed = false, eof = false, fault = false, shutdown = false, guest = false;
    std::string reason, carry;
    using Deadline = std::chrono::steady_clock::time_point;

    void require(bool yes) {
        qnx_shutdown_channel_require(yes);
    }

    void event(const Json &value) {
        if (!value.is_object()) {
            throw VoyemuError("QNX_SHUTDOWN_MONITOR", "malformed event");
        }

        if (value.value("event", std::string{}) != "SHUTDOWN") {
            return;
        }

        const auto &data = value.at("data");

        if (!data.is_object() || !data.contains("guest") || !data.at("guest").is_boolean() ||
            !data.contains("reason") || !data.at("reason").is_string()) {
            throw VoyemuError("QNX_SHUTDOWN_MONITOR", "malformed shutdown cause");
        }

        const auto why = data.at("reason").get<std::string>();
        const bool by_guest = data.at("guest").get<bool>();

        if (shutdown && (reason != why || guest != by_guest)) {
            throw VoyemuError("QNX_SHUTDOWN_MONITOR", "conflicting shutdown causes");
        }

        shutdown = true;
        reason = why;
        guest = by_guest;
    }

    std::string line(Deadline deadline) {
        for (;;) {
            const auto end = carry.find('\n');

            if (end != carry.npos) {
                auto out = carry.substr(0, end);
                carry.erase(0, end + 1);

                return out;
            }

            channel->wait(false, deadline);
            char bytes[4096];
            const auto count = channel->receive(bytes, sizeof bytes);

            if (count < 0) {
                continue;
            }

            require(count > 0 && carry.size() + static_cast<std::size_t>(count) <= 16384);
            carry.append(bytes, static_cast<std::size_t>(count));
        }
    }

    void send(std::string_view message, Deadline deadline) {
        while (!message.empty()) {
            channel->wait(true, deadline);
            const auto count = channel->send(message.data(), static_cast<int>(message.size()));

            if (count < 0) {
                continue;
            }

            require(count > 0);
            message.remove_prefix(static_cast<std::size_t>(count));
        }
    }
};

QnxShutdownMonitor::QnxShutdownMonitor(Json endpoint, std::uint64_t generation)
    : impl_(std::make_unique<Impl>()) {
    impl_->endpoint = std::move(endpoint);
    impl_->generation = generation;
    impl_->channel = std::make_unique<QnxShutdownChannel>(impl_->endpoint);
    impl_->require(generation > 0 && impl_->channel->valid());
}

QnxShutdownMonitor::~QnxShutdownMonitor() = default;

void QnxShutdownMonitor::connect_owned(std::int64_t pid,
                                       const std::function<void()> &verify_owner) {
    auto &p = *impl_;

    if (p.subscribed) {
        p.require(p.pid == pid);

        return;
    }

    p.require(pid > 1 && !p.eof && !p.fault);
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        p.channel->connect(pid, deadline);
        p.channel->verify(verify_owner);
        const auto greeting = Json::parse(p.line(deadline));
        const auto &version = greeting.at("QMP").at("version").at("qemu");
        p.require(version.at("major") == 11 && version.at("minor") == 1 &&
                  version.at("micro") == 2);
        p.send("{\"execute\":\"qmp_capabilities\",\"id\":\"shutdown-events\"}\n", deadline);
        bool acknowledged = false;

        for (unsigned i = 0; i < 32; ++i) {
            const auto reply = Json::parse(p.line(deadline));

            if (reply.value("id", std::string{}) == "shutdown-events") {
                p.require(reply.contains("return") && !reply.contains("error"));
                acknowledged = true;
                break;
            }

            p.event(reply);
        }

        p.require(acknowledged);
        p.channel->verify(verify_owner);
        p.pid = pid;
        p.subscribed = true;
    } catch (...) {
        p.channel->close();
        p.carry.clear();
        p.shutdown = false;
        p.reason.clear();
        throw;
    }
}

void QnxShutdownMonitor::drain() {
    auto &p = *impl_;

    if (!p.subscribed || p.eof || p.fault) {
        return;
    }

    try {
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            for (;;) {
                const auto end = p.carry.find('\n');

                if (end == p.carry.npos) {
                    break;
                }

                const auto value = Json::parse(p.carry.substr(0, end));
                p.carry.erase(0, end + 1);
                p.event(value);
            }

            char bytes[4096];
            const auto count = p.channel->receive(bytes, sizeof bytes);

            if (count > 0) {
                p.require(p.carry.size() + static_cast<std::size_t>(count) <= 16384);
                p.carry.append(bytes, static_cast<std::size_t>(count));
                continue;
            }

            if (count == 0) {
                p.eof = true;
                p.require(p.carry.empty());

                return;
            }

            if (count < 0) {
                return;
            }

            p.require(false);
        }

        p.require(false);
    } catch (...) {
        p.fault = true;
        p.shutdown = false;
        p.reason.clear();
    }
}

bool QnxShutdownMonitor::guest_reset(std::int64_t pid, std::uint64_t generation) const {
    const auto &p = *impl_;

    return p.subscribed && !p.fault && p.shutdown && p.guest && p.reason == "guest-reset" &&
           p.pid == pid && p.generation == generation;
}

Json QnxShutdownMonitor::evidence() const {
    const auto &p = *impl_;

    return {{"source", "owned_QMP_SHUTDOWN"},
            {"pid", p.pid},
            {"generation", p.generation},
            {"subscribed", p.subscribed},
            {"shutdown", p.shutdown},
            {"guest", p.guest},
            {"reason", p.reason},
            {"fault", p.fault},
            {"eof", p.eof}};
}

void QnxShutdownMonitor::finish_owned_exit(std::int64_t pid, std::uint64_t generation) {
    auto &p = *impl_;

    p.require((!p.subscribed || p.pid == pid) && p.generation == generation);
    p.channel->finish();
}
} // namespace voyemu
