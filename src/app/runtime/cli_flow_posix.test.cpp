#include "cli_flow_test_host.hpp"
#include <stdexcept>

voyemu::ProcessResult fixture_public_cli(const std::filesystem::path &,
                                         const std::vector<std::string> &,
                                         const std::filesystem::path &) {
    throw std::logic_error("the split CLI channels are a Windows fixture");
}
