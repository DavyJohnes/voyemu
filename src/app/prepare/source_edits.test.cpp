#include "source_edits.hpp"
#include "process.hpp"
#include <iostream>
using namespace voyemu;

static void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char **argv) {
    try {
        if (argc == 5 && std::string_view(argv[1]) == "--materialize") {
            std::cout << materialize_native_component_sources(std::filesystem::canonical(argv[2]),
                                                              std::filesystem::canonical(argv[3]),
                                                              std::filesystem::path(argv[4]))
                                 .dump(2)
                      << '\n';

            return 0;
        }

        const auto source = std::string("one\r\ntwo\nthree");
        const auto edit = Json::array(
                {{{"startLine", 2},
                  {"deleteCount", 1},
                  {"insert", Json::array({{{"text", "OWN\n"}},
                                          {{"copy", {{"startLine", 1}, {"lineCount", 1}}}}})}}});
        check(assemble_source_edits(source, edit) == "one\r\nOWN\none\r\nthree",
              "original coordinates and CRLF bytes preserved");
        check(assemble_source_edits(
                      "one\n", Json::array({{{"startLine", 2},
                                             {"deleteCount", 0},
                                             {"insert", Json::array({{{"text", "two\n"}}})}}})) ==
                      "one\ntwo\n",
              "append after final terminated line");

        for (unsigned mode = 0; mode < 4; ++mode) {
            auto invalid = edit;

            if (mode == 0) {
                invalid[0]["startLine"] = 0;
            }

            if (mode == 1) {
                invalid[0]["deleteCount"] = 99;
            }

            if (mode == 2) {
                invalid.push_back(invalid[0]);
            }

            if (mode == 3) {
                invalid[0]["insert"][1]["copy"]["lineCount"] = 99;
            }

            bool rejected = false;
            try {
                assemble_source_edits(source, invalid);
            } catch (...) {
                rejected = true;
            }

            check(rejected, "invalid or overlapping edits rejected");
        }

        std::cout << "native source snapshot edits, copy ranges, line endings and bounds passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
