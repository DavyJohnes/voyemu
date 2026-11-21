#include "conditions.hpp"
#include "error.hpp"
#include <algorithm>
#include <set>
namespace voyemu::runtime {
namespace {
using R = ConditionResult;

R yes() {
    return {"passed", "condition satisfied"};
}

R no() {
    return {"pending", "condition not observed"};
}

R unknown() {
    return {"unavailable", "source, coverage or epoch unavailable"};
}

bool known(const ConditionObservation &o, const std::string &path) {
    return !o.epoch.empty() &&
           std::any_of(o.available.begin(), o.available.end(), [&](const auto &p) {
               return path == p || (path.size() > p.size() && path.compare(0, p.size(), p) == 0 &&
                                    path[p.size()] == '/');
           });
}

const Json *lookup(const ConditionObservation &o, const std::string &p) {
    auto pointer = Json::json_pointer(p);

    return o.state.contains(pointer) ? &o.state.at(pointer) : nullptr;
}

bool state_predicate(const Json &e) {
    const auto op = e.at("op").get<std::string>();

    if (op == "all" || op == "any") {
        return std::all_of(e.at("args").begin(), e.at("args").end(), state_predicate);
    }

    return op == "eq" || op == "present" || op == "absent" || op == "count";
}

void validate(const Json &e, unsigned depth, unsigned &nodes) {
    if (depth > 16 || ++nodes > 128 || !e.is_object() || !e.contains("op") ||
        !e["op"].is_string()) {
        throw VoyemuError("TEST_ARGUMENT", "invalid condition AST or limit");
    }

    const auto op = e["op"].get<std::string>();
    std::set<std::string> fields = {"op"};

    if (op == "all" || op == "any" || op == "ordered" || op == "first") {
        fields.insert("args");

        if (!e.contains("args") || !e["args"].is_array() || e["args"].empty()) {
            throw VoyemuError("TEST_ARGUMENT", "condition args required");
        }

        for (const auto &arg : e["args"]) {
            validate(arg, depth + 1, nodes);

            if ((op == "ordered" || op == "first") && !state_predicate(arg)) {
                throw VoyemuError("TEST_ARGUMENT", "event alternatives require state predicates");
            }
        }
    } else if (op == "stable" || op == "never") {
        fields.insert("condition");
        fields.insert("duration_ms");

        if (!e.contains("condition") || !e.contains("duration_ms") ||
            !e["duration_ms"].is_number_integer() || e["duration_ms"].get<std::int64_t>() < 0 ||
            e["duration_ms"].get<std::int64_t>() > 86400000) {
            throw VoyemuError("TEST_ARGUMENT", "bounded duration_ms required");
        }

        validate(e["condition"], depth + 1, nodes);

        if (!state_predicate(e["condition"])) {
            throw VoyemuError("TEST_ARGUMENT", "interval requires state predicate");
        }
    } else if (op == "eq" || op == "present" || op == "absent" || op == "count" ||
               op == "changed") {
        fields.insert("path");

        if (!e.contains("path") || !e["path"].is_string() ||
            e["path"].get<std::string>().size() > 1024) {
            throw VoyemuError("TEST_ARGUMENT", "JSON pointer required");
        }

        try {
            Json::json_pointer pointer(e["path"].get<std::string>());
            (void)pointer;
        } catch (const Json::exception &) {
            throw VoyemuError("TEST_ARGUMENT", "invalid JSON pointer");
        }

        if (op == "eq" || op == "count") {
            fields.insert("value");

            if (!e.contains("value")) {
                throw VoyemuError("TEST_ARGUMENT", "value required");
            }
        }

        if (op == "count" &&
            (!e["value"].is_number_integer() || e["value"].get<std::int64_t>() < 0)) {
            throw VoyemuError("TEST_ARGUMENT", "count requires nonnegative integer");
        }
    } else {
        throw VoyemuError("TEST_ARGUMENT", "unknown condition operator");
    }

    for (auto it = e.begin(); it != e.end(); ++it) {
        if (!fields.count(it.key())) {
            throw VoyemuError("TEST_ARGUMENT", "unknown condition field");
        }
    }
}

R eval(const Json &e, const ConditionObservation &base,
       const std::vector<ConditionObservation> &events, std::int64_t now) {
    const auto op = e.at("op").get<std::string>();
    const auto &current = events.empty() ? base : events.back();

    if (op == "all" || op == "any") {
        bool pending = false, unavailable = false, possible = false;
        R pending_result = no();

        for (const auto &arg : e["args"]) {
            auto r = eval(arg, base, events, now);

            if (op == "any" && r.status == "passed") {
                return yes();
            }

            if (op == "all" && r.status == "failed") {
                return r;
            }

            pending |= r.status != "passed";
            unavailable |= r.status == "unavailable";
            possible |= r.status != "failed";

            if (r.status == "pending" && r.reason != "condition not observed") {
                pending_result = r;
            }
        }

        if (unavailable) {
            return unknown();
        }

        if (op == "any" && !possible) {
            return {"failed", "all alternatives failed"};
        }

        return op == "all" && !pending ? yes() : pending_result;
    }

    if (op == "ordered" || op == "first") {
        std::size_t index = 0;

        for (const auto &event : events) {
            if (event.gap || event.epoch != base.epoch) {
                return unknown();
            }

            if (op == "first") {
                for (std::size_t i = 0; i < e["args"].size(); ++i) {
                    auto r = eval(e["args"][i], event, {}, event.monotonic_ms);

                    if (r.status == "unavailable") {
                        return r;
                    }

                    if (r.status == "passed") {
                        return i == 0 ? yes() : R{"failed", "another condition occurred first"};
                    }
                }
            } else {
                auto r = eval(e["args"][index], event, {}, event.monotonic_ms);

                if (r.status == "unavailable") {
                    return r;
                }

                if (r.status == "passed" && ++index == e["args"].size()) {
                    return yes();
                }
            }
        }

        return no();
    }

    if (op == "stable" || op == "never") {
        auto until = base.monotonic_ms + e["duration_ms"].get<std::int64_t>();
        auto inspect = [&](const ConditionObservation &o) -> R {
            if (!o.continuous || o.gap || o.epoch != base.epoch || o.epoch.empty()) {
                return unknown();
            }

            auto r = eval(e["condition"], o, {}, o.monotonic_ms);

            if (r.status == "unavailable") {
                return r;
            }

            if ((op == "stable" && r.status != "passed") ||
                (op == "never" && r.status == "passed")) {
                return {"failed", "condition violated during interval"};
            }

            return yes();
        };

        if (!base.continuous || base.gap || base.epoch.empty()) {
            return unknown();
        }

        auto r = op == "stable" ? inspect(base) : yes();

        if (r.status != "passed") {
            return r;
        }

        for (const auto &event : events) {
            // A later watermark may prove coverage through the requested end, but
            // an occurrence after that end cannot violate the closed interval.
            if (!event.continuous || event.gap || event.epoch != base.epoch ||
                event.epoch.empty()) {
                return unknown();
            }

            if (event.monotonic_ms > until) {
                break;
            }

            r = inspect(event);

            if (r.status != "passed") {
                return r;
            }
        }

        // A caller must supply a continuous observation watermark through the interval.
        if (now >= until && current.monotonic_ms < until) {
            return {"pending", "continuous coverage watermark has not reached interval end"};
        }

        return now >= until ? yes() : no();
    }

    const auto path = e.at("path").get<std::string>();

    // An explicit journal watermark is not an event, even for absent(/event/...)
    // predicates. Ordinary missing event facts remain unknown below.
    if (current.event_watermark && (path == "/event" || path.starts_with("/event/"))) {
        return no();
    }

    if (!known(current, path)) {
        return unknown();
    }

    const auto value = lookup(current, path);

    if (op == "absent") {
        return value ? no() : yes();
    }

    if (op == "present") {
        return value ? yes() : no();
    }

    if (!value) {
        return unknown();
    }

    if (op == "eq") {
        return *value == e["value"] ? yes() : no();
    }

    if (op == "count") {
        return value->is_array() || value->is_object()
                       ? (value->size() == e["value"].get<std::size_t>() ? yes() : no())
                       : unknown();
    }

    if (!known(base, path) || base.epoch != current.epoch || current.gap) {
        return unknown();
    }

    const auto initial = lookup(base, path);

    if (!initial) {
        return unknown();
    }

    for (const auto &event : events) {
        if (!known(event, path)) {
            return unknown();
        }

        const auto observed = lookup(event, path);

        if (!observed) {
            return unknown();
        }

        if (*observed != *initial) {
            return yes();
        }
    }

    return no();
}
} // namespace

void validate_condition(const Json &expression) {
    if (expression.dump().size() > 65536) {
        throw VoyemuError("TEST_ARGUMENT", "condition payload exceeds limit");
    }

    unsigned nodes = 0;
    validate(expression, 0, nodes);
}

ConditionResult evaluate_condition(const Json &expression, const ConditionObservation &baseline,
                                   const std::vector<ConditionObservation> &events,
                                   std::int64_t now, bool deadline) {
    validate_condition(expression);

    if (events.size() > 4096) {
        return {"unavailable", "condition history retention exceeded"};
    }

    auto last = baseline.monotonic_ms;

    for (const auto &event : events) {
        if (event.monotonic_ms < last || event.monotonic_ms > now) {
            return {"unavailable", "invalid monotonic observation order"};
        }

        if (event.gap || event.epoch.empty() || event.epoch != baseline.epoch) {
            return {"unavailable", "observation gap or source epoch changed"};
        }

        last = event.monotonic_ms;
    }

    if (now < last) {
        return {"unavailable", "clock precedes observation"};
    }

    auto result = eval(expression, baseline, events, now);

    if (deadline && result.status == "pending") {
        if (result.reason == "continuous coverage watermark has not reached interval end") {
            return unknown();
        }

        return {"failed", "deadline elapsed before condition satisfied"};
    }

    return result;
}
} // namespace voyemu::runtime
