#pragma once
#include "error.hpp"
#include "qnx_cluster_ui_wire.hpp"
#include <array>
#include <map>
#include <unordered_map>

namespace voyemu::api {
// Bound the actual public envelope, sharing one byte budget across all three sources.
inline void bound_session_ui_response(Json &response, std::size_t budget,
                                      std::chrono::steady_clock::time_point deadline) {
    const auto bytes = [&](const Json &value, std::size_t limit) {
        return count_qnx_kanzi_ui_json(value, limit, deadline);
    };
    const auto fits = [&](const Json &value, std::size_t limit) {
        try {
            (void)bytes(value, limit);

            return true;
        } catch (const std::runtime_error &error) {
            if (std::string_view(error.what()) !=
                "CLUSTER_UI_RESOURCE_LIMIT: response encoding byte budget") {
                throw;
            }

            return false;
        }
    };
    const auto finalize_terminal = [&] {
        const auto &result = response.at("result");

        if (result.at("complete") != false ||
            (result.at("status") != "unavailable" && result.at("status") != "partial")) {
            return false;
        }

        bool terminal = false;
        const auto eligible = [&](const Json &slot) {
            if (slot.is_null()) {
                return true;
            }

            if (!slot.is_object() || !slot.contains("nodes") || !slot.at("nodes").is_array() ||
                !slot.contains("windows") || !slot.at("windows").is_array() ||
                !slot.contains("status")) {
                return false;
            }

            if (slot.at("status") == "unavailable" || slot.at("status") == "unsupported") {
                terminal = true;

                return slot.at("nodes").empty() && slot.at("windows").empty();
            }

            return slot.at("status") == "ok" || slot.at("status") == "partial";
        };

        if (!eligible(result.at("cluster")) || !eligible(result.at("multimedia").at(0)) ||
            !eligible(result.at("multimedia").at(1)) || !terminal) {
            return false;
        }

        // Reporting an exhausted provider must remain possible after a blocking call. This
        // terminal envelope gets at most 64 KiB of counting work, never another bulk UI pass.
        // Already completed small observations remain intact beside the terminal source status.
        (void)count_qnx_kanzi_ui_json(response, std::min<std::size_t>(budget, 64 * 1024),
                                      std::chrono::steady_clock::time_point::max());

        return true;
    };

    if (std::chrono::steady_clock::now() >= deadline) {
        if (finalize_terminal()) {
            return;
        }

        throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "response encoding deadline");
    }

    try {
        if (fits(response, budget)) {
            return;
        }
    } catch (const std::runtime_error &error) {
        if (std::string_view(error.what()) ==
                    "CLUSTER_UI_RESOURCE_LIMIT: response encoding deadline" &&
            finalize_terminal()) {
            return;
        }

        throw;
    }

    auto &result = response.at("result");
    constexpr auto reason = "host_response_byte_budget";
    struct Source {
        Json *screen = nullptr;
        Json nodes, metadata;
        std::unordered_map<std::string, std::size_t> ids;
        std::map<Json, std::pair<std::size_t, std::size_t>> window_counts;
        std::vector<unsigned char> state;
        std::size_t cursor = 0, kept = 0;
        bool stopped = false;
    };
    std::array<Source, 3> sources;
    sources[0].screen = &result.at("cluster");
    sources[1].screen = &result.at("multimedia").at(0);
    sources[2].screen = &result.at("multimedia").at(1);
    std::size_t original_larger = 0;

    for (auto &source : sources) {
        auto &screen = *source.screen;

        if (screen.is_null() || !screen.contains("nodes") || screen.at("nodes").empty()) {
            source.stopped = true;
            continue;
        }

        source.nodes = std::move(screen.at("nodes"));
        screen["nodes"] = Json::array();
        source.metadata = screen;
        const auto before = bytes(screen, budget);
        screen["complete"] = false;
        screen["status"] = "partial";
        screen["partial_reason"] = reason;

        for (auto &window : screen.at("windows")) {
            if (window.contains("read_complete")) {
                window["read_complete"] = false;
            }
        }

        const auto after = bytes(screen, budget);
        original_larger += before > after ? before - after : 0;

        for (std::size_t i = 0; i < source.nodes.size(); ++i) {
            if (std::chrono::steady_clock::now() >= deadline) {
                throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "public response deadline");
            }

            const auto &node = source.nodes[i];

            if (!node.contains("id") || !node.at("id").is_string() ||
                !source.ids.emplace(node.at("id").get<std::string>(), i).second) {
                throw VoyemuError("TREE_PROTOCOL", "invalid public node identity");
            }

            if (node.contains("window_id")) {
                ++source.window_counts[node.at("window_id")].first;
            }
        }
    }

    result["complete"] = false;
    result["status"] = "partial";
    result["partial_reason"] = reason;
    const auto metadata = bytes(response, budget);

    if (original_larger > budget - metadata) {
        throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "response metadata exceeds budget");
    }

    std::size_t remaining = budget - metadata - original_larger;

    for (auto &source : sources) {
        source.state.resize(source.nodes.is_array() ? source.nodes.size() : 0);
    }

    bool active = true;

    while (active) {
        active = false;

        for (auto &source : sources) {
            if (source.stopped) {
                continue;
            }

            while (source.cursor < source.nodes.size() && source.state[source.cursor]) {
                ++source.cursor;
            }

            if (source.cursor == source.nodes.size()) {
                source.stopped = true;
                continue;
            }

            active = true;
            // Follow actual parent identities, including Android child-before-parent order.
            // Each node becomes selected or blocked once; failed ancestors block dependants.
            std::vector<std::size_t> chain;
            std::size_t next = source.cursor;
            bool blocked = false;

            for (;;) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "public response deadline");
                }

                if (source.state[next] == 1) {
                    break;
                }

                if (source.state[next] == 2) {
                    blocked = true;
                    break;
                }

                if (source.state[next] == 3) {
                    throw VoyemuError("TREE_PROTOCOL", "cyclic public node parent");
                }

                source.state[next] = 3;
                chain.push_back(next);
                const auto &node = source.nodes[next];

                if (!node.contains("parent") ||
                    (!node.at("parent").is_null() && !node.at("parent").is_string())) {
                    throw VoyemuError("TREE_PROTOCOL", "invalid public node parent");
                }

                if (node.at("parent").is_null()) {
                    break;
                }

                const auto parent = source.ids.find(node.at("parent").get<std::string>());

                if (parent == source.ids.end()) {
                    throw VoyemuError("TREE_PROTOCOL", "dangling public node parent");
                }

                next = parent->second;
            }

            for (auto i = chain.rbegin(); i != chain.rend(); ++i) {
                if (!blocked) {
                    try {
                        const auto node_bytes = bytes(source.nodes[*i], remaining + 1) - 1;
                        const auto comma = source.kept ? 1u : 0u;

                        if (node_bytes > remaining || comma > remaining - node_bytes) {
                            blocked = true;
                        } else {
                            remaining -= node_bytes + comma;
                            ++source.kept;
                        }
                    } catch (const std::runtime_error &error) {
                        if (std::string_view(error.what()) !=
                            "CLUSTER_UI_RESOURCE_LIMIT: response encoding byte budget") {
                            throw;
                        }

                        blocked = true;
                    }
                }

                source.state[*i] = blocked ? 2 : 1;
            }

            ++source.cursor;
        }
    }

    for (auto &source : sources) {
        if (source.nodes.is_null()) {
            continue;
        }

        auto &screen = *source.screen;

        if (source.kept == source.nodes.size()) {
            screen = std::move(source.metadata);
        } else {
            if (screen.contains("node_count")) {
                screen["node_count"] = source.kept;
            }

            for (std::size_t i = 0; i < source.nodes.size(); ++i) {
                if (source.state[i] == 1 && source.nodes[i].contains("window_id")) {
                    ++source.window_counts[source.nodes[i].at("window_id")].second;
                }
            }

            for (std::size_t w = 0; w < screen.at("windows").size(); ++w) {
                auto &window = screen.at("windows")[w];
                const auto &original = source.metadata.at("windows")[w];

                if (!window.contains("id")) {
                    continue;
                }

                const auto counts = source.window_counts.find(window.at("id"));
                const auto observed = counts == source.window_counts.end() ? 0
                                                                           : counts->second.first;
                const auto count = counts == source.window_counts.end() ? 0 : counts->second.second;

                if (count == observed) {
                    window = original;
                    continue;
                }

                if (window.contains("node_count")) {
                    window["node_count"] = count;
                }

                window.erase("verified_binding");

                if (window.contains("root_id")) {
                    const auto root = source.ids.find(window.at("root_id").get<std::string>());

                    if (root == source.ids.end() || source.state[root->second] != 1) {
                        window.erase("root_id");
                    }
                }
            }
        }

        std::size_t kept = 0;

        for (std::size_t i = 0; i < source.nodes.size(); ++i) {
            if (source.state[i] == 1) {
                if (kept != i) {
                    source.nodes[kept] = std::move(source.nodes[i]);
                }

                ++kept;
            }
        }

        source.nodes.erase(source.nodes.begin() + static_cast<Json::difference_type>(kept),
                           source.nodes.end());
        screen["nodes"] = std::move(source.nodes);
    }

    (void)bytes(response, budget);
}
} // namespace voyemu::api
