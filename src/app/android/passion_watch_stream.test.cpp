#include "passion_watch_stream.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
using namespace voyemu::media;
struct Fake {
    bool running = true, preserved = false;
    unsigned stopped{};
    std::string bytes;
};

int main() try {
    std::vector<std::shared_ptr<Fake>> children;
    unsigned accepted = 0, lost = 0;
    PassionPointerWake::Binding b{true, "session", "boot", "watch"};
    PassionWatchStream stream(
            b,
            [&](const std::vector<std::string> &args) {
                assert((args == std::vector<std::string>{"shell", "cmd", "display",
                                                         "voyemu-passion-screen", "watch",
                                                         "session", "watch", "boot"}));
                auto child = std::make_shared<Fake>();
                children.push_back(child);

                return PassionWatchStream::Child{[child] {
                                                     return child->running;
                                                 },
                                                 [child](std::size_t n) {
                                                     auto out = child->bytes.substr(0, n);
                                                     child->bytes.erase(0, out.size());

                                                     return out;
                                                 },
                                                 [child] {
                                                     child->running = false;
                                                     ++child->stopped;
                                                 },
                                                 [child] {
                                                     child->preserved = true;
                                                 }};
            },
            [] {},
            [&](const Json &) {
                ++accepted;
            },
            [&] {
                ++lost;
            });
    const std::string event =
            "{\"kind\":\"display_state_event\",\"session_id\":\"session\",\"watch_id\":\"watch\",\"boot_id\":\"boot\"}";
    const std::string end =
            "{\"schema\":1,\"kind\":\"display_state_end\",\"session_id\":\"session\",\"watch_id\":\"watch\",\"boot_id\":\"boot\",\"reason\":\"byte_budget\"}\n";

    stream.poll(1);
    children[0]->bytes = event;
    stream.poll(2);
    assert(!accepted);
    children[0]->bytes = "\n";
    stream.poll(3);
    assert(accepted == 1);
    stream.recheck();
    stream.poll(4);
    assert(children.size() == 2 && stream.retained() == 2);
    children[0]->running = false;
    stream.poll(5);
    assert(children[0]->stopped == 1 && stream.retained() == 1);
    children[1]->bytes = end;
    stream.poll(6);
    assert(children.size() == 3 && stream.retained() == 2);
    children[1]->running = false;
    stream.poll(7);
    assert(stream.retained() == 1);
    children[2]->bytes = end + event + "\n";
    bool rejected = false;
    try {
        stream.poll(8);
    } catch (...) {
        rejected = true;
    }

    assert(rejected && children[2]->preserved && stream.retained() == 0 && lost);
    auto count = children.size();

    for (unsigned i = 9; i < 100; i++) {
        stream.poll(i);
    }

    assert(children.size() == count);
    stream.poll(2000008);
    assert(children.size() == count + 1);
    children.back()->bytes = "{";
    children.back()->running = false;
    rejected = false;
    try {
        stream.poll(2000009);
    } catch (...) {
        rejected = true;
    }

    assert(rejected && children.back()->preserved && stream.retained() == 0);
    stream.poll(4000009);
    children.back()->running = false;
    rejected = false;
    try {
        stream.poll(4000010);
    } catch (...) {
        rejected = true;
    }

    assert(rejected && children.back()->preserved);
    stream.poll(6000010);
    children.back()->bytes = std::string(4097, 'x');
    rejected = false;
    try {
        stream.poll(6000011);
    } catch (...) {
        rejected = true;
    }

    assert(rejected && children.back()->preserved && stream.retained() == 0);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
