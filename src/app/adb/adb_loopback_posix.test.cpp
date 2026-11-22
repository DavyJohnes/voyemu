#include "adb_loopback_test_host.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <pthread.h>
#include <signal.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>

using namespace std::chrono_literals;

static void noop_signal(int) {}

InterruptedCall interrupted_call(const std::function<void()> &call,
                                 const std::function<void()> &stop) {
    struct Handler {
        struct sigaction previous{};
        bool installed = false;

        Handler() {
            struct sigaction action{};
            action.sa_handler = noop_signal;
            action.sa_flags = 0;
            sigemptyset(&action.sa_mask);

            if (sigaction(SIGUSR1, &action, &previous) != 0) {
                throw std::runtime_error("SIGUSR1 handler installation failed");
            }

            installed = true;
        }

        ~Handler() {
            if (installed) {
                sigaction(SIGUSR1, &previous, nullptr);
            }
        }

        void restore() {
            if (sigaction(SIGUSR1, &previous, nullptr) != 0) {
                throw std::runtime_error("SIGUSR1 handler restoration failed");
            }

            installed = false;
        }
    } handler;
    InterruptedCall result;
    std::atomic<bool> finished{false};
    std::atomic<int> mask_status{0};
    const auto start = std::chrono::steady_clock::now();
    std::thread thread([&] {
        sigset_t signal_set, previous;
        sigemptyset(&signal_set);
        sigaddset(&signal_set, SIGUSR1);
        mask_status = pthread_sigmask(SIG_UNBLOCK, &signal_set, &previous);
        call();

        if (mask_status == 0) {
            mask_status = pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        }

        finished = true;
    });

    while (!finished && std::chrono::steady_clock::now() - start < 6200ms) {
        auto status = pthread_kill(thread.native_handle(), SIGUSR1);

        if (status != 0 && !(status == ESRCH && finished)) {
            result.signal_error = status;
            break;
        }

        if (status == 0) {
            result.signals++;
        }

        std::this_thread::sleep_for(20ms);
    }

    while (!finished && std::chrono::steady_clock::now() - start < 6700ms) {
        std::this_thread::sleep_for(10ms);
    }

    result.finished = finished;
    stop();
    thread.join();
    handler.restore();
    result.mask_error = mask_status;

    return result;
}

std::pair<voyemu::os::SocketHandle, voyemu::os::SocketHandle> local_socket_pair() {
    int descriptors[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        throw std::runtime_error("owned local socketpair failed");
    }

    return {descriptors[0], descriptors[1]};
}
