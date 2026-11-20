#include "qnx_guest_shell.hpp"
#include "process.hpp"
#include "qnx_imagefs.hpp"
#include "qnx_passion_platform.hpp"
#include "qnx_prepare.hpp"
#include "qnx_qinggan11.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace voyemu;
namespace {
void need(bool yes, const std::string &message) {
    if (!yes) {
        throw std::runtime_error(message);
    }
}

std::string read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);

    need(bool(input), "cannot read " + path.string());

    return {std::istreambuf_iterator<char>(input), {}};
}

void refuses(std::string_view script, std::string_view code) {
    try {
        (void)prepare_qnx_guest_shell(script);
    } catch (const std::runtime_error &error) {
        need(std::string_view(error.what()).starts_with(code),
             "expected " + std::string(code) + ", got " + error.what());

        return;
    }

    throw std::runtime_error("accepted " + std::string(script));
}

void minifies(std::string_view script, std::string_view expected) {
    const auto actual = prepare_qnx_guest_shell(script);

    need(actual == expected, "minified\n" + std::string(script) + "into\n" + actual);
    need(prepare_qnx_guest_shell(actual) == actual, "not idempotent\n" + actual);
}

#ifndef _WIN32
std::vector<std::filesystem::path> host_shells() {
    std::vector<std::filesystem::path> shells{"/bin/sh"};

    if (std::filesystem::exists("/bin/bash")) {
        shells.emplace_back("/bin/bash");
    }

    return shells;
}

ProcessResult shell(const std::filesystem::path &executable, std::vector<std::string> arguments) {
    ProcessSpec spec;

    spec.executable = executable;
    spec.arguments = std::move(arguments);
    spec.directory = std::filesystem::current_path();

    return execute(spec, 10000, 64 * 1024);
}
#endif

// bash 5.2 and newer print the script they parsed, with a blank line where the source had blank or
// comment lines between commands: apart from those, minification must leave it unchanged. Other
// hosts skip this comparison.
void same_parse(const std::string &original, const std::string &installed,
                const std::string &label) {
#ifndef _WIN32
    if (!std::filesystem::exists("/bin/bash")) {
        return;
    }

    const auto directory = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                           ("voyemu-guest-shell-" + random_token());
    std::filesystem::create_directory(directory);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    auto parse = [&](const std::string &script) {
        std::ofstream(directory / "script.sh", std::ios::binary) << script;
        auto result = shell("/bin/bash", {"--pretty-print", (directory / "script.sh").string()});
        std::string lines;

        for (std::size_t at = 0; at < result.output.size();) {
            const auto end = std::min(result.output.find('\n', at), result.output.size());
            const auto line = std::string_view(result.output).substr(at, end - at);

            if (line.find_first_not_of(" \t") != std::string_view::npos) {
                lines.append(line).append("\n");
            }

            at = end + 1;
        }

        result.output = lines;

        return result;
    };
    const auto probe = parse("echo probe\n");

    if (probe.exit_code != 0 || probe.output.find("echo probe") == std::string::npos) {
        return;
    }

    const auto before = parse(original), after = parse(installed);
    need(before.exit_code == 0 && after.exit_code == 0 && !before.output.empty() &&
                 after.output == before.output,
         label + " parses differently after minification");
#else
    (void)original;
    (void)installed;
    (void)label;
#endif
}

// The largest ssplash the writer accepts: in place for A11, into zero padding for A9 and Passion.
std::size_t ssplash_slot(const std::string &display, bool zero_padding) {
    const auto original = read_qnx_imagefs_file(display, "bin/ssplash");
    std::size_t low = original.size(), high = original.size() + 4096;

    while (zero_padding && low < high) {
        const auto middle = (low + high + 1) / 2;

        try {
            (void)replace_qnx_imagefs_file_in_zero_padding(
                    display, "bin/ssplash", sha256_bytes(original), std::string(middle, 'x'), 4096);
            low = middle;
        } catch (const std::runtime_error &) {
            high = middle - 1;
        }
    }

    return low;
}
} // namespace

int main() try {
    // Comments, indentation and blank lines go; quoted text, expansions and line breaks stay.
    minifies("#!/bin/sh\n# comment\n\n    echo a   # trailing\n\techo b\t\n",
             "#!/bin/sh\necho a\necho b\n");
    minifies("echo \"a # b\" 'c # d' e#f $# ${#x} ${x#*/} ${x##*/}\n",
             "echo \"a # b\" 'c # d' e#f $# ${#x} ${x#*/} ${x##*/}\n");
    minifies("echo a\\ #b \\# c # d\n", "echo a\\ #b \\# c\n");
    minifies("echo \"${x:-a #b}\" ${y:-'c #d'} \"$(echo \"e # f\")\" `echo g`#h\n",
             "echo \"${x:-a #b}\" ${y:-'c #d'} \"$(echo \"e # f\")\" `echo g`#h\n");
    minifies("x='one\n    # two\n'  # three\ny=\"four\n  five\" # six\n",
             "x='one\n    # two\n'\ny=\"four\n  five\"\n");
    minifies("case $x in\n    *'#'*) echo hash ;; # pattern\n    a|b) ;;\nesac\n",
             "case $x in\n*'#'*) echo hash ;;\na|b) ;;\nesac\n");
    minifies("f() {\n    # body\n    echo \"$1\" \\\n        two\\\n  three\n}\n",
             "f() {\necho \"$1\" \\\n two\\\n three\n}\n");
    minifies("echo a \\\n# joined comment\necho b\n", "echo a \\\n\necho b\n");
    minifies("echo a\\\n#b\n", "echo a\\\n#b\n");
    minifies("echo \"$( (echo x) )\" $((1 + 2))\n", "echo \"$( (echo x) )\" $((1 + 2))\n");
    minifies("echo 'it''s' \"q\\\"#\" x\\\\ #y\n", "echo 'it''s' \"q\\\"#\" x\\\\\n");
    refuses("cat <<EOF\n# data\nEOF\n", "QNX_GUEST_SHELL_HEREDOC: line 1 ");
    refuses("echo\ncat <<-'EOF'\nEOF\n", "QNX_GUEST_SHELL_HEREDOC: line 2 ");
    refuses("echo $'a\\'b'\n", "QNX_GUEST_SHELL_QUOTING: line 1 ");
    refuses("x=$(echo a # b\n)\n", "QNX_GUEST_SHELL_COMMENT: line 1 ");
    refuses("x=`echo a # b`\n", "QNX_GUEST_SHELL_COMMENT: line 1 ");
    refuses("echo 'open\n", "QNX_GUEST_SHELL_UNTERMINATED");
    refuses("echo \"${x\n", "QNX_GUEST_SHELL_UNTERMINATED");
    refuses("echo $(true\n", "QNX_GUEST_SHELL_UNTERMINATED");
    refuses("echo \\\n", "QNX_GUEST_SHELL_UNTERMINATED");
    refuses("echo a", "QNX_GUEST_SHELL_SOURCE");
    refuses(std::string("echo \0\n", 7), "QNX_GUEST_SHELL_SOURCE");
#ifndef _WIN32
    // The minified script prints exactly what the original prints.
    const std::string semantics = R"SH(#!/bin/sh
# leading comment
set -- one 'two # words' three   # trailing comment
echo "count $# ${#1}" a#b # comment
v=path/to/file.tar.gz
echo ${v#*/} ${v##*/} ${v%.*} "${v%%.*}" "${unset_value:-default # word}"
echo a\ #not-comment escaped\#hash \# done
    echo "indented"	# tab comment
case "$2" in
    *'#'*) echo hash-in-pattern ;;   # comment after a pattern
    *) echo none ;;
esac
show() {
    # comment in a function
    echo "in show: $1"
}
show "arg # one"
y=$(echo "sub # quoted"; echo sub2)   # comment
echo "$y" "$( (echo nested) )" $(( 2 + 3 ))
z=`echo back`#tail
echo "$z"
multi='line one
    # indented line in quotes

line four'
echo "$multi"
dq="first
  second # still quoted"
echo "$dq"
echo long \
    continued \
line
echo joined\
    word
echo comment after \
# a joined comment
echo end
)SH";
    const auto compact = prepare_qnx_guest_shell(semantics);
    need(compact.size() < semantics.size() && compact.find("# leading") == std::string::npos,
         "semantic fixture did not shrink");
    same_parse(semantics, compact, "semantic fixture");

    for (const auto &executable : host_shells()) {
        const auto before = shell(executable, {"-c", semantics}),
                   after = shell(executable, {"-c", compact});
        need(before.exit_code == 0 && !before.timed_out && after.exit_code == before.exit_code &&
                     after.output == before.output &&
                     before.output.find("end") != std::string::npos,
             executable.string() + " output changed:\n" + before.output + "---\n" + after.output);
    }
#endif
    const auto root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    const auto guest = root / "src/guest/qnx";
    unsigned scripts = 0;

    // Every shipped guest script minifies, stays idempotent and parses as before.
    for (const auto &item : std::filesystem::directory_iterator(guest)) {
        const auto name = item.path().filename().string();

        if (item.path().extension() != ".sh" || name.ends_with(".test.sh")) {
            continue;
        }

        const auto source = read(item.path()), installed = prepare_qnx_guest_shell(source);
        need(prepare_qnx_guest_shell(installed) == installed && installed.size() <= source.size(),
             name + " minification");
#ifndef _WIN32
        for (const auto &executable : host_shells()) {
            const auto result = shell(executable, {"-n", "-c", installed});
            need(result.exit_code == 0, name + " fails " + executable.string() +
                                                " -n after minification: " + result.output);
        }
#endif
        same_parse(source, installed, name);
        std::cout << name << ": " << source.size() << " -> " << installed.size() << " bytes\n";
        ++scripts;
    }

    need(scripts >= 4, "shipped guest scripts missing");
    // The installed runtime of each adapter fits its OEM bin/ssplash slot, which preparation does
    // not grow: in place on A11, into the zero padding that follows it on A9 and Passion.
    const auto runtime = read(guest / "qnx-runtime.sh");
    const auto qinggan09 = qnx_qinggan09_runtime(runtime, read(guest / "persistent-runtime.sh"),
                                                 true),
               qinggan11 = qnx_kanzi_gauge_runtime(prepare_qnx_qinggan11_runtime(
                       qnx_native_mcu_async_runtime(runtime), std::string(64, '0')));
    same_parse(qinggan09, prepare_qnx_guest_shell(qinggan09), "A9 runtime");
    same_parse(qinggan11, prepare_qnx_guest_shell(qinggan11), "A11 runtime");
    struct Install {
        std::string profile, bytes;
        std::size_t slot;
        bool zero_padding;
    };
    // Every profile with a QNX installs the runtime of its hardware adapter; both qinggan11
    // adapters install the same runtime.
    std::vector<Install> installs;

    for (const auto &entry : std::filesystem::directory_iterator(root / "profiles")) {
        const auto definition = entry.path() / "profile-qnx.json";

        if (!std::filesystem::is_regular_file(definition)) {
            continue;
        }

        const auto adapter =
                read_private_json(definition, 128 * 1024).at("hardware_adapter").get<std::string>();
        const auto profile = entry.path().filename().string();

        if (adapter == "sa8155-qinggan09") {
            installs.push_back({profile, prepare_qnx_guest_shell(qinggan09), 24576, true});
        } else if (adapter == "sa8155-passion-21480") {
            installs.push_back(
                    {profile,
                     prepare_qnx_passion_11713_runtime(read(guest / "qnx-passion-runtime.sh")),
                     24576, true});
        } else {
            installs.push_back({profile, prepare_qnx_guest_shell(qinggan11), 21210, false});
        }
    }

    need(!installs.empty(), "profiles with a QNX definition");

    for (const auto &install : installs) {
        const auto &bytes = install.bytes;
        const auto image = root / ".local/profiles" / install.profile / "qnx/ifs2_la.img";
        auto slot = install.slot;

        if (std::filesystem::is_regular_file(image)) {
            slot = ssplash_slot(read_qnx_imagefs_file(read(image), "ifs_display.img"),
                                install.zero_padding);
            need(slot == install.slot, install.profile + " ssplash slot is " +
                                               std::to_string(slot) + " bytes, the test expects " +
                                               std::to_string(install.slot));
        }

        need(bytes.size() <= slot, install.profile + " runtime needs " +
                                           std::to_string(bytes.size()) + " bytes of the " +
                                           std::to_string(slot) + "-byte ssplash slot");
        std::cout << install.profile << ": installed " << bytes.size() << " of " << slot
                  << " bytes, margin " << slot - bytes.size()
                  << (std::filesystem::is_regular_file(image) ? " (OEM image)\n" : "\n");
    }

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
