#pragma once
#include "model.hpp"
namespace voyemu::runtime {
// AST: eq(path,value), present(path), absent(path), count(path,value),
// all/any(args), changed(path), stable/never(condition,duration_ms), ordered/first(args).
// Interval condition and ordered/first args are state predicates (eq/present/absent/count,
// recursively all/any). first passes when args[0] occurs before any other alternative;
// same-event ties use argument order. ordered requires distinct consecutive matches
// in increasing event order (unrelated events may intervene). changed latches any
// observed difference from baseline, including a later restoration.
// Paths are JSON pointers. available contains complete known subtree prefixes.
// Baseline is state only; subsequent entries are event occurrences with full normalized state.
struct ConditionObservation {
    Json state;
    std::int64_t monotonic_ms = 0;
    std::string epoch;
    std::vector<std::string> available;
    bool continuous = false;
    bool gap = false;
    // Complete event-journal coverage through this instant, with no event occurrence.
    bool event_watermark = false;
};
struct ConditionResult {
    std::string status, reason;

    Json json() const {
        return {{"status", status}, {"reason", reason}};
    }
};
// Throws TEST_ARGUMENT for a malformed AST. Maximum depth 16, nodes 128,
// history 4096. Retention loss is explicit unavailable, never silent success.
void validate_condition(const Json &expression);
ConditionResult evaluate_condition(const Json &expression, const ConditionObservation &baseline,
                                   const std::vector<ConditionObservation> &events,
                                   std::int64_t now_ms, bool deadline = false);
} // namespace voyemu::runtime
