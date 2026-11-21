#include "api.hpp"
#include "guest_android_fake.hpp"
#include "os/platform.hpp"
#include "profile_runtime.hpp"
#include "qnx_cluster_ui_wire.hpp"
#include "qnx_runtime_layout.hpp"
#include "session.hpp"
#include "session_ui_response.hpp"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>

extern "C" unsigned qgfx_test_scalar_record(unsigned observation, unsigned char *out);
extern "C" void qgfx_test_scalar_assertions(void);

using namespace voyemu;
namespace {
// Serialize actual records independently of the decoder, using fixed
// little-endian fields.
void append_number(std::vector<unsigned char> &out, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
        out.push_back(static_cast<unsigned char>(value >> (8 * i)));
    }
}

void add_record(QnxKanziUiSample &sample, std::uint64_t parent, unsigned flags,
                std::string_view name, unsigned status, std::string_view text = {}) {
    auto &stream = sample.stream;

    append_number(stream, 24 + 12 * QGFX_KANZI_UI_PROPERTIES + name.size() + text.size(), 4);
    append_number(stream, flags, 4);
    append_number(stream, parent, 8);
    append_number(stream,
                  flags & QGFX_KANZI_UI_NAME_UNAVAILABLE ? QGFX_KANZI_UI_UNAVAILABLE
                                                         : QGFX_KANZI_UI_VALUE,
                  4);
    append_number(stream, name.size(), 4);
    stream.insert(stream.end(), name.begin(), name.end());
    append_number(stream, status, 4);
    append_number(stream, QGFX_KANZI_UI_KIND_STRING, 4);
    append_number(stream, text.size(), 4);
    stream.insert(stream.end(), text.begin(), text.end());

    for (unsigned property = QGFX_KANZI_UI_PROPERTY_VISIBLE; property < QGFX_KANZI_UI_PROPERTIES;
         ++property) {
        append_number(stream, QGFX_KANZI_UI_UNAVAILABLE, 4);
        append_number(stream,
                      property <= QGFX_KANZI_UI_PROPERTY_ENABLED ? QGFX_KANZI_UI_KIND_BOOL
                                                                 : QGFX_KANZI_UI_KIND_FLOAT,
                      4);
        append_number(stream, 0, 4);
    }

    ++sample.page.count;
    sample.page.used = stream.size();
}

QnxKanziUiSample fixture_sample() {
    QnxKanziUiSample sample;
    auto &page = sample.page;

    page.magic = QGFX_KANZI_UI_MAGIC;
    page.schema = 1;
    page.header_bytes = 136;
    page.sequence = 2;
    page.guest_pid = 11;
    page.incarnation = 23;
    page.guest_monotonic_ns = 42;
    page.capture_finished_ns = 43;
    page.heartbeat_ns = 44;
    page.presented_ns = 42;
    page.window_width = 1920;
    page.window_height = 720;
    add_record(sample, QGFX_KANZI_UI_ROOT, 3, "Root", QGFX_KANZI_UI_UNSET);
    add_record(sample, 0, 2, "Icon", QGFX_KANZI_UI_VALUE, "120");

    return sample;
}

bool malformed(const QnxKanziUiSample &sample) {
    try {
        (void)decode_qnx_kanzi_ui(sample);
    } catch (const std::runtime_error &error) {
        return std::string_view(error.what()).starts_with("CLUSTER_UI_PROTOCOL:");
    }

    return false;
}

// Real C encoder records through the host decoder.
void check_scalar_properties() {
    qgfx_test_scalar_assertions();
    const auto sample = [](unsigned observation) {
        QnxKanziUiSample out;
        std::array<unsigned char, 2048> bytes{};
        const auto size = qgfx_test_scalar_record(observation, bytes.data());

        out.stream.assign(bytes.begin(), bytes.begin() + size);
        out.page.count = 1;
        out.page.used = size;

        return out;
    };
    const auto first = sample(0);
    const auto decoded = decode_qnx_kanzi_ui(first);
    const auto &properties = decoded[0].at("properties");
    assert(properties.size() == 6 && properties.at("text").at("value") == "node");
    assert(properties.at("visible") == Json({{"status", "ok"}, {"value", false}}) &&
           properties.at("enabled").at("value") == false &&
           properties.at("opacity") == Json({{"status", "ok"}, {"value", 0.0}}) &&
           properties.at("actual_width").at("value") == 0.0 &&
           properties.at("actual_height").at("value") == 0.0);
    const auto changed = decode_qnx_kanzi_ui(sample(1))[0];
    // A resolved Visible value differs from the effective flag, which follows the parents.
    assert(changed.at("properties").at("opacity").at("value") == 0.5 &&
           changed.at("properties").at("visible").at("value") == true &&
           changed.at("visible") == false);
    static constexpr std::array reasons{"value_unavailable", "property_type_unavailable",
                                        "non_finite_value", "non_finite_value", "non_finite_value"};

    for (unsigned observation = 2; observation < 7; ++observation) {
        const auto opacity =
                decode_qnx_kanzi_ui(sample(observation))[0].at("properties").at("opacity");
        assert(opacity == Json({{"status", "unavailable"}, {"reason", reasons[observation - 2]}}));
    }

    const auto text_unset = decode_qnx_kanzi_ui(sample(2))[0].at("properties").at("text");
    assert(text_unset == Json({{"status", "unset"}}));
    const auto put = [](std::vector<unsigned char> &bytes, std::size_t at, unsigned value) {
        for (unsigned n = 0; n < 4; ++n) {
            bytes[at + n] = static_cast<unsigned char>(value >> (8 * n));
        }
    };
    // Record header and name "node", then text, visible and enabled records with their values.
    const std::size_t visible = 24 + 4 + 12 + 4, opacity = visible + 2 * (12 + 1);
    const auto rejected = [&](std::size_t at, unsigned value) {
        auto bad = first;
        put(bad.stream, at, value);

        return malformed(bad);
    };
    assert(rejected(opacity + 4, 99));                      // unknown kind
    assert(rejected(opacity + 4, QGFX_KANZI_UI_KIND_BOOL)); // kind out of order
    assert(rejected(opacity + 8, 3));                       // short float
    assert(rejected(opacity + 12, 0x7f800000));             // infinity reported as a value
    assert(rejected(visible + 12, 2));                      // noncanonical bool
    // The same record without its value, carrying another status.
    const auto absent = [&](std::size_t at, std::size_t bytes, unsigned status) {
        auto out = first;
        put(out.stream, at, status);
        put(out.stream, at + 8, 0);
        out.stream.erase(out.stream.begin() + static_cast<std::ptrdiff_t>(at + 12),
                         out.stream.begin() + static_cast<std::ptrdiff_t>(at + 12 + bytes));
        put(out.stream, 0, static_cast<unsigned>(out.stream.size()));
        out.page.used = out.stream.size();

        return out;
    };

    for (const unsigned status :
         {QGFX_KANZI_UI_UNSET, QGFX_KANZI_UI_UNAVAILABLE, QGFX_KANZI_UI_NON_FINITE}) {
        const auto value =
                decode_qnx_kanzi_ui(absent(opacity, 4, status))[0].at("properties").at("opacity");
        assert(value.at("status") == "unavailable" && !value.contains("value"));
    }

    assert(malformed(absent(opacity, 4, QGFX_KANZI_UI_TOO_LONG)));
    assert(malformed(absent(opacity, 4, QGFX_KANZI_UI_NON_FINITE + 1)));
    assert(malformed(absent(visible, 1, QGFX_KANZI_UI_NON_FINITE)));
    assert(decode_qnx_kanzi_ui(absent(visible, 1, QGFX_KANZI_UI_UNSET))[0]
                   .at("properties")
                   .at("visible") ==
           Json({{"status", "unavailable"}, {"reason", "value_unavailable"}}));
    assert(qnx_kanzi_ui_adapter("sa8155-qinggan09") &&
           qnx_kanzi_ui_adapter("sa8155-qinggan11-611") &&
           qnx_kanzi_ui_adapter("sa8155-qinggan11-731") &&
           !qnx_kanzi_ui_adapter("sa8155-passion-21480") && !qnx_kanzi_ui_adapter(""));
}

void check_decode() {
    const auto nodes = decode_qnx_kanzi_ui(fixture_sample());

    assert(nodes.size() == 2 && nodes[0].at("name") == "Root" && nodes[0].at("parent").is_null());
    assert(nodes[1].at("properties").at("text").at("value") == "120");
    auto bounded = nodes;
    const auto first_bytes = nodes[0].dump().size() + 2;
    assert(bound_qnx_kanzi_ui_nodes(bounded, first_bytes) && bounded.size() == 1 &&
           bounded[0].at("parent").is_null() && bounded.dump().size() <= first_bytes);
    bounded = nodes;
    assert(!bound_qnx_kanzi_ui_nodes(bounded, nodes.dump().size()));
    Json envelope = {{"complete", true},
                     {"partial_reason", "complete"},
                     {"request_id", "wire"},
                     {"nodes", nodes}};
    auto metadata = envelope;
    metadata["nodes"] = Json::array();
    metadata["complete"] = false;
    metadata["partial_reason"] = "host_response_byte_budget";
    const auto budget = metadata.dump().size() + 1 + first_bytes - 2;
    bound_qnx_kanzi_ui_reply(envelope, budget);
    assert(envelope.at("nodes").size() == 1 && envelope.at("complete") == false &&
           envelope.at("partial_reason") == "host_response_byte_budget" &&
           envelope.dump().size() + 1 <= budget);
    const auto future = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    assert(encode_qnx_kanzi_ui_json(envelope, envelope.dump().size() + 1, future) ==
           envelope.dump() + "\n");

    for (const bool elapsed : {false, true}) {
        try {
            (void)encode_qnx_kanzi_ui_json(envelope, elapsed ? 1024 : 8,
                                           elapsed ? std::chrono::steady_clock::time_point{}
                                                   : future);
            assert(false);
        } catch (const std::runtime_error &error) {
            assert(std::string_view(error.what()).starts_with("CLUSTER_UI_RESOURCE_LIMIT:"));
        }
    }

    auto expired = nodes;
    try {
        (void)bound_qnx_kanzi_ui_nodes(expired, 1024, std::chrono::steady_clock::time_point{});
        assert(false);
    } catch (const std::runtime_error &error) {
        assert(std::string_view(error.what()).starts_with("CLUSTER_UI_RESOURCE_LIMIT:"));
    }

    auto statuses = fixture_sample();
    add_record(statuses, 0, QGFX_KANZI_UI_NAME_UNAVAILABLE, "", QGFX_KANZI_UI_TOO_LONG);
    add_record(statuses, 0, 0, "", QGFX_KANZI_UI_UNAVAILABLE);
    add_record(statuses, 0, 0, "\xff", QGFX_KANZI_UI_VALUE, "\xfe");
    const auto decoded = decode_qnx_kanzi_ui(statuses);
    assert(decoded[2].at("name").is_null());
    assert(decoded[2].at("properties").at("text").at("reason") == "too_long");
    assert(decoded[3].at("properties").at("text").at("reason") == "property_type_unavailable");
    assert(decoded[4].at("name").is_null() &&
           decoded[4].at("properties").at("text").at("reason") == "not_utf8");

    for (const unsigned count : {2049u, 4097u, 70000u}) {
        auto large = fixture_sample();

        while (large.page.count < count) {
            add_record(large, large.page.count - 1, 3, "Node", QGFX_KANZI_UI_UNSET);
        }

        const auto result = decode_qnx_kanzi_ui(
                large, {256ULL * 1024 * 1024,
                        std::chrono::steady_clock::now() + std::chrono::seconds(20)});
        assert(result.size() == count && result.back().at("parent") == count - 2);
    }

    auto bad = fixture_sample();
    bad.stream.pop_back();
    bad.page.used = bad.stream.size();
    assert(malformed(bad));
    bad = fixture_sample();
    bad.stream.push_back(0);
    bad.page.used = bad.stream.size();
    assert(malformed(bad));
    bad = fixture_sample();
    bad.stream[100 + 8] = 1; // second record self-parent
    assert(malformed(bad));
    bad = fixture_sample();
    bad.stream[100 + 24 + 4] = 9; // unknown text status
    assert(malformed(bad));
    bad = fixture_sample();
    bad.page.reason = 100;
    assert(malformed(bad));
    bad = fixture_sample();
    bad.page.count = ~0ULL;
    assert(malformed(bad));
}

void check_wire() {
    constexpr auto producer = qnx_kanzi_ram_base + 4096, address = producer + 4096;
    auto sample = fixture_sample();

    while (sample.page.count < 10000) {
        add_record(sample, 0, 1, "Filler", QGFX_KANZI_UI_VALUE, std::string(100, 'x'));
    }

    auto &header = sample.page;
    header.pages = (header.used - 1) / QGFX_KANZI_UI_PAYLOAD + 1;
    std::vector<QgfxKanziUiData> pages(static_cast<std::size_t>(header.pages));
    const auto physical = [&](std::size_t i) {
        // Sparse, descending physical pages; do not assume a contiguous allocation.
        return qnx_kanzi_ram_base + 0x10000000ULL - i * 8192;
    };
    header.first_page = physical(0);

    for (std::size_t i = 0, at = 0; i < pages.size(); ++i) {
        auto &p = pages[i];
        p.sequence = header.sequence;
        p.ordinal = i;
        p.used = static_cast<unsigned>(
                std::min<std::size_t>(QGFX_KANZI_UI_PAYLOAD, sample.stream.size() - at));
        std::memcpy(p.payload, sample.stream.data() + at, p.used);
        at += p.used;
        p.next = i + 1 == pages.size() ? 0 : physical(i + 1);
    }

    assert(pages.size() > 31);
    std::array<std::byte, QGFX_KANZI_UI_BYTES> channel{};
    const std::array<std::uint32_t, 3> channel_header{0x58464751, 3, 2};
    std::memcpy(channel.data(), channel_header.data(), sizeof(channel_header));
    std::array<std::uint64_t, 7> descriptor{address, 0, 0, 0, 0, 11, 23};
    bool mutate_sequence = false, replace_owner = false;
    std::uint64_t later_present = 0;
    unsigned reads = 0;
    const auto read = [&](std::uint64_t location, std::span<std::byte> output) {
        ++reads;

        if (location >= producer && location + output.size() <= address) {
            std::memcpy(channel.data() + QGFX_KANZI_UI_DESCRIPTOR_OFFSET, descriptor.data(),
                        sizeof(descriptor));
            std::memcpy(output.data(), channel.data() + location - producer, output.size());

            return true;
        }

        for (std::size_t i = 0; i < pages.size(); ++i) {
            if (location == physical(i) && output.size() == sizeof(QgfxKanziUiData)) {
                std::memcpy(output.data(), &pages[i], output.size());

                if (mutate_sequence) {
                    header.sequence += 2;
                }

                if (replace_owner) {
                    descriptor[6] = 24;
                }

                return true;
            }
        }

        if (location < address || location + output.size() > address + sizeof(header)) {
            return false;
        }

        if (later_present && location == address + offsetof(QgfxKanziUiPage, presented_ns)) {
            header.presented_ns = later_present;
        }

        const auto bytes = std::as_bytes(std::span(&header, 1));
        std::memcpy(output.data(), bytes.data() + location - address, output.size());

        return true;
    };
    const auto capture = [&] {
        return read_qnx_kanzi_ui(read, producer, 11, 23);
    };
    const auto initial = capture();
    assert(initial && initial->stream == sample.stream);
    assert(decode_qnx_kanzi_ui(*initial).size() == 10000);
    later_present = 99;
    assert(capture()->page.presented_ns == 99 && header.guest_monotonic_ns == 42);
    later_present = 0;
    const auto malformed_chain = [&] {
        try {
            (void)capture();
        } catch (const std::runtime_error &e) {
            return std::string_view(e.what()).starts_with("CLUSTER_UI_PROTOCOL:");
        }

        return false;
    };
    const auto original = pages[0];

    for (const std::uint64_t next : std::initializer_list<std::uint64_t>{
                 physical(0), producer, address, qnx_kanzi_ram_end, physical(1) + 1, 0}) {
        pages[0].next = next;
        assert(malformed_chain());
    }

    pages[0] = original;
    pages.back().next = physical(0);
    assert(malformed_chain());
    pages.back().next = 0;
    pages[0].used = QGFX_KANZI_UI_PAYLOAD + 1;
    assert(malformed_chain());
    pages[0] = original;
    pages[0].sequence = 4;
    assert(malformed_chain());
    pages[0] = original;
    const auto saved_used = header.used;
    header.used = ~0ULL;
    assert(malformed_chain());
    header.used = saved_used;
    ++header.pages;
    assert(malformed_chain());
    --header.pages;
    pages[0].ordinal = 100;
    assert(malformed_chain());
    pages[0] = original;
    // Malformed bytes observed during a concurrent rewrite are a retry, not a
    // protocol failure.
    pages[0].next = producer;
    mutate_sequence = true;
    assert(!capture());
    mutate_sequence = false;
    header.sequence = 2;
    pages[0] = original;
    replace_owner = true;
    assert(!capture());
    replace_owner = false;
    descriptor[6] = 23;
    header.heartbeat_ns = 0;
    assert(!capture());
    header.heartbeat_ns = 44;
    header.sequence = 3;
    assert(!capture());
    header.sequence = 2;
    reads = 0;
    try {
        (void)read_qnx_kanzi_ui(read, producer, 11, 23, {4096});
        assert(false);
    } catch (const std::runtime_error &e) {
        assert(std::string_view(e.what()).starts_with("CLUSTER_UI_RESOURCE_LIMIT:") && reads < 12);
    }

    try {
        (void)read_qnx_kanzi_ui(read, producer, 11, 23,
                                {16ULL * 1024 * 1024, std::chrono::steady_clock::time_point{}});
        assert(false);
    } catch (const std::runtime_error &e) {
        assert(std::string_view(e.what()).starts_with("CLUSTER_UI_RESOURCE_LIMIT:"));
    }
}

void check_public_ui_budget() {
    using api::bound_session_ui_response;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto encoded = [&](const Json &value) {
        return count_qnx_kanzi_ui_json(value, 1ULL << 30, deadline);
    };
    Json nodes = Json::array();

    for (unsigned i = 0; i < 3; ++i) {
        nodes.push_back({{"id", std::to_string(i)},
                         {"parent", i ? Json(std::to_string(i - 1)) : Json()},
                         {"window_id", 8},
                         {"text", std::string(256, 'x')}});
    }

    Json screen = {{"screen", "cluster"},
                   {"status", "ok"},
                   {"complete", true},
                   {"windows",
                    Json::array({{{"id", 8},
                                  {"root_id", "0"},
                                  {"node_count", 3},
                                  {"read_complete", true},
                                  {"verified_binding", {{"root_id", "0"}, {"node_count", 3}}}}})},
                   {"nodes", nodes}};
    Json envelope = {{"schema", 1},
                     {"request_id", "public-budget"},
                     {"operation_id", "operation"},
                     {"ok", true},
                     {"elapsed_ms", 1.25},
                     {"result",
                      {{"format", "voyemu.session.ui.v1"},
                       {"complete", true},
                       {"status", "ready"},
                       {"cluster", screen},
                       {"multimedia", Json::array({screen, screen})}}}};
    auto small = envelope;
    bound_session_ui_response(small, encoded(envelope), deadline);
    assert(small == envelope);
    auto metadata = envelope;
    auto mark = [](Json &slot) {
        slot["nodes"] = Json::array();
        slot["complete"] = false;
        slot["status"] = "partial";
        slot["partial_reason"] = "host_response_byte_budget";
        slot["windows"][0]["read_complete"] = false;
    };
    mark(metadata["result"]["cluster"]);
    mark(metadata["result"]["multimedia"][0]);
    mark(metadata["result"]["multimedia"][1]);
    metadata["result"]["complete"] = false;
    metadata["result"]["status"] = "partial";
    metadata["result"]["partial_reason"] = "host_response_byte_budget";
    const auto budget = encoded(metadata) + 3 * (encoded(nodes[0]) - 1) + encoded(nodes[1]);
    auto aggregate = envelope;
    bound_session_ui_response(aggregate, budget, deadline);
    assert(encoded(aggregate) <= budget && aggregate["result"]["complete"] == false);
    std::size_t retained = 0;

    for (const auto *slot : {&aggregate["result"]["cluster"], &aggregate["result"]["multimedia"][0],
                             &aggregate["result"]["multimedia"][1]}) {
        assert((*slot)["nodes"].size() >= 1 && (*slot)["nodes"].size() < 3);
        retained += (*slot)["nodes"].size();
        const auto &window = (*slot)["windows"][0];
        assert(window["node_count"] == (*slot)["nodes"].size() && window["root_id"] == "0" &&
               window["read_complete"] == false && !window.contains("verified_binding"));
        assert((*slot)["partial_reason"] == "host_response_byte_budget");
    }

    assert(retained == 4);
    auto numeric = envelope;

    for (auto *slot : {&numeric["result"]["cluster"], &numeric["result"]["multimedia"][0],
                       &numeric["result"]["multimedia"][1]}) {
        for (std::size_t i = 0; i < 3; ++i) {
            (*slot)["nodes"][i]["id"] = i;

            if (i) {
                (*slot)["nodes"][i]["parent"] = i - 1;
            }
        }
    }

    assert(encoded(numeric) < encoded(envelope));
    auto expansion = envelope;
    bound_session_ui_response(expansion, encoded(numeric), deadline);
    assert(encoded(expansion) <= encoded(numeric) && expansion["result"]["complete"] == false);
    auto child_first = envelope;
    child_first["result"]["cluster"]["nodes"] = Json::array({nodes[1], nodes[0], nodes[2]});
    bound_session_ui_response(child_first, budget, deadline);
    const auto &prefix = child_first["result"]["cluster"]["nodes"];

    for (const auto &node : prefix) {
        if (!node.at("parent").is_null()) {
            assert(std::any_of(prefix.begin(), prefix.end(), [&](const Json &parent) {
                return parent.at("id") == node.at("parent");
            }));
        }
    }

    auto root_last = envelope;
    root_last["result"]["multimedia"] = Json::array({nullptr, nullptr});
    auto root_node = nodes[0], big_child = nodes[1], small_child = nodes[2];
    root_node["text"] = "root";
    big_child["text"] = std::string(8192, 'x');
    small_child["parent"] = "0";
    small_child["text"] = "small";
    root_last["result"]["cluster"]["nodes"] = Json::array({big_child, small_child, root_node});
    auto root_metadata = root_last;
    mark(root_metadata["result"]["cluster"]);
    root_metadata["result"]["complete"] = false;
    root_metadata["result"]["status"] = "partial";
    root_metadata["result"]["partial_reason"] = "host_response_byte_budget";
    const auto root_budget = encoded(root_metadata) + encoded(root_node) + encoded(small_child) - 1;
    bound_session_ui_response(root_last, root_budget, deadline);
    assert(root_last["result"]["cluster"]["nodes"] == Json::array({small_child, root_node}) &&
           encoded(root_last) <= root_budget);
    auto two_windows = envelope;
    two_windows["result"]["multimedia"] = Json::array({nullptr, nullptr});
    auto second_root = root_node;
    second_root["id"] = "second";
    second_root["window_id"] = 9;
    two_windows["result"]["cluster"]["nodes"] = Json::array({root_node, second_root, big_child});
    two_windows["result"]["cluster"]["windows"][0]["node_count"] = 2;
    auto second_window = screen["windows"][0];
    second_window["id"] = 9;
    second_window["root_id"] = "second";
    second_window["node_count"] = 1;
    second_window["verified_binding"] = {{"root_id", "second"}, {"node_count", 1}};
    two_windows["result"]["cluster"]["windows"].push_back(second_window);
    auto two_metadata = two_windows;
    mark(two_metadata["result"]["cluster"]);
    two_metadata["result"]["cluster"]["windows"][1]["read_complete"] = false;
    two_metadata["result"]["complete"] = false;
    two_metadata["result"]["status"] = "partial";
    two_metadata["result"]["partial_reason"] = "host_response_byte_budget";
    const auto two_budget = encoded(two_metadata) + encoded(root_node) + encoded(second_root) - 1;
    bound_session_ui_response(two_windows, two_budget, deadline);
    assert(two_windows["result"]["cluster"]["windows"][1] == second_window);
    assert(two_windows["result"]["cluster"]["windows"][0]["node_count"] == 1 &&
           !two_windows["result"]["cluster"]["windows"][0].contains("verified_binding"));
    auto dangling = envelope;
    dangling["result"]["cluster"]["nodes"][0]["parent"] = "missing";
    try {
        bound_session_ui_response(dangling, budget, deadline);
        assert(false);
    } catch (const std::runtime_error &error) {
        assert(std::string_view(error.what()).starts_with("TREE_PROTOCOL: dangling"));
    }

    auto oversized = envelope;
    oversized["result"]["cluster"]["windows"][0]["label"] = std::string(4096, '\n') + "😀";
    try {
        bound_session_ui_response(oversized, budget, deadline);
        assert(false);
    } catch (const std::runtime_error &error) {
        assert(std::string_view(error.what()).starts_with("CLUSTER_UI_RESOURCE_LIMIT:"));
    }

    auto escaped = envelope;
    escaped["result"]["cluster"]["nodes"][0]["text"] = std::string(1024, '\n') + "😀";
    bound_session_ui_response(escaped, budget, deadline);
    assert(encoded(escaped) <= budget && escaped["result"]["cluster"]["nodes"].empty());
    auto elapsed = envelope;
    try {
        bound_session_ui_response(elapsed, budget, std::chrono::steady_clock::time_point{});
        assert(false);
    } catch (const std::runtime_error &error) {
        assert(std::string_view(error.what()).starts_with("CLUSTER_UI_RESOURCE_LIMIT:"));
    }

    Json terminal_slot = {{"screen", "cluster"},      {"status", "unavailable"},
                          {"complete", false},        {"nodes", Json::array()},
                          {"windows", Json::array()}, {"error_code", "CLUSTER_UI_NOT_READY"}};
    auto terminal = envelope;
    terminal["result"]["complete"] = false;
    terminal["result"]["status"] = "unavailable";
    terminal["result"]["cluster"] = terminal_slot;
    terminal["result"]["multimedia"] = Json::array({nullptr, nullptr});
    const auto expired = std::chrono::steady_clock::time_point{};
    const auto original_terminal = terminal;
    bound_session_ui_response(terminal, 100 * 1024 * 1024, expired);
    assert(terminal == original_terminal);
    auto all_terminal = terminal;
    all_terminal["result"]["multimedia"] = Json::array({terminal_slot, terminal_slot});
    all_terminal["result"]["multimedia"][0]["screen"] = "primary";
    all_terminal["result"]["multimedia"][1]["screen"] = "secondary";
    all_terminal["result"]["multimedia"][1]["status"] = "unsupported";
    const auto original_all = all_terminal;
    bound_session_ui_response(all_terminal, 100 * 1024 * 1024, expired);
    assert(all_terminal == original_all);
    auto terminal_mixed = terminal;
    terminal_mixed["result"]["status"] = "partial";
    terminal_mixed["result"]["multimedia"][0] = screen;
    const auto original_mixed = terminal_mixed;
    bound_session_ui_response(terminal_mixed, 100 * 1024 * 1024, expired);
    assert(terminal_mixed == original_mixed);

    for (const bool metadata_only : {false, true}) {
        auto large_terminal = metadata_only ? terminal : terminal_mixed;

        if (metadata_only) {
            large_terminal["result"]["metadata"] = std::string(128 * 1024, 'x');
        } else {
            large_terminal["result"]["multimedia"][0]["nodes"][0]["text"] = std::string(128 * 1024,
                                                                                        'x');
        }

        try {
            bound_session_ui_response(large_terminal, 100 * 1024 * 1024, expired);
            assert(false);
        } catch (const std::runtime_error &error) {
            assert(std::string_view(error.what()) ==
                   "CLUSTER_UI_RESOURCE_LIMIT: response encoding byte budget");
        }
    }

    auto invalid_terminal = terminal;
    invalid_terminal["result"]["cluster"]["nodes"] = nodes;
    try {
        bound_session_ui_response(invalid_terminal, 100 * 1024 * 1024, expired);
        assert(false);
    } catch (const std::runtime_error &error) {
        assert(std::string_view(error.what()) ==
               "CLUSTER_UI_RESOURCE_LIMIT: response encoding deadline");
    }

    auto expired_ready = envelope;
    expired_ready["result"]["cluster"]["nodes"][0]["text"] = std::string(128 * 1024, 'x');
    try {
        bound_session_ui_response(expired_ready, 100 * 1024 * 1024, expired);
        assert(false);
    } catch (const std::runtime_error &error) {
        assert(std::string_view(error.what()) ==
               "CLUSTER_UI_RESOURCE_LIMIT: response encoding deadline");
    }
}

void check_cluster_transport_deadline() {
    // The deadline is exercised over a Unix control socket.
    if (!voyemu::os::host_windows) {
        const auto endpoint =
                private_control_endpoint("cluster-deadline-" + random_token().substr(0, 8));

        for (const bool elapsed : {false, true}) {
            std::atomic<bool> stopped = false;
            ControlServer server(endpoint, [&](const Json &) {
                stopped = true;

                if (elapsed) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                }

                return Json{{"ok", true}, {"payload", "small"}};
            });
            std::thread serving([&] {
                server.serve(stopped);
            });
            bool rejected = false;
            try {
                const auto reply = voyemu::request(
                        endpoint, {{"op", "test_cluster_ui"}, {"timeout_ms", elapsed ? 10 : 1000}},
                        1000);
                assert(reply.at("ok") == true);
            } catch (const std::runtime_error &) {
                rejected = true;
            }

            stopped = true;
            serving.join();
            assert(rejected == elapsed);
        }
    }
}

void check_system_and_power(const std::filesystem::path &base) {
    const auto root = std::filesystem::absolute(base) / ("voyemu-subsystem-" + random_token());
    const std::string profile = "free-09-chinese-20260106-6.0.3", id = random_token(),
                      owner = random_token();
    const auto directory = root / ".local/machines" / profile / "sessions" / id;

    ensure_private_directory(directory);
    write_private_json(root / ".local/machines" / profile / "active" / (profile + ".json"),
                       {{"schema", 1},
                        {"session_id", id},
                        {"directory", directory.string()},
                        {"endpoint", "owned-subsystem-fixture"}});
    write_private_json(directory / "automation-owner.json",
                       {{"schema", 1}, {"session_id", id}, {"ownership_id", owner}});
    Session session(directory, id);
    std::string scenario = "not-configured";
    unsigned reads = 0, writes = 0;
    bool incomplete_receipt = false;
    api::Transports transports;
    transports.control = [&](const auto &, const Json &op, int timeout) -> Json {
        assert(op.at("session_id") == id);

        if (op.at("op") == "test_media_power") {
            assert(timeout == 90000 && op.at("ownership_id") == owner &&
                   op.at("enabled").is_boolean());
            ++writes;

            if (incomplete_receipt) {
                return Json{{"ok", true}, {"session_id", id}, {"request_id", op.at("request_id")}};
            }

            const bool enabled = op.at("enabled").get<bool>();
            Json result = {{"ok", true},
                           {"session_id", id},
                           {"request_id", op.at("request_id")},
                           {"enabled", enabled},
                           {"changed", true},
                           {"android_generation", 3u},
                           {"android_pid", enabled ? Json(123) : Json()},
                           {"process_alive", enabled},
                           {"verified", true},
                           {"source", "supervisor.android_lifecycle"},
                           {"visible_effect_verified", false},
                           {"qnx_unchanged", true}};

            if (!enabled) {
                result["shutdown"] = {{"wrapper_terminal", true}, {"ports_released", true}};
            }

            return result;
        }

        assert(op.at("op") == "test_cluster_system" && !op.contains("ownership_id") &&
               timeout > 0 && timeout <= 1000);
        ++reads;

        if (scenario == "not-configured") {
            return session.handle(op);
        }

        if (scenario == "old-supervisor") {
            throw std::runtime_error("IPC_OPERATION: unrecognized operation");
        }

        return {{"ok", true},
                {"session_id", id},
                {"status", "sample"},
                {"generation", scenario == "generation" && reads > 1 ? 2u : 1u},
                {"owner", scenario == "owner" && reads > 1 ? 52u : 51u},
                {"guest_pid", 42u},
                {"producer_incarnation", 12u},
                {"sequence", 2u},
                {"guest_monotonic_ns", 25u},
                {"heartbeat_ns", scenario == "stale" ? 25u : 25u + reads},
                {"node_count", 2u},
                {"complete", true}};
    };
    api::Api api(root, "unused", transports);
    auto request = [&](const char *op) {
        return Json{{"schema", 1},
                    {"request_id", random_token()},
                    {"op", op},
                    {"profile", profile},
                    {"session", {{"id", id}}}};
    };

    for (const auto *mode :
         {"not-configured", "old-supervisor", "fresh", "stale", "owner", "generation"}) {
        scenario = mode;
        reads = 0;
        auto query = request("cluster.system");

        if (scenario == "stale") {
            query["timeout_ms"] = 120;
        }

        const auto response = api.call(query);
        assert(response.at("ok") == true);
        const auto &result = response.at("result");
        assert(result.at("complete") == false && result.at("session_id") == id);

        if (scenario == "fresh") {
            assert(reads == 2 && result.at("status") == "partial" &&
                   result.at("qnx_generation") == 1);
            assert(result.at("processes").size() == 1 &&
                   result.at("processes").at(0).at("guest_pid") == 42);
            const auto &gauge = result.at("gauge");
            assert(gauge.at("freshness") == "heartbeat_advanced_during_request" &&
                   gauge.at("node_count") == 2 && gauge.at("scene_complete") == true);
        } else {
            const std::map<std::string, std::string> reasons{
                    {"not-configured", "cluster_not_configured"},
                    {"old-supervisor", "supervisor_unsupported"},
                    {"stale", "gauge_heartbeat_not_advanced"},
                    {"owner", "gauge_producer_changed"},
                    {"generation", "cluster_generation_changed"}};
            assert(result.at("status") == "unavailable" &&
                   result.at("reason") == reasons.at(scenario));
            assert(result.at("gauge").is_null() && result.at("processes").empty());
        }
    }

    auto power = request("multimedia.power");
    power["enabled"] = true;
    assert(api.call(power).at("error").at("code") == "TEST_OWNERSHIP" && writes == 0);

    for (bool enabled : {false, true}) {
        power = request("multimedia.power");
        power["session"]["ownership_id"] = owner;
        power["enabled"] = enabled;
        const auto response = api.call(power);
        assert(response.at("ok") == true);
        const auto &result = response.at("result");
        assert(result.at("process_alive") == enabled &&
               result.at("android_pid") == (enabled ? Json(123) : Json()));
        assert(result.at("qnx_unchanged") == true && result.contains("shutdown") == !enabled);
        assert(api.call(power) == response);
    }

    assert(writes == 2);
    power["request_id"] = random_token();
    power.erase("enabled");
    assert(api.call(power).at("error").at("code") == "TEST_ARGUMENT" && writes == 2);
    incomplete_receipt = true;
    power["request_id"] = random_token();
    power["enabled"] = false;
    const auto failed = api.call(power).at("error");
    assert(failed.at("code") == "ANDROID_POWER_UNKNOWN" && failed.at("outcome") == "unknown" &&
           writes == 3);
    session.stop();
    std::filesystem::remove_all(root);
}
} // namespace

int main(int argc, char **argv) try {
    assert(argc == 2);
    check_public_ui_budget();
    check_scalar_properties();
    check_decode();
    check_wire();
    check_cluster_transport_deadline();
    check_system_and_power(argv[1]);
    const auto root = std::filesystem::absolute(argv[1]) /
                      ("voyemu-cluster-tree-" + random_token());
    const std::string profile = "free-09-chinese-20260106-6.0.3", id = random_token();
    const auto directory = root / ".local/machines" / profile / "sessions" / id;
    ensure_private_directory(directory);
    write_private_json(root / ".local/machines" / profile / "active" / (profile + ".json"),
                       {{"schema", 1},
                        {"session_id", id},
                        {"directory", directory.string()},
                        {"endpoint", "owned-cluster-tree-fixture"}});
    Session session(directory, id);
    const auto powered_off = session.handle({{"op", "test_cluster_ui"}, {"session_id", id}});
    assert(powered_off.at("ok") == true && powered_off.at("session_id") == id &&
           powered_off.at("status") == "off");
    auto nodes = decode_qnx_kanzi_ui(fixture_sample());
    std::uint64_t sequence = 2, owner = 51, generation = 1, heartbeat = 45;
    unsigned controls = 0;
    std::string scenario = "fresh";
    const std::string boot = "aaaaaaaa-1111-2222-3333-444444444444";
    std::condition_variable dispatches;
    std::mutex dispatch_mutex;
    unsigned arrivals = 0, android_reads = 0;
    bool verify_public_deadline = false;
    auto public_received = std::chrono::steady_clock::time_point{};
    std::size_t deadline_checks = 0;
    bool verify_concurrent = false, parallel_supported = true, secondary_available = true,
         observer_epoch_changed = false, stop_during_read = false;
    const auto rendezvous = [&] {
        std::unique_lock lock(dispatch_mutex);
        ++arrivals;
        dispatches.notify_all();
        assert(dispatches.wait_for(lock, std::chrono::seconds(2), [&] {
            return arrivals == 2;
        }));
    };
    api::Transports transports;
    auto guest = std::make_shared<api::ScriptedGuestAndroid>();
    transports.android = guest;
    guest->owner = [&](const Json &request, bool recover, int timeout) {
        assert(request.at("session").at("id") == id && !recover && timeout > 0);

        return Json{{"ok", true},
                    {"session_id", id},
                    {"android_pid", 123},
                    {"connection",
                     {{"boot_id", boot}, {"observer_pid", 777}, {"auth", "unused-observer-auth"}}}};
    };
    transports.control = [&](const auto &, const Json &operation, int timeout) {
        if (verify_public_deadline) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - public_received)
                                         .count();
            assert(timeout <= 1000 - elapsed + 2);
            ++deadline_checks;
        }

        assert(operation.at("session_id") == id && !operation.contains("ownership_id") &&
               timeout > 0);

        if (operation.at("op") == "status") {
            auto state = session.handle(operation);
            state["android_pid"] = 123;

            return state;
        }

        assert(operation.at("op") == "test_cluster_ui");

        if (verify_concurrent && controls == 0) {
            rendezvous();
        }

        ++controls;

        if (controls == 2) {
            if (scenario == "owner") {
                ++owner;
            }

            if (scenario == "generation") {
                ++generation;
            }

            if (scenario == "fresh" || scenario == "partial" || scenario == "no-heartbeat" ||
                scenario == "behind") {
                sequence += 2;
            }

            if (scenario != "no-heartbeat") {
                ++heartbeat;
            }
        }

        if (scenario == "off") {
            return session.handle(operation);
        }

        return Json{{"ok", true},
                    {"session_id", id},
                    {"generation", generation},
                    {"owner", scenario == "invalid-owner" ? 0 : owner},
                    {"sequence", sequence},
                    {"guest_monotonic_ns", 42u},
                    {"heartbeat_ns", heartbeat},
                    {"presented_ns", scenario == "behind" ? 43u : 42u},
                    {"window", {{"width", 1920u}, {"height", scenario == "no-window" ? 0u : 720u}}},
                    {"status", "sample"},
                    {"complete", scenario != "partial"},
                    {"partial_reason", "byte_budget"},
                    {"nodes", nodes}};
    };
    guest->exchange = [&](const Json &, const Json &operation, int timeout) {
        assert(timeout > 0);

        if (operation.at("op") == "hello") {
            return Json{{"ok", true},
                        {"boot_id", boot},
                        {"observer_pid", 777},
                        {"parallel_surfaces", parallel_supported},
                        {"observer_epoch", "fixture-epoch"}};
        }

        assert(operation.at("op") == "ui" && operation.at("parallel_surfaces") == true &&
               operation.at("scope").at("windows") == "all_attached");
        assert(operation.at("surfaces") == Json::array({"media_primary", "media_secondary"}));
        ++android_reads;

        if (verify_concurrent) {
            rendezvous();
        }

        if (stop_during_read) {
            write_private_json(directory / "runtime.json", {{"session_id", id}, {"stopped", true}});
        }

        auto surface = [&](bool primary) {
            const auto display = primary ? 0 : 9;

            return Json{{"surface", primary ? "media_primary" : "media_secondary"},
                        {"display_id", display},
                        {"status", "ok"},
                        {"complete", true},
                        {"scope", "all_attached"},
                        {"clock_domain", "android_uptime_ms"},
                        {"started_uptime_ms", 101u},
                        {"completed_uptime_ms", 110u},
                        {"source", Json{{"basis", "fixture"}}},
                        {"windows", Json::array({{{"display_id", display}}})},
                        {"nodes", Json::array({{{"id", primary ? "primary-root" : "secondary-root"},
                                                {"parent", nullptr},
                                                {"visible", true},
                                                {"enabled", true}}})}};
        };
        auto secondary = surface(false);

        if (!secondary_available) {
            secondary["status"] = "unavailable";
            secondary["complete"] = false;
            secondary["display_id"] = nullptr;
            secondary["reason"] = "display_unavailable";
            secondary["windows"] = Json::array();
            secondary["nodes"] = Json::array();
        }

        return Json{{"schema", 1},
                    {"ok", true},
                    {"parallel_surfaces", true},
                    {"boot_id", boot},
                    {"observer_pid", 777},
                    {"observer_epoch", observer_epoch_changed ? "changed-epoch" : "fixture-epoch"},
                    {"observation_id", "fixture-revision"},
                    {"started_uptime_ms", 100u},
                    {"completed_uptime_ms", 120u},
                    {"surfaces", Json::array({std::move(secondary), surface(true)})}};
    };
    api::Api api(root, "unused", transports);
    const Json request = {
            {"schema", 1},        {"request_id", "cluster-tree"}, {"op", "session.ui"},
            {"profile", profile}, {"session", {{"id", id}}},      {"timeout_ms", 500},
            {"screen", "cluster"}};

    for (const auto timeout : {Json(0), Json(-1), Json(~0ULL), Json(1.5), Json("500")}) {
        auto invalid = request;
        invalid["timeout_ms"] = timeout;
        assert(api.call(invalid).at("ok") == false);
        const auto internal = session.handle(
                {{"op", "test_cluster_ui"}, {"session_id", id}, {"timeout_ms", timeout}});
        assert(internal.at("ok") == false &&
               internal.at("error").get<std::string>().starts_with("TEST_TIMEOUT:"));
    }

    auto fresh = api.call(request);
    assert(fresh.at("ok") == true && controls == 2);
    const auto &result = fresh.at("result");
    assert(result.at("format") == "voyemu.session.ui.v1" && result.at("profile") == profile &&
           result.at("session_id") == id);
    assert(result.at("status") == "ready" && result.at("complete") == true &&
           result.at("atomic_ui_state") == false);
    assert(result.at("multimedia") == Json::array({nullptr, nullptr}));
    const auto &screen = result.at("cluster");
    assert(screen.at("source") == "qnx.kanzi" &&
           screen.at("source_clock_domain") == "qnx.monotonic" && screen.at("layer") == "gauge");
    assert(screen.at("qnx_generation") == 1 && screen.at("sequence") == 4 &&
           screen.at("guest_monotonic_ns") == 42);
    const auto &interval = screen.at("host_interval");
    assert(interval.at("clock_domain") == "host.steady" && interval.at("clock_id") == id &&
           interval.at("start_ns").is_string() && interval.at("end_ns").is_string());
    assert(screen.at("screen") == "cluster" && screen.at("status") == "ok" &&
           screen.at("complete") == true);
    assert(screen.at("selection_coverage") == "gauge_node2d_tree" &&
           screen.at("windows") ==
                   Json::array({{{"id", "0"},
                                 {"source", "qnx.screen"},
                                 {"binding", "posted_before_snapshot"},
                                 {"buffer_size", {{"width", 1920}, {"height", 720}}}}}));
    assert(screen.at("observation_id") == "1-51-4" && screen.at("freshness") == "latest_present" &&
           screen.at("latest_present_guest_monotonic_ns") == 42);

    for (const char *key : {"display_id", "sdk_display_id", "width", "height", "boot_id",
                            "android_pid", "observer_pid", "owner"}) {
        assert(!screen.contains(key));
    }

    auto public_nodes = nodes;
    public_nodes[0]["id"] = "0";
    public_nodes[1]["id"] = "1";
    public_nodes[1]["parent"] = "0";
    assert(screen.at("nodes") == public_nodes);
    scenario = "quiet";
    controls = 0;
    const auto quiet = api.call(request).at("result").at("cluster");
    assert(quiet.at("sequence") == 4 && quiet.at("observation") == "last_sampled_present" &&
           quiet.at("nodes") == public_nodes);
    scenario = "behind";
    controls = 0;
    const auto behind = api.call(request).at("result").at("cluster");
    assert(behind.at("status") == "ok" && behind.at("sequence") == 6 &&
           behind.at("observation_id") == "1-51-6" &&
           behind.at("freshness") == "older_than_latest_present" &&
           behind.at("guest_monotonic_ns") == 42 &&
           behind.at("latest_present_guest_monotonic_ns") == 43);
    scenario = "no-window";
    controls = 0;
    assert(api.call(request).at("result").at("cluster").at("error_code") == "CLUSTER_UI_PROTOCOL" &&
           controls == 1);

    for (const auto *change : {"owner", "generation", "off", "no-heartbeat"}) {
        scenario = change;
        controls = 0;
        auto bounded = request;
        bounded["timeout_ms"] = 1000;
        const auto response = api.call(bounded).at("result").at("cluster");
        assert(response.at("status") == "unavailable" && response.at("nodes").empty());
        assert(response.at("error_code") == (scenario == "off" || scenario == "no-heartbeat"
                                                     ? "CLUSTER_UI_NOT_READY"
                                                     : "CLUSTER_UI_STALE"));
    }

    scenario = "invalid-owner";
    controls = 0;
    assert(api.call(request).at("result").at("cluster").at("error_code") == "CLUSTER_UI_PROTOCOL" &&
           controls == 1);
    auto deadline_request = request;
    deadline_request["timeout_ms"] = 1000;
    // Normalization copies the supplied Json before dispatch. Retained session metadata makes
    // that work visible to the transport's remaining-budget assertion without a production hook.
    deadline_request["session"]["fixture_metadata"] = std::string(32 * 1024 * 1024, 'x');
    scenario = "fresh";
    controls = 0;
    verify_public_deadline = true;
    public_received = std::chrono::steady_clock::now();
    const auto timed_public = api.call(deadline_request);
    verify_public_deadline = false;
    assert(timed_public.at("ok") == true && deadline_checks == 2);
    deadline_request.clear();
    const auto original_nodes = nodes;

    for (std::uint64_t i = nodes.size(); i < 4097; ++i) {
        auto node = nodes[1];
        node["id"] = i;
        nodes.push_back(std::move(node));
    }

    scenario = "fresh";
    controls = 0;
    const auto large_api = api.call(request);
    assert(large_api.at("ok") == true &&
           large_api.at("result").at("cluster").at("nodes").size() == 4097);
    nodes = original_nodes;
    nodes[1]["properties"]["text"]["value"] = std::string(17 * 1024 * 1024, '\x01');
    auto public_boundary_request = request;
    public_boundary_request["timeout_ms"] = 30000;
    scenario = "fresh";
    controls = 0;
    const auto public_boundary = api.call(public_boundary_request);
    assert(public_boundary.at("ok") == true &&
           public_boundary.at("result").at("status") == "partial" &&
           public_boundary.at("result").at("cluster").at("nodes").size() == 1 &&
           public_boundary.at("result").at("cluster").at("partial_reason") ==
                   "host_response_byte_budget");
    assert(count_qnx_kanzi_ui_json(public_boundary, qnx_kanzi_ui_response_bytes,
                                   std::chrono::steady_clock::now() + std::chrono::seconds(2)) <=
           qnx_kanzi_ui_response_bytes);
    nodes = original_nodes;

    scenario = "partial";
    controls = 0;
    const auto partial = api.call(request).at("result");
    assert(partial.at("status") == "partial" && partial.at("complete") == false);
    assert(partial.at("cluster").at("partial_reason") == "byte_budget");
    assert(partial.at("cluster").at("status") == "partial" &&
           partial.at("cluster").at("complete") == false);
    auto malformed = request;
    malformed["selector"] = {{"node", "Root"}};
    controls = 0;
    assert(api.call(malformed).at("error").at("code") == "TEST_ARGUMENT" && !controls);
    auto combined = request;
    combined.erase("screen");
    combined["timeout_ms"] = 2500;
    scenario = "quiet";
    controls = 0;
    verify_concurrent = true;
    const auto both = api.call(combined).at("result");
    assert(arrivals == 2 && android_reads == 1 && controls == 2);
    assert(both.at("format") == "voyemu.session.ui.v1" && both.at("status") == "ready" &&
           both.at("complete") == true && both.at("atomic_ui_state") == false);
    assert(!both.contains("screens") && both.at("cluster").at("nodes") == public_nodes &&
           both.at("cluster").at("source") == "qnx.kanzi");
    const auto &media = both.at("multimedia");
    assert(media.size() == 2 && media[0].at("screen") == "primary" &&
           media[1].at("screen") == "secondary");
    assert(media[0].at("nodes")[0].at("id") == "primary-root" &&
           media[1].at("nodes")[0].at("id") == "secondary-root");
    assert(media[0].at("source") == "android.ui" && media[0].at("boot_id") == boot &&
           media[0].at("source_instance") == "fixture-epoch");
    assert(media[1].at("started_uptime_ms") == "101" &&
           media[1].at("observer_source").at("basis") == "fixture");
    verify_concurrent = false;
    secondary_available = false;
    scenario = "no-heartbeat";
    controls = 0;
    combined["timeout_ms"] = 240;
    const auto missing = api.call(combined).at("result");
    assert(missing.at("status") == "partial" &&
           missing.at("cluster").at("error_code") == "CLUSTER_UI_NOT_READY");
    assert(missing.at("multimedia")[0].at("nodes")[0].at("id") == "primary-root");
    const auto &absent = missing.at("multimedia")[1];
    assert(absent.at("screen") == "secondary" && absent.at("status") == "unavailable" &&
           !absent.contains("display_id") && absent.at("nodes").empty());
    scenario = "quiet";
    controls = 0;
    parallel_supported = false;
    const auto old_observer_reads = android_reads;
    const auto old_observer = api.call(combined).at("result");
    assert(android_reads == old_observer_reads && old_observer.at("cluster").at("status") == "ok");
    assert(old_observer.at("multimedia")[0].at("status") == "unsupported" &&
           old_observer.at("multimedia")[1].at("status") == "unsupported");
    parallel_supported = true;
    observer_epoch_changed = true;
    controls = 0;
    const auto changed = api.call(combined).at("result");
    assert(changed.at("cluster").at("status") == "ok" &&
           changed.at("multimedia")[0].at("error_code") == "TREE_STALE");
    observer_epoch_changed = false;
    auto invalid = combined;
    invalid["screens"] = {"cluster", "cluster"};
    assert(api.call(invalid).at("error").at("code") == "TEST_ARGUMENT");
    invalid = combined;
    invalid["session"]["id"] = random_token();
    assert(api.call(invalid).at("error").at("code") == "TEST_SESSION");
    const auto first_terminal = directory / "runtime.json";
    write_private_json(first_terminal, {{"session_id", id}, {"stopped", true}});
    const auto before_stopped = android_reads;
    assert(api.call(combined).at("error").at("code") == "TEST_SESSION" &&
           android_reads == before_stopped);
    std::filesystem::remove(first_terminal);
    stop_during_read = true;
    controls = 0;
    assert(api.call(combined).at("error").at("code") == "TEST_SESSION");
    std::filesystem::remove(first_terminal);

    const std::string clone = "free-09-chinese-20260106-6.0.3-copy";
    const auto clone_seed = root / "machines" / clone;
    const auto clone_session = native_session_directory(root, clone,
                                                        voyemu::machine_identity(clone, clone), id);
    ensure_private_directory(clone_seed);
    ensure_private_directory(clone_session);
    const Json definition = {{"id", clone},
                             {"platform", "qinggan09"},
                             {"board", "8155"},
                             {"capture", {{"profile", clone}, {"referenceProfile", profile}}}};
    write_private_json(clone_seed / "profile.json", definition);
    Json images = Json::object();

    for (const auto &image : qnx_runtime_layout("sa8155-qinggan09").images) {
        images[image] = {{"file", "qnx/" + image}, {"bytes", 1u}, {"sha256", std::string(64, 'a')}};
    }

    write_private_json(clone_seed / "runtime.json",
                       {{"schema", 1},
                        {"kind", "portable-guest-runtime"},
                        {"profile", clone},
                        {"profile_sha256", sha256_file(clone_seed / "profile.json")},
                        {"qnx", {{"guest_arch", "aarch64"}, {"images", images}}}});
    write_private_json(clone_session / "machine-selection.json",
                       {{"machine", clone},
                        {"seed", clone_seed.string()},
                        {"manifest_sha256", sha256_file(clone_seed / "runtime.json")}});
    write_private_json(
            native_current_active_directory(root, clone, voyemu::machine_identity(clone, clone)) /
                    (clone + ".json"),
            {{"schema", 1},
             {"session_id", id},
             {"directory", clone_session.string()},
             {"endpoint", "owned-clone-tree-fixture"}});
    auto clone_read = request;
    clone_read["profile"] = clone;
    clone_read["machine"] = clone;
    controls = 0;
    assert(api.call(clone_read).at("result").at("profile") == clone && controls == 2);
    write_private_json(clone_seed / "profile.json", {{"id", clone}});
    controls = 0;
    assert(api.call(clone_read).at("error").at("code") == "TEST_SESSION" && controls == 0);

    // An A11 copy reaches the Kanzi scene of its reference Gauge; Passion has no Kanzi Gauge.
    for (const std::string reference :
         {"free-11-chinese-20260731-7.3.1", "passion-11-russian-20260403-2.1.4.80"}) {
        const bool passion = reference.starts_with("passion");
        const auto copy = reference + "-copy";
        const auto copy_seed = root / "machines" / copy;
        const auto copy_session =
                native_session_directory(root, copy, voyemu::machine_identity(copy, copy), id);
        ensure_private_directory(copy_seed);
        ensure_private_directory(copy_session);
        write_private_json(copy_seed / "profile.json",
                           {{"id", copy},
                            {"platform", passion ? "passion" : "qinggan11"},
                            {"board", "8155"},
                            {"capture", {{"profile", copy}, {"referenceProfile", reference}}}});
        const auto layout = qnx_runtime_layout(passion ? "sa8155-passion-21480"
                                                       : "sa8155-qinggan11-731");
        Json copy_images = Json::object();

        for (const auto &image : layout.images) {
            copy_images[image] = {
                    {"file", "qnx/" + image}, {"bytes", 1u}, {"sha256", std::string(64, 'a')}};
        }

        Json qnx = {{"guest_arch", "aarch64"}, {"images", copy_images}};
        qnx_runtime_metadata(qnx, layout);
        write_private_json(copy_seed / "runtime.json",
                           {{"schema", 1},
                            {"kind", "portable-guest-runtime"},
                            {"profile", copy},
                            {"profile_sha256", sha256_file(copy_seed / "profile.json")},
                            {"qnx", qnx}});
        write_private_json(copy_session / "machine-selection.json",
                           {{"machine", copy},
                            {"seed", copy_seed.string()},
                            {"manifest_sha256", sha256_file(copy_seed / "runtime.json")}});
        write_private_json(
                native_current_active_directory(root, copy, voyemu::machine_identity(copy, copy)) /
                        (copy + ".json"),
                {{"schema", 1},
                 {"session_id", id},
                 {"directory", copy_session.string()},
                 {"endpoint", "owned-copy-tree-fixture"}});
        auto copy_read = request;
        copy_read["profile"] = copy;
        copy_read["machine"] = copy;
        controls = 0;
        const auto response = api.call(copy_read);

        if (passion) {
            const auto &cluster = response.at("result").at("cluster");
            assert(cluster.at("status") == "unsupported" &&
                   cluster.at("error_code") == "TEST_UNSUPPORTED" && cluster.at("nodes").empty());
        } else {
            assert(response.at("result").at("profile") == copy &&
                   response.at("result").at("cluster").at("source") == "qnx.kanzi" &&
                   controls == 2);
        }
    }

    session.stop();
    std::filesystem::remove_all(root);
    std::cout << "session.ui cluster, owner fencing and wire bounds passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
