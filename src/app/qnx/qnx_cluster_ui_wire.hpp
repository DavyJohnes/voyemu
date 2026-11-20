#pragma once
#include "../../guest/qnx/cluster_ui_wire.h"
#include "error.hpp"
#include "model.hpp"
#include "qnx_adapter.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string_view>
#include <vector>

namespace voyemu {
static_assert(sizeof(QgfxKanziUiPage) == QGFX_KANZI_UI_BYTES);
static_assert(sizeof(QgfxKanziUiData) == QGFX_KANZI_UI_BYTES);
static_assert(offsetof(QgfxKanziUiPage, sequence) == 16);
static_assert(offsetof(QgfxKanziUiPage, reserved) == 136);
static_assert(offsetof(QgfxKanziUiData, payload) == 32);
inline constexpr std::uint64_t qnx_kanzi_ram_base = 0x40000000ULL,
                               qnx_kanzi_ram_end = 0x240000000ULL;
// A node record: 24 header bytes and a 12-byte header per property.
inline constexpr std::uint64_t qnx_kanzi_ui_min_record = 24 + 12 * QGFX_KANZI_UI_PROPERTIES;

// The pinned qinggan09 and qinggan11 Gauges carry the Kanzi observer; Passion has no Kanzi Gauge.
inline bool qnx_kanzi_ui_adapter(std::string_view adapter) {
    return std::any_of(qnx_adapter_registry.begin(), qnx_adapter_registry.end(),
                       [&](const QnxAdapterSpec &spec) {
                           return spec.name == adapter &&
                                  spec.platform != QnxGuestPlatform::passion;
                       });
}

struct QnxKanziUiSample {
    QgfxKanziUiPage page{};
    std::vector<unsigned char> stream;
};

inline const char *qnx_kanzi_ui_reason(unsigned reason) {
    static constexpr std::array reasons{
            "complete",   "byte_budget",           "time_budget",     "allocation_failed",
            "pin_failed", "invalid_physical_page", "traversal_cycle", "source_unavailable"};

    if (reason >= reasons.size()) {
        throw VoyemuError("CLUSTER_UI_PROTOCOL", "invalid snapshot reason");
    }

    return reasons[reason];
}

inline constexpr std::size_t qnx_kanzi_ui_response_bytes = 100ULL * 1024 * 1024;

// Both counting and encoding check each serializer write before retaining bytes.
inline std::size_t count_qnx_kanzi_ui_json(const Json &value, std::size_t budget,
                                           std::chrono::steady_clock::time_point deadline,
                                           std::string *encoded = nullptr) {
    class Buffer final : public std::streambuf {
        std::string *encoded_;
        std::size_t used_ = 0, budget_;
        std::chrono::steady_clock::time_point deadline_;

        void append(const char *data, std::size_t size) {
            if (std::chrono::steady_clock::now() >= deadline_) {
                throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "response encoding deadline");
            }

            if (size > budget_ - used_) {
                throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "response encoding byte budget");
            }

            if (encoded_) {
                encoded_->append(data, size);
            }

            used_ += size;
        }

        std::streamsize xsputn(const char *data, std::streamsize size) override {
            append(data, static_cast<std::size_t>(size));

            return size;
        }

        int_type overflow(int_type value) override {
            if (!traits_type::eq_int_type(value, traits_type::eof())) {
                const char byte = traits_type::to_char_type(value);
                append(&byte, 1);
            }

            return traits_type::not_eof(value);
        }

    public:
        Buffer(std::size_t budget, std::chrono::steady_clock::time_point deadline,
               std::string *encoded)
            : encoded_(encoded), budget_(budget), deadline_(deadline) {}

        std::size_t finish() {
            append("\n", 1);

            return used_;
        }
    } buffer(budget, deadline, encoded);
    std::ostream output(&buffer);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output << value;

    return buffer.finish();
}

inline std::string encode_qnx_kanzi_ui_json(const Json &value, std::size_t budget,
                                            std::chrono::steady_clock::time_point deadline) {
    std::string encoded;

    (void)count_qnx_kanzi_ui_json(value, budget, deadline, &encoded);

    return encoded;
}

// The complete wire stream has already been validated. A transport-limited
// answer may retain only its complete node prefix, whose parents still precede
// their children.
inline bool bound_qnx_kanzi_ui_nodes(Json &nodes, std::size_t budget,
                                     std::chrono::steady_clock::time_point deadline =
                                             std::chrono::steady_clock::time_point::max()) {
    std::size_t used = 2, keep = 0;

    if (budget < used) {
        nodes.clear();

        return true;
    }

    for (const auto &node : nodes) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "response encoding deadline");
        }

        const auto bytes =
                encode_qnx_kanzi_ui_json(node, qnx_kanzi_ui_response_bytes, deadline).size() - 1 +
                (keep ? 1 : 0);

        if (bytes > budget - used) {
            break;
        }

        used += bytes;
        ++keep;
    }

    const bool partial = keep != nodes.size();
    nodes.erase(nodes.begin() + static_cast<Json::difference_type>(keep), nodes.end());

    return partial;
}

// Bind the final supervisor envelope, including its request ID, before
// transport serialization. Metadata is serialized once without nodes; node
// bytes are counted once, avoiding dump/retry loops.
inline void bound_qnx_kanzi_ui_reply(Json &reply, std::size_t budget,
                                     std::chrono::steady_clock::time_point deadline =
                                             std::chrono::steady_clock::time_point::max()) {
    auto nodes = std::move(reply.at("nodes"));

    reply["nodes"] = Json::array();
    const auto complete = reply.at("complete");
    const auto reason = reply.at("partial_reason");
    reply["complete"] = false;
    reply["partial_reason"] = "host_response_byte_budget";
    const auto metadata = encode_qnx_kanzi_ui_json(reply, budget, deadline).size();

    if (metadata > budget) {
        throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "response metadata exceeds budget");
    }

    const bool partial = bound_qnx_kanzi_ui_nodes(nodes, budget - metadata + 2, deadline);

    if (nodes.empty()) {
        throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "no complete node fits response budget");
    }

    if (!partial) {
        reply["complete"] = complete;
        reply["partial_reason"] = reason;
    }

    reply["nodes"] = std::move(nodes);
}

struct QnxKanziUiLimits {
    std::uint64_t bytes = 16ULL * 1024 * 1024;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() +
                                                     std::chrono::milliseconds(250);
};

// Stable malformed snapshots fail explicitly. A malformed intermediate read is
// discarded when the final sequence/descriptor check proves a concurrent writer
// or a replaced producer.
template <class Reader>
std::optional<QnxKanziUiSample> read_qnx_kanzi_ui(Reader &&read, std::uint64_t producer,
                                                  std::uint64_t pid, std::uint64_t incarnation,
                                                  QnxKanziUiLimits limits = {}) {
    const auto address_ok = [](std::uint64_t address) {
        return address >= qnx_kanzi_ram_base &&
               address <= qnx_kanzi_ram_end - QGFX_KANZI_UI_BYTES &&
               address % QGFX_KANZI_UI_BYTES == 0;
    };

    if (!pid || !incarnation || !address_ok(producer)) {
        return {};
    }

    const auto read_value = [&](std::uint64_t address, auto &value) {
        return read(address, std::as_writable_bytes(std::span(&value, 1)));
    };
    const auto descriptor = [&]() -> std::uint64_t {
        std::array<std::uint32_t, 3> channel{};
        constexpr auto pid_word = (QGFX_KANZI_UI_PID_OFFSET - QGFX_KANZI_UI_DESCRIPTOR_OFFSET) / 8;
        constexpr auto incarnation_word =
                (QGFX_KANZI_UI_INCARNATION_OFFSET - QGFX_KANZI_UI_DESCRIPTOR_OFFSET) / 8;
        std::array<std::uint64_t, incarnation_word + 1> identity{};

        if (!read_value(producer, channel) || channel[0] != 0x58464751 || channel[1] != 3 ||
            channel[2] > 2 || !read_value(producer + QGFX_KANZI_UI_DESCRIPTOR_OFFSET, identity) ||
            identity[pid_word] != pid || identity[incarnation_word] != incarnation) {
            return 0;
        }

        return identity[0];
    };
    const auto address = descriptor();

    if (!address_ok(address) || address == producer) {
        return {};
    }

    std::uint64_t before = 0, after = 0;
    constexpr auto seq = offsetof(QgfxKanziUiPage, sequence);

    if (!read_value(address + seq, before) || !before || (before & 1)) {
        return {};
    }

    QnxKanziUiSample sample;
    auto &h = sample.page;

    if (!read_value(address, h)) {
        return {};
    }

    bool invalid = h.magic != QGFX_KANZI_UI_MAGIC || h.schema != 1 || h.header_bytes != 136 ||
                   h.guest_pid != pid || h.incarnation != incarnation ||
                   h.guest_monotonic_ns < incarnation ||
                   h.capture_finished_ns < h.guest_monotonic_ns ||
                   h.reason > QGFX_KANZI_UI_SOURCE_UNAVAILABLE || h.sequence != before ||
                   !h.window_width || !h.window_height ||
                   (!h.count && h.reason == QGFX_KANZI_UI_COMPLETE) ||
                   (h.used == 0 ? h.pages != 0 || h.first_page != 0 || h.count != 0
                                : h.pages != (h.used - 1) / QGFX_KANZI_UI_PAYLOAD + 1);
    const bool oversized =
            h.used > limits.bytes ||
            h.pages > (limits.bytes - std::min<std::uint64_t>(limits.bytes, h.used)) / 64;
    bool expired = false;
    std::set<std::uint64_t> addresses{producer, address};
    std::uint64_t next = h.first_page;

    if (!invalid && !oversized) {
        sample.stream.reserve(static_cast<std::size_t>(h.used));

        for (std::uint64_t i = 0; i < h.pages; ++i) {
            if (std::chrono::steady_clock::now() >= limits.deadline) {
                expired = true;
                break;
            }

            if (!address_ok(next) || !addresses.insert(next).second) {
                invalid = true;
                break;
            }

            QgfxKanziUiData page{};

            if (!read_value(next, page)) {
                return {};
            }

            const auto expected = std::min<std::uint64_t>(QGFX_KANZI_UI_PAYLOAD,
                                                          h.used - sample.stream.size());

            if (page.sequence != before || page.ordinal != i || page.reserved ||
                page.used != expected || (i + 1 == h.pages ? page.next != 0 : page.next == 0)) {
                invalid = true;
                break;
            }

            sample.stream.insert(sample.stream.end(), page.payload, page.payload + page.used);
            next = page.next;
        }
    }

    if (!read_value(address + offsetof(QgfxKanziUiPage, heartbeat_ns), h.heartbeat_ns) ||
        !read_value(address + offsetof(QgfxKanziUiPage, presented_ns), h.presented_ns) ||
        !read_value(address + seq, after) || before != after || descriptor() != address) {
        return {};
    }

    if (invalid) {
        throw VoyemuError("CLUSTER_UI_PROTOCOL", "invalid Gauge page chain");
    }

    if (h.heartbeat_ns < incarnation) {
        return {};
    }

    if (oversized || expired || std::chrono::steady_clock::now() >= limits.deadline) {
        throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "snapshot exceeds reader budget");
    }

    return sample;
}

inline Json decode_qnx_kanzi_ui(const QnxKanziUiSample &sample,
                                QnxKanziUiLimits limits = {256ULL * 1024 * 1024}) {
    const auto malformed = [] {
        throw VoyemuError("CLUSTER_UI_PROTOCOL", "invalid Gauge scene snapshot");
    };
    const auto &h = sample.page;

    (void)qnx_kanzi_ui_reason(h.reason);

    if (h.used != sample.stream.size() || h.count > h.used / qnx_kanzi_ui_min_record ||
        (!h.count && h.reason == QGFX_KANZI_UI_COMPLETE)) {
        malformed();
    }

    // Conservative accounting for JSON nodes/properties in addition to the byte
    // stream; unlike a node-count schema cap this is a per-reader resource
    // policy, checked before decoding.
    if (h.used > limits.bytes || h.count > (limits.bytes - h.used) / 2048) {
        throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "decoded snapshot exceeds budget");
    }

    const auto utf8 = [](const unsigned char *bytes, std::size_t length) -> std::optional<Json> {
        Json value = std::string(reinterpret_cast<const char *>(bytes), length);
        try {
            (void)value.dump();
        } catch (const Json::type_error &) {
            return {};
        }

        return value;
    };
    Json nodes = Json::array();
    std::size_t at = 0;
    const auto take = [&](std::size_t length) {
        if (length > sample.stream.size() - at) {
            malformed();
        }

        const auto *bytes = sample.stream.data() + at;
        at += length;

        return bytes;
    };
    const auto number = [&](unsigned bytes) {
        const auto *value = take(bytes);
        std::uint64_t out = 0;

        for (unsigned i = 0; i < bytes; ++i) {
            out |= std::uint64_t(value[i]) << (8 * i);
        }

        return out;
    };

    for (std::uint64_t index = 0; index < h.count; ++index) {
        if (std::chrono::steady_clock::now() >= limits.deadline) {
            throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "snapshot decoding deadline");
        }

        const auto start = at;
        const auto length = number(4), flags = number(4), parent = number(8),
                   name_status = number(4), name_bytes = number(4);

        if (length < qnx_kanzi_ui_min_record || length > sample.stream.size() - start ||
            (flags & ~7ULL) || (index ? parent >= index : parent != QGFX_KANZI_UI_ROOT) ||
            name_bytes > QGFX_KANZI_UI_MAX_NAME ||
            (name_status != QGFX_KANZI_UI_VALUE && name_status != QGFX_KANZI_UI_UNAVAILABLE) ||
            ((flags & QGFX_KANZI_UI_NAME_UNAVAILABLE) !=
             (name_status == QGFX_KANZI_UI_UNAVAILABLE ? QGFX_KANZI_UI_NAME_UNAVAILABLE : 0)) ||
            (name_status == QGFX_KANZI_UI_UNAVAILABLE && name_bytes)) {
            malformed();
        }

        const auto *name_data = take(static_cast<std::size_t>(name_bytes));
        Json name = name_status == QGFX_KANZI_UI_VALUE
                            ? utf8(name_data, name_bytes).value_or(Json())
                            : Json();
        static constexpr std::array<const char *, QGFX_KANZI_UI_PROPERTIES> keys{
                "text", "visible", "enabled", "opacity", "actual_width", "actual_height"};
        static constexpr std::array<unsigned, QGFX_KANZI_UI_PROPERTIES> kinds{
                QGFX_KANZI_UI_KIND_STRING, QGFX_KANZI_UI_KIND_BOOL,  QGFX_KANZI_UI_KIND_BOOL,
                QGFX_KANZI_UI_KIND_FLOAT,  QGFX_KANZI_UI_KIND_FLOAT, QGFX_KANZI_UI_KIND_FLOAT};
        Json properties = Json::object();

        for (std::size_t i = 0; i < keys.size(); ++i) {
            const auto status = number(4), kind = number(4), size = number(4);
            const bool text = kind == QGFX_KANZI_UI_KIND_STRING;

            if (kind != kinds[i] || status > QGFX_KANZI_UI_NON_FINITE ||
                (status != QGFX_KANZI_UI_VALUE && size) ||
                (status == QGFX_KANZI_UI_TOO_LONG && !text) ||
                (status == QGFX_KANZI_UI_NON_FINITE && kind != QGFX_KANZI_UI_KIND_FLOAT) ||
                (status == QGFX_KANZI_UI_VALUE &&
                 (text ? size > QGFX_KANZI_UI_MAX_STRING
                       : size != (kind == QGFX_KANZI_UI_KIND_BOOL ? 1U : 4U)))) {
                malformed();
            }

            const auto *value = take(static_cast<std::size_t>(size));
            Json property;

            if (status == QGFX_KANZI_UI_VALUE && text) {
                const auto decoded = utf8(value, size);
                property = decoded ? Json{{"status", "ok"}, {"value", *decoded}}
                                   : Json{{"status", "unavailable"}, {"reason", "not_utf8"}};
            } else if (status == QGFX_KANZI_UI_VALUE && kind == QGFX_KANZI_UI_KIND_BOOL) {
                if (value[0] > 1) {
                    malformed();
                }

                property = {{"status", "ok"}, {"value", value[0] == 1}};
            } else if (status == QGFX_KANZI_UI_VALUE) {
                std::uint32_t bits = 0;

                for (unsigned n = 0; n < 4; ++n) {
                    bits |= std::uint32_t(value[n]) << (8 * n);
                }

                const auto scalar = std::bit_cast<float>(bits);

                if (!std::isfinite(scalar)) {
                    malformed();
                }

                property = {{"status", "ok"}, {"value", scalar}};
            } else if (status == QGFX_KANZI_UI_UNSET && text) {
                property = {{"status", "unset"}};
            } else {
                property = {{"status", "unavailable"},
                            {"reason", status == QGFX_KANZI_UI_UNSET      ? "value_unavailable"
                                       : status == QGFX_KANZI_UI_TOO_LONG ? "too_long"
                                       : status == QGFX_KANZI_UI_NON_FINITE
                                               ? "non_finite_value"
                                               : "property_type_unavailable"}};
            }

            properties[keys[i]] = std::move(property);
        }

        if (at - start != length) {
            malformed();
        }

        nodes.push_back({{"id", index},
                         {"parent", index ? Json(parent) : Json()},
                         {"name", std::move(name)},
                         {"visible", bool(flags & 1)},
                         {"enabled", bool(flags & 2)},
                         {"properties", std::move(properties)}});
    }

    if (at != sample.stream.size()) {
        malformed();
    }

    return nodes;
}
} // namespace voyemu
