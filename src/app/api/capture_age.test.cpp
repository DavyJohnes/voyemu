#include "capture_age.hpp"
#include <cassert>
#include <exception>
#include <iostream>
#include <limits>
using namespace voyemu::api;

int main() try {
    assert(capture_age_upper_us(100000, {100000, 1000}, {101000, 2000}) == 3000);
    assert(capture_age_upper_us(101000, {100000, 1000}, {101000, 2000}) == 2000);
    assert(capture_age_upper_us(1, {100000, 1000}, {101000, 2000}) == 102999);
    assert(capture_age_upper_us(100000, {100000, 1000}, {103000, 2000}) == 5000);
    auto rejects = [](std::uint64_t source, CaptureClock before, CaptureClock after) {
        bool rejected = false;
        try {
            capture_age_upper_us(source, before, after);
        } catch (const std::runtime_error &) {
            rejected = true;
        }

        assert(rejected);
    };
    rejects(0, {100, 100}, {101, 101});
    rejects(102, {100, 100}, {101, 101});
    rejects(90, {100, 100}, {99, 101});
    rejects(90, {100, 100}, {101, 99});
    rejects(90, {100, 100}, {3102, 101});
    rejects(90, {100, 100}, {101, 3102});
    rejects(1, {1, 1},
            {std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max()});
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
