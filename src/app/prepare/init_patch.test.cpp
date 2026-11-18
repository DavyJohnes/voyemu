#include "init_patch.hpp"
#include "model.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

static void check(bool ok, const char *text) {
    if (!ok) {
        throw std::runtime_error(text);
    }
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "profile steps directory required");
        check(init_patch_recipes().size() == 6, "six native init recipes expected");

        for (const auto &recipe : init_patch_recipes()) {
            std::ifstream file(std::filesystem::path(argv[1]) / (std::string(recipe.id) + ".json"));
            check(bool(file), "reference recipe exists");
            Json source;
            file >> source;
            check(source.at("api") == 28 && source.at("calls").size() == 1,
                  "reference API and operation preserved");
            const auto &call = source.at("calls")[0];
            const auto &args = call.at("args");
            check(call.at("operation") == "editLines" && args.size() == 3 && args[0] == "output",
                  "only line-coordinate editing allowed");
            const auto &edits = args[1];
            check(edits.size() == 1 && edits[0].at("startLine") == recipe.insert_line &&
                          edits[0].at("deleteCount") == 0 &&
                          edits[0].at("insert") == Json::array({{{"text", "    disabled\n"}}}),
                  "native coordinate matches profile recipe");
            check(args[2].get<std::string>().size() == 64, "result is SHA pinned");
            std::string prefix;

            for (unsigned n = 1; n < recipe.insert_line; ++n) {
                prefix += "fixture line\n";
            }

            const std::string input = prefix + "tail\n", expected = prefix + "    disabled\ntail\n";
            check(apply_init_patch(recipe.id, 28, input) == expected, "only declared line changes");

            for (const auto &invalid : {std::string("missing"), expected}) {
                bool rejected = false;
                try {
                    apply_init_patch(recipe.id, 28, invalid);
                } catch (...) {
                    rejected = true;
                }

                check(rejected, "short or already applied source rejected");
            }

            bool rejected = false;
            try {
                apply_init_patch(recipe.id, 30, input);
            } catch (...) {
                rejected = true;
            }

            check(rejected, "Android 11 cannot use Android 9 recipe");
        }

        bool rejected = false;
        try {
            apply_init_patch("unknown", 28, "");
        } catch (...) {
            rejected = true;
        }

        check(rejected, "unknown recipe rejected");
        std::cout << "six native init patches match line-only profile recipes\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
