#pragma once

namespace voyemu {
template <class Alive, class Released, class BeforeDeadline, class Verify, class Pause>
bool await_android_stop_identity(Alive alive, Released released, BeforeDeadline before_deadline,
                                 Verify verify, Pause pause) {
    while ((alive() || !released()) && before_deadline()) {
        if (verify()) {
            return true;
        }

        pause();
    }

    return false;
}

// A live SDK child retained by its launch handle (waitpid or job) is owned by construction, so
// host-only termination does not depend on who holds its console tuple; a mismatched generation
// never authorizes it. An exited child proves nothing about the listeners that remain.
constexpr bool allow_unready_android_stop(bool child_alive, bool identity_mismatch) {
    return child_alive && !identity_mismatch;
}

// Host-only termination completes when the child is terminal and every listener it owned is
// closed; a listener it never owned belongs to another process and stays.
constexpr bool android_host_stop_complete(bool child_terminal, bool owned_listeners,
                                          bool ports_released) {
    return child_terminal && (ports_released || !owned_listeners);
}
} // namespace voyemu
