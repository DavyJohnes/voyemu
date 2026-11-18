#pragma once
#include "error.hpp"
#include "ui_observer_diagnostics.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace voyemu {
inline constexpr std::size_t observer_response_bytes = 6 * 1024 * 1024;

template <class Open>
Json exchange_ui_observer(const Json &connection, Json message, std::string_view auth,
                          int timeout_ms, Open open) {
    if (timeout_ms < 1 || timeout_ms > 35000) {
        throw VoyemuError("TEST_ARGUMENT", "bounded observer transport required");
    }

    message["schema"] = 1;
    message["auth"] = auth;
    const auto payload = message.dump() + "\n";

    if (payload.size() > 8192) {
        throw VoyemuError("TEST_ARGUMENT", "observer request exceeds bound");
    }

    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::milliseconds(timeout_ms);
    std::string response;
    const auto timeout = [&](std::string_view phase) {
        try {
            const auto operation = message.value("op", std::string{});
            const auto pid = connection.value("observer_pid", std::int64_t{});
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - started)
                                         .count();
            std::cerr << observer_wire_timeout(operation, phase, timeout_ms, elapsed,
                                               response.size(), pid)
                      << '\n';
        } catch (...) { /* Ignored: diagnostics must preserve the original typed timeout. */
        }

        throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "observer deadline elapsed");
    };
    auto stream = open();
    std::size_t written = 0;
    bool opened = false;

    for (;;) {
        const auto phase = opened ? (written == payload.size() ? "recv" : "send") : "connect";

        if (std::chrono::steady_clock::now() >= deadline) {
            timeout(phase);
        }

        auto state = stream.poll();
        opened = opened || state.opened;

        if (opened && written < payload.size()) {
            written += stream.write(std::string_view(payload).substr(written));
        }

        if (!state.output.empty()) {
            if (written != payload.size()) {
                throw VoyemuError("TREE_PROTOCOL", "observer replied before the request was sent");
            }

            const auto previous = response.size();
            response += state.output;

            if (response.size() > observer_response_bytes + 1) {
                throw VoyemuError("TREE_LIMIT", "response exceeds bound");
            }

            const auto end = response.find('\n', previous);

            if (end != std::string::npos) {
                if (end != response.size() - 1) {
                    throw VoyemuError("TREE_PROTOCOL", "multiple frames rejected");
                }

                auto result = Json::parse(response.begin(), response.begin() + end, nullptr, false);

                if (!result.is_object()) {
                    throw VoyemuError("TREE_PROTOCOL", "JSON object required");
                }

                if (result.dump().find(auth) != std::string::npos) {
                    throw VoyemuError("TREE_PROTOCOL", "private observer credential in response");
                }

                return result;
            }
        }

        if (state.terminal) {
            if (state.terminal->timed_out) {
                timeout(phase);
            }

            throw std::runtime_error(
                    opened ? "TREE_TRANSPORT_UNKNOWN: observer closed before response"
                           : "TREE_TRANSPORT_UNKNOWN: observer unavailable");
        }

        if (state.output.empty()) {
            std::this_thread::sleep_until(std::min(deadline, std::chrono::steady_clock::now() +
                                                                     std::chrono::milliseconds(2)));
        }
    }
}
} // namespace voyemu
