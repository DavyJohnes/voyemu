#pragma once
// Caller supplies windows_startup.hpp and model.hpp. Metadata never grants readiness.
namespace voyemu {
inline Json windows_startup_frame_metadata(std::string_view input, std::string_view frame,
                                           std::string_view nonce, std::string_view pinned_boot,
                                           std::string_view alternate_nonce = {}) {
    Json out = {{"schema", 1},
                {"readiness_granted", false},
                {"declared_body_length_present", false},
                {"parse_stage", "arguments_invalid"},
                {"begin_boot_present", false},
                {"begin_boot_valid", false},
                {"begin_nonce_present", false},
                {"begin_nonce_valid", false},
                {"body_marker_present", false},
                {"body_bytes", 0},
                {"exit_marker_present", false},
                {"exit_status_valid", false},
                {"trailing_nonce_present", false},
                {"trailing_nonce_valid", false},
                {"trailing_boot_present", false},
                {"trailing_boot_valid", false},
                {"trailing_utc_present", false},
                {"trailing_utc_valid", false},
                {"shell_error_category", "none"}};

    if (!windows_startup_token(frame) || !windows_startup_token(nonce) ||
        (!pinned_boot.empty() && !android_boot_id_valid(pinned_boot)) ||
        (!alternate_nonce.empty() && !windows_startup_token(alternate_nonce))) {
        return out;
    }

    const std::string marker = "VOYEMU_STARTUP_" + std::string(frame);
    out["expected_marker_sha256"] = sha256_bytes(marker);
    out["expected_frame_sha256"] = sha256_bytes(frame);

    if (input.size() > 512 * 1024) {
        out["parse_stage"] = "oversized";

        return out;
    }

    std::string text;
    std::vector<std::size_t> offsets;
    text.reserve(input.size());
    offsets.reserve(input.size() + 1);

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\r' && i + 1 < input.size() && input[i + 1] == '\n') {
            continue;
        }

        offsets.push_back(i > 0 && input[i] == '\n' && input[i - 1] == '\r' ? i - 1 : i);
        text += input[i];
    }

    offsets.push_back(input.size());
    std::size_t cursor = 0;
    std::string before, first, last, after, utc, code;
    auto field = [&](std::string_view name, std::string_view key, std::string &value) {
        const std::string prefix = marker + ":" + std::string(name) + ":";

        if (text.compare(cursor, prefix.size(), prefix) != 0) {
            out["parse_stage"] = "missing_or_reordered_" + std::string(name);

            return false;
        }

        out[std::string(key) + "_present"] = true;
        cursor += prefix.size();
        const auto end = text.find('\n', cursor);

        if (end == std::string::npos) {
            out["parse_stage"] = "incomplete_" + std::string(name);

            return false;
        }

        value = text.substr(cursor, end - cursor);
        cursor = end + 1;

        return true;
    };

    if (!field("b1", "begin_boot", before)) {
        return out;
    }

    out["begin_boot_valid"] = android_boot_id_valid(before) &&
                              (pinned_boot.empty() || before == pinned_boot);

    if (!field("n1", "begin_nonce", first)) {
        return out;
    }

    out["begin_nonce_valid"] = first == nonce ||
                               (!alternate_nonce.empty() && first == alternate_nonce);

    if (!out["begin_boot_valid"].get<bool>() || !out["begin_nonce_valid"].get<bool>()) {
        out["parse_stage"] = "pre_fence_invalid";

        return out;
    }

    const auto body_marker = marker + ":body\n";

    if (text.compare(cursor, body_marker.size(), body_marker) != 0) {
        out["parse_stage"] = "missing_body";

        return out;
    }

    out["body_marker_present"] = true;
    cursor += body_marker.size();
    const auto body_start = cursor;
    const auto ending = "\n" + marker + ":exit:";
    const auto end = text.find(ending, cursor);
    auto shell_category = [&](std::string_view body) {
        if (body.starts_with("java.lang.SecurityException") ||
            (body.starts_with("Error while accessing provider:") &&
             body.find("java.lang.SecurityException") != std::string_view::npos)) {
            return "security_exception";
        }

        if (body.starts_with("/system/bin/sh: content: not found") &&
            (body.size() == std::string_view("/system/bin/sh: content: not found").size() ||
             body[std::string_view("/system/bin/sh: content: not found").size()] == '\n')) {
            return "content_not_found";
        }

        if (body.starts_with("Error while accessing provider:")) {
            return "content_provider_access";
        }

        if (body.starts_with("/system/bin/sh: Permission denied") ||
            body.starts_with("Error: Permission denial")) {
            return "permission_denied";
        }

        if (body == "/system/bin/sh: syntax error: unexpected '('\n" ||
            body == "/system/bin/sh: syntax error: unexpected '('") {
            return "shell_syntax_error";
        }

        if (body == "/system/bin/sh: syntax error: unexpected end of file\n" ||
            body == "/system/bin/sh: syntax error: unexpected end of file") {
            return "shell_unexpected_eof";
        }

        return body.empty() ? "none" : "unclassified";
    };

    if (end == std::string::npos) {
        out["body_bytes"] = input.size() - offsets[body_start];
        out["shell_error_category"] = shell_category(std::string_view(text).substr(body_start));
        out["parse_stage"] = "body_without_exit";

        return out;
    }

    out["exit_marker_present"] = true;
    // The protocol inserts one newline before exit; CRLF is one separator.
    const auto raw_end = offsets[end];
    out["body_bytes"] = raw_end - offsets[body_start];
    const auto body = std::string_view(text).substr(body_start, end - body_start);
    out["shell_error_category"] = shell_category(body);

    if (text.find(ending, end + ending.size()) != std::string::npos) {
        out["parse_stage"] = "duplicate_exit";

        return out;
    }

    cursor = end + 1;

    if (!field("exit", "exit_marker", code)) {
        return out;
    }

    int status = -1;
    const auto parsed = std::from_chars(code.data(), code.data() + code.size(), status);
    const bool valid_status = parsed.ec == std::errc{} && parsed.ptr == code.data() + code.size() &&
                              status >= 0 && status <= 255;
    out["exit_status_valid"] = valid_status;

    if (valid_status) {
        out["exit_status"] = status;
    }

    if (!field("n2", "trailing_nonce", last)) {
        return out;
    }

    out["trailing_nonce_valid"] = last == first;

    if (!field("b2", "trailing_boot", after)) {
        return out;
    }

    out["trailing_boot_valid"] = android_boot_id_valid(after) && after == before;

    if (!field("utc", "trailing_utc", utc)) {
        return out;
    }

    const auto epoch = android_epoch_seconds(utc);
    out["trailing_utc_valid"] = epoch && *epoch >= 0;

    if (cursor != text.size() || body.find(marker) != std::string_view::npos) {
        out["parse_stage"] = "extra_frame_data";

        return out;
    }

    if (!valid_status) {
        out["parse_stage"] = "invalid_exit_status";

        return out;
    }

    if (!out["trailing_nonce_valid"].get<bool>() || !out["trailing_boot_valid"].get<bool>()) {
        out["parse_stage"] = "post_fence_invalid";

        return out;
    }

    out["parse_stage"] = out["trailing_utc_valid"].get<bool>() ? "complete" : "invalid_utc";

    return out;
}
} // namespace voyemu
