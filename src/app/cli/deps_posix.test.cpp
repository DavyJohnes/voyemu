#include "deps_test_host.hpp"

void with_foreign_write(const std::filesystem::path &file, const std::function<void()> &granted) {
    std::filesystem::permissions(file, std::filesystem::perms::group_write,
                                 std::filesystem::perm_options::add);
    granted();
    std::filesystem::permissions(file, std::filesystem::perms::group_write,
                                 std::filesystem::perm_options::remove);
}
