#include "qnx_lzo_pack.hpp"
#include "check.hpp"
#include "error.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
// Written from the LZO1X stream format as the Linux kernel documents it in
// Documentation/staging/lzo.rst; no LZO compressor served as a source.
namespace voyemu {
namespace {
constexpr std::int32_t unreachable = std::numeric_limits<std::int32_t>::max() / 2;
// A match this long starts no search at the positions it covers, except the last half of this.
constexpr std::uint32_t nice_length = 258;
// Previous positions with the same three bytes that one position compares at most.
constexpr unsigned chain_limit = 4096;
constexpr std::uint32_t near2_distance = 1024, short_distance = 2048, far3_first = 2049,
                        far3_last = 3072, middle_distance = 16384, distant_distance = 49151;

constexpr CodeCheck need{"LZO packing check failed"};

// Bytes of a length beyond the bits of an opcode: one zero byte per 255 and a final non-zero byte.
std::int32_t extension(std::uint32_t beyond) {
    return static_cast<std::int32_t>(1 + (beyond - 1) / 255);
}

// Bytes a run of literals costs besides the literals themselves; first marks the block start.
std::int32_t literal_cost(bool first, std::uint32_t count) {
    if (first) {
        return count <= 238 ? 1 : 1 + extension(count - 18);
    }

    if (count <= 3) {
        return 0;
    }

    return count <= 18 ? 1 : 1 + extension(count - 18);
}

// Literal state after a run: what the next opcode 0..15 means.
unsigned literal_state(bool first, std::uint32_t count) {
    if (!first && count == 0) {
        return 0;
    }

    return count <= 3 ? count : 4;
}

std::int32_t middle_cost(std::uint32_t length) {
    return length <= 33 ? 3 : 3 + extension(length - 33);
}

std::int32_t distant_cost(std::uint32_t length) {
    return length <= 9 ? 3 : 3 + extension(length - 9);
}

void put_extension(std::string &out, std::uint32_t beyond) {
    for (; beyond > 255; beyond -= 255) {
        out += char(0);
    }

    out += static_cast<char>(beyond);
}
} // namespace

QnxLzoBlock::QnxLzoBlock(std::string_view plain) : plain_(plain) {
    const auto n = static_cast<std::uint32_t>(plain.size());

    need(n > 0 && n <= 65536, "QNX_LZO_PACK_BLOCK");
    const auto *data = reinterpret_cast<const unsigned char *>(plain_.data());
    // cost[i]: fewest bytes for plain[0, i) when a match ends at i (or i is the start).
    std::vector<std::int32_t> cost(n + 1, unreachable);
    struct Step {
        std::uint32_t from = 0, literals = 0, length = 0, distance = 0;
        Kind kind = Kind::short_match;
    };
    std::vector<Step> step(n + 1);
    std::vector<std::int32_t> previous(n, -1);
    std::vector<std::int32_t> head3(1 << 16, -1), head2(1 << 16, -1);
    cost[0] = 0;
    // The start j of 19 or more literals before i with the least cost[j] - j, and that value.
    std::int32_t long_best = unreachable;
    std::uint32_t long_from = 0;
    std::uint32_t covered = 0;

    for (std::uint32_t i = 0; i < n; ++i) {
        if (i >= 19 && cost[i - 19] < unreachable &&
            cost[i - 19] - static_cast<std::int32_t>(i - 19) < long_best) {
            long_best = cost[i - 19] - static_cast<std::int32_t>(i - 19);
            long_from = i - 19;
        }

        // Arrivals at i by literal state: 0 right after a match, 1..3 literals, 4 or more.
        std::array<std::int32_t, 5> arrival;
        std::array<std::uint32_t, 5> origin{};
        arrival.fill(unreachable);

        if (i > 0) {
            arrival[0] = cost[i];
            origin[0] = i;
        }

        auto offer = [&](std::uint32_t from) {
            if (cost[from] >= unreachable) {
                return;
            }

            const auto count = i - from;
            const auto state = literal_state(from == 0, count);
            const auto total = cost[from] + static_cast<std::int32_t>(count) +
                               literal_cost(from == 0, count);

            if (total < arrival[state]) {
                arrival[state] = total;
                origin[state] = from;
            }
        };

        for (std::uint32_t count = 1; count <= std::min<std::uint32_t>(i, 18); ++count) {
            offer(i - count);
        }

        if (i > 0) {
            offer(0);
        }

        if (long_best < unreachable && i - long_from >= 19) {
            offer(long_from);
        }

        std::uint32_t near2 = 0, best_short = 0, short_at = 0, far3 = 0, best_middle = 0,
                      middle_at = 0, best_distant = 0, distant_at = 0;

        if (i + 1 < n) {
            const auto key = static_cast<unsigned>(data[i] | data[i + 1] << 8);

            if (head2[key] >= 0 && i - static_cast<std::uint32_t>(head2[key]) <= near2_distance) {
                near2 = i - static_cast<std::uint32_t>(head2[key]);
            }

            head2[key] = static_cast<std::int32_t>(i);
        }

        if (i + 2 < n) {
            const auto key = static_cast<unsigned>(
                    ((data[i] << 16 | data[i + 1] << 8 | data[i + 2]) * 2654435761U) >> 16);

            if (i >= covered) {
                unsigned visited = 0;

                for (auto at = head3[key]; at >= 0 && visited < chain_limit;
                     at = previous[static_cast<std::uint32_t>(at)], ++visited) {
                    const auto distance = i - static_cast<std::uint32_t>(at);

                    if (distance > distant_distance) {
                        break;
                    }

                    // Every class open at this distance holds the longest match so far, so only a
                    // longer one counts, besides the first three-byte copy 2..3 KiB back.
                    const bool far3_open = far3 == 0 && distance >= far3_first &&
                                           distance <= far3_last;

                    if (best_distant >= 3 && !far3_open &&
                        (i + best_distant >= n ||
                         data[at + best_distant] != data[i + best_distant])) {
                        continue;
                    }

                    std::uint32_t length = 0;

                    while (i + length < n && data[at + length] == data[i + length]) {
                        ++length;
                    }

                    if (length < 3) {
                        continue;
                    }

                    if (distance <= short_distance && length > best_short) {
                        best_short = length;
                        short_at = distance;
                    }

                    if (distance >= far3_first && distance <= far3_last && far3 == 0) {
                        far3 = distance;
                    }

                    if (distance <= middle_distance && length > best_middle) {
                        best_middle = length;
                        middle_at = distance;
                    }

                    if (length > best_distant) {
                        best_distant = length;
                        distant_at = distance;
                    }

                    if (i + length == n) {
                        break;
                    }
                }
            }

            previous[i] = head3[key];
            head3[key] = static_cast<std::int32_t>(i);
        }

        if (i < covered) {
            continue;
        }

        const auto any = *std::min_element(arrival.begin(), arrival.end());
        const auto any_state = static_cast<unsigned>(
                std::min_element(arrival.begin(), arrival.end()) - arrival.begin());
        auto relax = [&](std::uint32_t length, std::uint32_t distance, Kind kind,
                         std::int32_t bytes, unsigned state) {
            const auto total = arrival[state] + bytes;

            if (total < cost[i + length]) {
                cost[i + length] = total;
                step[i + length] = {origin[state], i - origin[state], length, distance, kind};
            }
        };

        if (any >= unreachable) {
            continue;
        }

        if (near2 > 0) {
            for (unsigned state = 1; state <= 3; ++state) {
                if (arrival[state] < unreachable) {
                    relax(2, near2, Kind::near2, 2, state);
                }
            }
        }

        if (far3 > 0 && arrival[4] < unreachable) {
            relax(3, far3, Kind::far3, 2, 4);
        }

        for (std::uint32_t length = 3; length <= std::min<std::uint32_t>(best_short, 8); ++length) {
            relax(length, short_at, Kind::short_match, 2, any_state);
        }

        for (std::uint32_t length = 3; length <= best_middle; ++length) {
            relax(length, middle_at, Kind::middle, middle_cost(length), any_state);
        }

        if (distant_at > middle_distance) {
            for (std::uint32_t length = std::max<std::uint32_t>(best_middle + 1, 3);
                 length <= best_distant; ++length) {
                relax(length, distant_at, Kind::distant, distant_cost(length), any_state);
            }
        }

        if (best_distant >= nice_length) {
            covered = i + best_distant - nice_length / 2;
        }
    }

    // The literal tail after the last match and the three-byte end marker.
    std::int32_t best = unreachable;
    std::uint32_t last = 0;

    for (std::uint32_t from = 0; from <= n; ++from) {
        if (cost[from] >= unreachable || (from == 0 && n == 0)) {
            continue;
        }

        const auto count = n - from;
        const auto total = cost[from] + static_cast<std::int32_t>(count) +
                           (count == 0 ? 0 : literal_cost(from == 0, count)) + 3;

        if (total < best) {
            best = total;
            last = from;
        }
    }

    for (auto at = last; at > 0; at = step[at].from) {
        const auto &s = step[at];
        tokens_.push_back({s.literals, s.length, s.distance, s.kind});
        growable_ += s.kind == Kind::short_match;
    }

    std::reverse(tokens_.begin(), tokens_.end());
    bytes_ = static_cast<std::size_t>(best);
    need(emit().size() == bytes_, "QNX_LZO_PACK_COST");
}

std::string QnxLzoBlock::emit(std::size_t grow) const {
    need(grow <= growable_, "QNX_LZO_PACK_GROWTH");
    std::string out;
    out.reserve(bytes_ + grow);
    std::size_t at = 0, state_byte = std::string::npos;
    // Literals before a match or the end marker; 1..3 after a match ride in its state bits.
    auto literals = [&](std::uint32_t count) {
        if (at == 0) {
            if (count <= 238) {
                out += static_cast<char>(17 + count);
            } else {
                out += char(0);
                put_extension(out, count - 18);
            }
        } else if (count >= 1 && count <= 3) {
            out[state_byte] = static_cast<char>(out[state_byte] | static_cast<char>(count));
        } else if (count >= 4 && count <= 18) {
            out += static_cast<char>(count - 3);
        } else if (count > 18) {
            out += char(0);
            put_extension(out, count - 18);
        }

        out.append(plain_, at, count);
        at += count;
    };

    for (const auto &token : tokens_) {
        literals(token.literals);
        auto kind = token.kind;

        if (kind == Kind::short_match && grow > 0) {
            kind = Kind::middle;
            --grow;
        }

        const auto length = token.length, distance = token.distance;

        if (kind == Kind::near2 || kind == Kind::far3) {
            const auto d = distance - (kind == Kind::near2 ? 1 : far3_first);
            state_byte = out.size();
            out += static_cast<char>((d & 3) << 2);
            out += static_cast<char>(d >> 2);
        } else if (kind == Kind::short_match) {
            const auto d = distance - 1;
            state_byte = out.size();
            out += static_cast<char>((length - 1) << 5 | (d & 7) << 2);
            out += static_cast<char>(d >> 3);
        } else {
            const bool middle = kind == Kind::middle;
            const auto d = middle ? distance - 1 : (distance - middle_distance) & 0x3fff;
            const auto room = middle ? 33U : 9U;
            const auto opcode = middle ? 0x20U : 0x10U | ((distance - middle_distance) >> 14) << 3;

            if (length <= room) {
                out += static_cast<char>(opcode | (length - 2));
            } else {
                out += static_cast<char>(opcode);
                put_extension(out, length - room);
            }

            state_byte = out.size();
            out += static_cast<char>((d & 0x3f) << 2);
            out += static_cast<char>(d >> 6);
        }

        at += length;
    }

    literals(static_cast<std::uint32_t>(plain_.size() - at));
    out += char(0x11);
    out.append(2, 0);

    return out;
}
} // namespace voyemu
