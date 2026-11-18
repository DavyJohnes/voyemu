#include "qnx_guest_shell.hpp"
#include "error.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace voyemu {
namespace {
// The scanner follows the quoting that decides where a comment can begin. command is the body of
// $(...) and $((...)), whose parentheses it counts; parameter is the body of ${...}.
enum class Context {
    code,
    command,
    backtick,
    double_quote,
    single_quote,
    parameter
};
struct Frame {
    Context context;
    unsigned depth = 0;
};

bool code_like(Context context) {
    return context == Context::code || context == Context::command || context == Context::backtick;
}

void refuse(const char *code, std::size_t line, const char *reason) {
    throw VoyemuError(code, "line " + std::to_string(line) + " " + reason);
}
} // namespace

std::string prepare_qnx_guest_shell(std::string_view script) {
    if (script.empty() || script.back() != '\n' || script.find('\0') != std::string_view::npos) {
        throw VoyemuError("QNX_GUEST_SHELL_SOURCE",
                          "a guest shell script is text that ends with a line feed");
    }

    std::string output;
    std::vector<Frame> stack{{Context::code}};
    // continued: the previous line ended with an escaped line feed outside quotes, so this line
    // joins it; joined_word_start says whether a word began right before that backslash.
    bool continued = false, joined_word_start = true;
    std::size_t at = 0, number = 0;

    if (script.starts_with("#!")) {
        at = script.find('\n') + 1;
        output.append(script.substr(0, at));
        ++number;
    }

    while (at < script.size()) {
        const auto end = script.find('\n', at);
        const auto line = script.substr(at, end - at);
        at = end + 1;
        ++number;
        const bool starts_in_code = code_like(stack.back().context);
        std::string text;
        // kept: the length of text without its trailing unquoted blanks.
        std::size_t i = 0, kept = 0;
        bool word_start = true, continues = false;

        if (starts_in_code) {
            i = std::min(line.find_first_not_of(" \t"), line.size());

            if (continued) {
                // A joined line keeps one blank where it had indentation, so words stay apart.
                word_start = joined_word_start || i > 0;
                text = i > 0 ? " " : "";
            }
        }

        for (; i < line.size(); ++i) {
            const auto context = stack.back().context;
            const char c = line[i], next = i + 1 < line.size() ? line[i + 1] : '\n';
            bool blank = false;

            if (context == Context::single_quote) {
                if (c == '\'') {
                    stack.pop_back();
                }

                text += c;
                kept = text.size();
                continue;
            }

            if (c == '\\') {
                text += c;

                if (i + 1 == line.size()) {
                    kept = text.size();
                    continues = code_like(context);
                    break;
                }

                text += next;
                kept = text.size();
                word_start = false;
                ++i;
                continue;
            }

            if (code_like(context)) {
                if (c == '#' && word_start && context != Context::code) {
                    // Shells disagree on whether such a comment hides the closing ) or `.
                    refuse("QNX_GUEST_SHELL_COMMENT", number,
                           "has a comment inside a command substitution");
                }

                if (c == '#' && word_start) {
                    break;
                }

                if (c == '<' && next == '<') {
                    refuse("QNX_GUEST_SHELL_HEREDOC", number,
                           "starts a here-document, which guest shell scripts may not use");
                }

                if (c == '$' && next == '\'') {
                    refuse("QNX_GUEST_SHELL_QUOTING", number,
                           "uses $'...' quoting, which guest shell scripts may not use");
                }

                blank = c == ' ' || c == '\t';

                if (c == '\'') {
                    stack.push_back({Context::single_quote});
                } else if (c == '"') {
                    stack.push_back({Context::double_quote});
                } else if (c == '`' && context == Context::backtick) {
                    stack.pop_back();
                } else if (c == '`') {
                    stack.push_back({Context::backtick});
                } else if (c == '(' && context == Context::command) {
                    ++stack.back().depth;
                } else if (c == ')' && context == Context::command) {
                    if (stack.back().depth == 0) {
                        stack.pop_back();
                    } else {
                        --stack.back().depth;
                    }
                }
            } else if ((c == '}' && context == Context::parameter) ||
                       (c == '"' && context == Context::double_quote)) {
                stack.pop_back();
            } else if (c == '"') {
                stack.push_back({Context::double_quote});
            } else if (c == '\'' && context == Context::parameter) {
                stack.push_back({Context::single_quote});
            } else if (c == '`') {
                stack.push_back({Context::backtick});
            }

            text += c;

            if (c == '$' && (next == '(' || next == '{')) {
                stack.push_back({next == '(' ? Context::command : Context::parameter});
                text += next;
                ++i;
            }

            if (!blank) {
                kept = text.size();
            }

            word_start = blank;
        }

        text.resize(kept);

        if (!text.empty() || !starts_in_code || continued) {
            output += text;
            output += '\n';
        }

        continued = continues;
        joined_word_start = word_start;
    }

    if (stack.size() != 1 || continued) {
        refuse("QNX_GUEST_SHELL_UNTERMINATED", number,
               "ends the script inside a quote, a substitution or a continued line");
    }

    return output;
}
} // namespace voyemu
