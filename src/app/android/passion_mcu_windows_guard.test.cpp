#include "android_reboot.hpp"
#include "model.hpp"
#include "passion_mcu_property.hpp"
#include "process.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

int main() try {
#ifdef NDEBUG
    throw std::runtime_error("assertions required");
#endif
    bool rejected = false;
    try {
        // Exercise the actual unsupported branch on a host without a Windows runtime.
        voyemu::passion_mcu_available(
                voyemu::Json{{"mcu_property_adapter", voyemu::Json::object()}}, true);
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()) ==
                   "PASSION_MCU: Windows partial projection is unsupported";
    }

    assert(rejected);
    std::cout
            << "PASS Windows unsupported capability branch; host-exercised, no Windows runtime claim\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
