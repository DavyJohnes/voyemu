#include "egl_handles.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu::graphics;

template <class F> void rejected(F f) {
    bool failed = false;

    try {
        f();
    } catch (const std::runtime_error &) {
        failed = true;
    }

    assert(failed);
}

int main() try {
    EglHandles first;
    auto display = first.expose(EglKind::display, 0x1000),
         context = first.expose(EglKind::context, 0x2000);

    assert(display != 0x1000 && context != 0x2000 && display != context);
    assert(first.expose(EglKind::display, 0x1000) == display);
    assert(first.resolve(EglKind::display, display) == 0x1000);
    assert(first.resolve(EglKind::context, 0) == 0);
    rejected([&] {
        first.resolve(EglKind::context, display);
    });
    rejected([&] {
        first.resolve(EglKind::context, 0xdead);
    });
    EglHandles second;
    assert(second.expose(EglKind::display, 0x3000, display) == display);
    assert(second.expose(EglKind::context, 0x4000, context) == context);
    assert(second.resolve(EglKind::context, context) == 0x4000);
    rejected([&] {
        second.expose(EglKind::surface, 0x5000, context);
    });
    rejected([&] {
        second.expose(EglKind::display, 0x3000, context);
    });
    rejected([&] {
        second.expose(EglKind::context, 0, context);
    });
    second.release(EglKind::context, context);
    rejected([&] {
        second.resolve(EglKind::context, context);
    });
    auto reused = second.expose(EglKind::context, 0x4000);
    assert(reused != context);
    assert(second.resolve(EglKind::context, reused) == 0x4000);
    EglHandles legacy;
    assert(legacy.expose(EglKind::display, 0x6000, 0x12345) == 0x12345);
    assert(legacy.resolve(EglKind::display, 0x12345) == 0x6000);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
