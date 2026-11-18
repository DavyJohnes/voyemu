#include "qnx_startup_extension.hpp"
#include <exception>
#include <functional>
#include <iostream>

using namespace voyemu;

static void require(bool value) {
    if (!value) {
        throw std::runtime_error("startup extension assertion failed");
    }
}

static void rejects(const std::function<void()> &action) {
    bool failed = false;

    try {
        action();
    } catch (const std::runtime_error &) {
        failed = true;
    }

    require(failed);
}

int main() try {
    const std::string original = "#!/bin/sh\necho synthetic\nclusterboot &\necho complete\n";
    const auto pin = sha256_bytes(original);
    const auto at = original.find("clusterboot &");
    auto insert = [&](const std::string &hook) {
        return original.substr(0, at) + hook + original.substr(at);
    };

    require(qnx_startup_extension(original, original, pin).empty());
    const std::string hook = ". /var/example/startup.sh /var/example /var/example/boot.json\n";
    require(qnx_startup_extension(original, insert(hook), pin) == hook);
    rejects([&] {
        qnx_startup_extension(original, insert(hook), std::string(64, '0'));
    });
    auto changed = insert(hook);
    changed[2] = 'X';
    rejects([&] {
        qnx_startup_extension(original, changed, pin);
    });
    changed = insert(hook);
    changed.back() = 'X';
    rejects([&] {
        qnx_startup_extension(original, changed, pin);
    });
    rejects([&] {
        qnx_startup_extension(original, insert("clusterboot &\n"), pin);
    });
    const auto duplicate = original + "clusterboot &\n";
    rejects([&] {
        qnx_startup_extension(duplicate, duplicate, sha256_bytes(duplicate));
    });
    rejects([&] {
        qnx_startup_extension(original, insert(std::string("echo \0bad\n", 10)), pin);
    });
    auto nul = original;
    nul[1] = '\0';
    rejects([&] {
        qnx_startup_extension(nul, nul, sha256_bytes(nul));
    });
    rejects([&] {
        qnx_startup_extension(original, original.substr(1), pin);
    });
    rejects([&] {
        qnx_startup_extension(original, insert(std::string(65536, 'x') + "\n"), pin);
    });
    require(qnx_startup_extension(original, insert(std::string(65535, 'x') + "\n"), pin).size() ==
            65536);
    const std::string oversized(65537, 'x');
    rejects([&] {
        qnx_startup_extension(oversized, oversized, sha256_bytes(oversized));
    });
    rejects([&] {
        qnx_startup_extension(original, original, pin, "");
    });
    rejects([&] {
        qnx_startup_extension(original, original, pin, "clusterboot &\n");
    });
    const std::string partial = "#!/bin/sh\necho clusterboot &\n";
    rejects([&] {
        qnx_startup_extension(partial, partial, sha256_bytes(partial));
    });
    std::cout << "QNX startup extension guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
