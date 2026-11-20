#include "passion_power_owner.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace voyemu;
using namespace voyemu::media;

// File-scope frame builder so a lambda passed into PassionPowerOwner's std::function does not
// capture a non-copyable enclosing local (it must be copy-constructible for type erasure).
static std::string line_of(unsigned sequence, std::int64_t guest, std::int64_t observed) {
    return Json{{"schema", 1},
                {"kind", "display_state_event"},
                {"session_id", "session"},
                {"watch_id", "watch"},
                {"boot_id", "boot"},
                {"sequence", sequence},
                {"guest_time_ms", guest},
                {"states", Json::array({{{"display", 0},
                                         {"unique_id", "local:8141241408256768"},
                                         {"state", 2},
                                         {"kind", "observed"},
                                         {"request", 0},
                                         {"transaction", 0},
                                         {"order", sequence},
                                         {"observed_ms", observed}}})}}
                   .dump() +
           "\n";
}

int main() try {
    assert(!PassionPowerOwner::enabled(Json::object()));
    Json spec = {{"display_power_capability", PassionPowerOwner::capability()}};
    assert(PassionPowerOwner::enabled(spec));
    spec["display_power_capability"]["sdk_helper_dex_sha256"] = "unreviewed";
    bool bad = false;
    try {
        PassionPowerOwner::enabled(spec);
    } catch (...) {
        bad = true;
    }

    assert(bad);
    // Opt-in causal trace (strict watch-clock guard unchanged). Every watch frame must be recorded
    // with the complete admission triplet (guest_time_ms / host receipt / verified guest UTC) plus
    // projection upper bound and uncertainty, both when the guard admits and when it rejects; the
    // trace may never change a guard decision and the record stream stays hard-bounded. Fakes
    // honour the real child contract (liveness shared per generation, cleared by stop()), and both
    // clocks come from explicit timelines rather than side effects of a clock read.
    auto frame = [](unsigned sequence, std::int64_t guest, std::int64_t observed) {
        return Json{{"schema", 1},
                    {"kind", "display_state_event"},
                    {"session_id", "session"},
                    {"watch_id", "watch"},
                    {"boot_id", "boot"},
                    {"sequence", sequence},
                    {"guest_time_ms", guest},
                    {"states", Json::array({{{"display", 0},
                                             {"unique_id", "local:8141241408256768"},
                                             {"state", 2},
                                             {"kind", "observed"},
                                             {"request", 0},
                                             {"transaction", 0},
                                             {"order", sequence},
                                             {"observed_ms", observed}}})}};
    };
    struct Fake {
        std::vector<std::string> lines;
        std::size_t next{};
        bool live = true;
        unsigned stops{};

        PassionWatchStream::Child child() {
            return PassionWatchStream::Child{[&] {
                                                 return live;
                                             },
                                             [&](std::size_t) {
                                                 return next < lines.size() ? lines[next++]
                                                                            : std::string{};
                                             },
                                             [&] {
                                                 live = false;
                                                 ++stops;
                                             },
                                             [] {}};
        }
    };
    constexpr std::uint64_t kPoll =
            200000; // 200ms owner poll; every host clock read is one poll step
    {
        // (a) Admitted triplets: the OEM watch republishes its observation on every poll and the
        // verified guest clock advances in lockstep, so the strict guard admits frames and the
        // trace carries the launch plus every admission with real projection numbers. The retained
        // ON observation lets an ordinary gesture through with zero wake commands.
        std::vector<Json> traced;
        unsigned calls{};
        std::vector<std::shared_ptr<Fake>> children;
        auto publish = [&](unsigned count) {
            if (!children.empty() && children.back()->live) {
                for (unsigned i = 0; i < count; i++) {
                    const auto sequence = static_cast<unsigned>(children.back()->lines.size()) + 1;
                    const auto sampled = 1000 + static_cast<std::int64_t>(sequence) * 200;
                    children.back()->lines.push_back(frame(sequence, sampled, sampled).dump() +
                                                     "\n");
                }
            }
        };
        PassionPowerOwner live(
                PassionPointerWake::Binding{true, "session", "boot", "watch"}, [] {},
                [&](const auto &) {
                    auto fake = std::make_shared<Fake>();
                    children.push_back(fake);

                    return fake->child();
                },
                [](const auto &, int) {
                    return Json{};
                },
                [&] {
                    return static_cast<std::uint64_t>(100 + kPoll * (calls++));
                },
                [] {
                    return 1200;
                },
                [&](const Json &record) {
                    traced.push_back(record);
                });

        for (unsigned cycle = 0; cycle < 5; ++cycle) {
            publish(1);
            live.poll();
        }

        unsigned launched = 0, admitted = 0, rejected = 0;

        for (const auto &r : traced) {
            const auto kind = r.at("kind").get<std::string>();

            if (kind == "launched") {
                ++launched;
            } else if (kind == "admitted") {
                ++admitted;
            } else {
                assert(kind == "rejected");
                ++rejected;
            }
        }

        assert(launched == 1 && admitted == 4 && rejected == 0 && traced.size() == 5);
        assert(traced[0].at("kind") == "launched" &&
               traced[0].at("launch_host_monotonic_us") == 200100u);
        assert(traced[1].at("kind") == "admitted" && traced[1].at("guest_time_ms") == 1200 &&
               traced[1].at("receipt_host_monotonic_us") == 600100u &&
               traced[1].at("verified_guest_utc_ms") == 1200 &&
               traced[1].at("projection_uncertainty_ms") == 400 &&
               traced[1].at("projection_upper_ms") == 1600 &&
               traced[1].at("session_id") == "session" && traced[1].at("watch_id") == "watch" &&
               traced[1].at("boot_id") == "boot");
        assert(traced[4].at("kind") == "admitted" && traced[4].at("sequence") == 4 &&
               traced[4].at("guest_time_ms") == 1800 &&
               traced[4].at("receipt_host_monotonic_us") == 1800100u &&
               traced[4].at("verified_guest_utc_ms") == 1200 &&
               traced[4].at("projection_upper_ms") == 2800);
        assert(!live.gesture(0) &&
               !live.gesture(0)); // ON observation consumed twice, no wake command issued
    }
    {
        // (b) The real 2026-10-22 theme-1 shape: the child retained two observations and the host
        // drained them in the same poll, so the guest advanced a whole 1001ms tick while the host
        // receipt advanced only 400ms. The strict rule must reject, the trace must name the
        // violation with the full triplet, and the closed generation must leave the cache unusable.
        std::vector<Json> traced;
        unsigned calls{};
        std::shared_ptr<Fake> fake;
        PassionPowerOwner batch(
                PassionPointerWake::Binding{true, "session", "boot", "watch"}, [] {},
                [&](const auto &) {
                    fake = std::make_shared<Fake>();
                    fake->lines.push_back(frame(1, 1000, 1000).dump() + "\n");
                    fake->lines.push_back(frame(2, 2001, 2001).dump() + "\n");

                    return fake->child();
                },
                [](const auto &, int) {
                    return Json{};
                },
                [&] {
                    return static_cast<std::uint64_t>(100 + kPoll * (calls++));
                },
                [] {
                    return 1000;
                },
                [&](const Json &record) {
                    traced.push_back(record);
                });
        bool fatal = false;

        for (unsigned cycle = 0; cycle < 3 && !fatal; ++cycle) {
            try {
                batch.poll();
            } catch (const std::exception &) {
                fatal = true;
            }
        }

        assert(fatal && traced.size() == 3);
        assert(traced[0].at("kind") == "launched" &&
               traced[0].at("launch_host_monotonic_us") == 200100u);
        assert(traced[1].at("kind") == "admitted" && traced[1].at("sequence") == 1 &&
               traced[1].at("guest_time_ms") == 1000 &&
               traced[1].at("receipt_host_monotonic_us") == 400100u &&
               traced[1].at("verified_guest_utc_ms") == 1000 &&
               traced[1].at("projection_uncertainty_ms") == 200 &&
               traced[1].at("projection_upper_ms") == 1200);
        assert(traced[2].at("kind") == "rejected" && traced[2].at("sequence") == 2 &&
               traced[2].at("guest_time_ms") == 2001 &&
               traced[2].at("receipt_host_monotonic_us") == 800100u &&
               traced[2].at("verified_guest_utc_ms") == 1000 &&
               traced[2].at("projection_uncertainty_ms") == 200 &&
               traced[2].at("error").get<std::string>() ==
                       "PASSION_WATCH: Watch clock envelope discontinuity");
        // The rejected generation lost its cache, so a pointer gesture is not consumed from a dead
        // observation; it queues the asynchronous recheck instead (the theme-1 pointer failure).
        bool recheck = false;
        try {
            batch.gesture(0);
        } catch (const std::exception &) {
            recheck = true;
        }

        assert(recheck);
        batch.close();
        assert(fake->stops == 1 && !fake->live);
    }
    {
        // (c) Trace bounds: a verified guest clock permanently frozen two seconds behind the guest
        // timeline trips the strict envelope again in every generation, so the record stream must
        // stop at 32 entries and 32KiB no matter how many further frames the retained children
        // publish or generations relaunch.
        std::vector<Json> traced;
        unsigned calls{};
        std::size_t sequence{};
        std::vector<std::shared_ptr<Fake>> children;
        auto publish = [&] {
            ++sequence;
            const auto observed = 1000 + static_cast<std::int64_t>(sequence) * 200;

            return frame(static_cast<unsigned>(sequence), observed, observed).dump() + "\n";
        };
        PassionPowerOwner capped(
                PassionPointerWake::Binding{true, "session", "boot", "watch"}, [] {},
                [&](const auto &) {
                    auto fake = std::make_shared<Fake>();
                    fake->lines.push_back(publish());
                    children.push_back(fake);

                    return fake->child();
                },
                [](const auto &, int) {
                    return Json{};
                },
                [&] {
                    return static_cast<std::uint64_t>(100 + kPoll * (calls++));
                },
                [] {
                    return 1000;
                },
                [&](const Json &record) {
                    traced.push_back(record);
                });

        for (unsigned cycle = 0; cycle < 200; ++cycle) {
            if (!children.empty() && children.back()->live) {
                children.back()->lines.push_back(publish());
            }

            try {
                capped.poll();
            } catch (const std::exception &) { /* Ignored: continued failures must stay bounded */
            }
        }

        assert(traced.size() == 32);
        unsigned launched = 0, admitted = 0, rejected = 0, bytes = 0;

        for (const auto &r : traced) {
            const auto kind = r.at("kind").get<std::string>();
            assert(kind == "launched" || kind == "admitted" || kind == "rejected");
            assert(r.at("session_id") == "session" && r.at("watch_id") == "watch" &&
                   r.at("boot_id") == "boot");

            if (kind == "launched") {
                ++launched;
            } else if (kind == "admitted") {
                ++admitted;
            } else {
                ++rejected;
            }

            bytes += static_cast<unsigned>(r.dump().size());
        }

        assert(launched >= 2 && admitted >= 2 && rejected >= 2 && bytes <= 32768);
        assert(children.size() >= 8); // the guard kept refusing generations after the trace stopped
    }
    {
        // (d) Coarse verified_guest_utc_ms is a FIRST-FRAME ANCHOR ONLY (uncertainty derives from
        // receipt-launch, not from it) and preserves the first-frame coarse bound.
        // PassionWatchStream::drain dispatches accept_ over ALL buffered lines in one owner poll,
        // so a synchronous per-event ADB guest clock would stall dispatch and stamp later events'
        // authentic now= receipts late -> spurious >1500ms discontinuity. The coarse clock is read
        // before launch once per projection incarnation; the strict monotonic guest-vs-receipt lag
        // check is UNCHANGED, so a genuine backlog still rejects. The first coarse read takes
        // 2600ms; any redundant read takes another 2400ms. Both must happen outside frame dispatch:
        // even one read inside the first callback delays the next authentic receipt.
        auto batched = [](bool stale) {
            std::vector<Json> traced;
            unsigned monotonic{};
            std::size_t guest_calls{};
            std::shared_ptr<Fake> fake;
            auto now = [&] {
                return static_cast<std::uint64_t>(100 + kPoll * (monotonic++));
            };
            auto guest = [&] {
                if (++guest_calls == 1) {
                    monotonic += 13;
                } else {
                    monotonic += 12;
                }

                return 1200LL;
            };
            PassionPowerOwner o(
                    PassionPointerWake::Binding{true, "session", "boot", "watch"}, [] {},
                    [&](const auto &) {
                        fake = std::make_shared<Fake>();
                        std::string buffered;

                        for (unsigned i = 1; i <= 3; ++i) {
                            buffered += line_of(i, 1000 + 200 * i, 1000 + 200 * i);
                        }

                        fake->lines.push_back(buffered);

                        return fake->child();
                    },
                    [](const auto &, int) {
                        return Json{};
                    },
                    now, guest,
                    [&](const Json &r) {
                        traced.push_back(r);
                    });
            bool fatal = false;
            o.poll();

            if (stale) {
                monotonic += 20;
                fake->lines.push_back(line_of(4, 1800, 1800));
            }

            for (unsigned cycle = 0; cycle < 2 && !fatal; ++cycle) {
                try {
                    o.poll();
                } catch (const std::exception &) {
                    fatal = true;
                }
            }

            o.close();

            return std::make_tuple(fatal, traced, guest_calls);
        };
        auto [green_fatal, green_traced, green_guest_calls] = batched(false);
        assert(!green_fatal && green_traced.size() == 4);

        for (unsigned i = 1; i <= 3; ++i) {
            assert(green_traced[i].at("kind") == "admitted" && green_traced[i].at("sequence") == i);
        }

        assert(green_traced[3].at("guest_time_ms") == 1600 &&
               green_traced[3].at("verified_guest_utc_ms") == 1200);
        assert(green_traced[3].at("receipt_host_monotonic_us") == 3400100u &&
               green_guest_calls == 1);
        // A monotonically newer guest frame held for a real four-second backlog still rejects.
        auto [stale_fatal, stale_traced, stale_guest_calls] = batched(true);
        assert(stale_fatal && stale_traced.size() == 5 &&
               stale_traced[4].at("kind") == "rejected" &&
               stale_traced[4].at("guest_time_ms") == 1800);
        assert(stale_traced[4].at("error").get<std::string>() ==
               "PASSION_WATCH: Watch clock envelope discontinuity");
        assert(stale_guest_calls == 1);
        // (d4) FIRST-FRAME COARSE BOUND is preserved: the first event still coarse-verifies
        // |guest-verified|<=2000; a first guest_time_ms far from the coarse reading is rejected as
        // a coarse epoch mismatch, cache or not.
        {
            std::vector<Json> t;
            unsigned m{};
            std::size_t gc{};
            std::shared_ptr<Fake> f;
            PassionPowerOwner o(
                    PassionPointerWake::Binding{true, "session", "boot", "watch"}, [] {},
                    [&f](const auto &) {
                        f = std::make_shared<Fake>();
                        f->lines.push_back(line_of(1, 5000, 5000));

                        return f->child();
                    },
                    [](const auto &, int) {
                        return Json{};
                    },
                    [&] {
                        return static_cast<std::uint64_t>(100 + kPoll * (m++));
                    },
                    [&] {
                        ++gc;

                        return 1200LL;
                    },
                    [&](const Json &r) {
                        t.push_back(r);
                    });
            bool fatal = false;

            for (unsigned c = 0; c < 3 && !fatal; ++c) {
                try {
                    o.poll();
                } catch (const std::exception &) {
                    fatal = true;
                }
            }

            assert(fatal && t.size() == 2 && t[1].at("kind") == "rejected" &&
                   t[1].at("error").get<std::string>() ==
                           "PASSION_WATCH: Watch clock coarse epoch mismatch");
            o.close();
        }
        // (d5) SAME-OWNER relaunch after lost() re-reads a FRESH coarse clock: both projection and
        // cache reset (the stream relaunches within the SAME owner; the owner is NOT rebuilt per
        // epoch).
        std::vector<Json> rel_trace;
        unsigned rel_calls{};
        std::size_t rel_clock{};
        std::int64_t coarse = 1200;
        std::vector<std::shared_ptr<Fake>> kids;
        PassionPowerOwner rel(
                PassionPointerWake::Binding{true, "session", "boot", "watch"}, [] {},
                [&coarse, &kids](const auto &) {
                    auto f = std::make_shared<Fake>();
                    kids.push_back(f);
                    f->lines.push_back(line_of(1, coarse, coarse));

                    return f->child();
                },
                [](const auto &, int) {
                    return Json{};
                },
                [&] {
                    return static_cast<std::uint64_t>(100 + kPoll * (rel_calls++));
                },
                [&] {
                    ++rel_clock;

                    return coarse;
                },
                [&](const Json &r) {
                    rel_trace.push_back(r);
                });
        rel.poll();
        assert(rel_clock == 1 &&
               rel_trace.back().at("kind") == "admitted"); // gen 1: one coarse read
        kids.back()->live = false;
        try {
            rel.poll();
        } catch (const std::exception &) { /* Ignored: a dying child may fail this poll. */
        } // child dies -> lost() resets cache

        coarse = 5000;
        rel_calls += 15;
        rel.poll(); // pass the real two-second retry and admit a fresh coarse epoch
        assert(kids.size() == 2 && rel_clock == 2 && rel_trace.back().at("kind") == "admitted" &&
               rel_trace.back().at("guest_time_ms") == 5000 &&
               rel_trace.back().at("verified_guest_utc_ms") == 5000);
        rel.close();
    }
    {
        // (e) The 18-second child rotation retains the projection and coarse anchor. A redundant
        // synchronous read here would block the retained child and let the new child overtake its
        // buffered observation.
        std::uint64_t host = 100;
        unsigned coarse_reads{};
        std::vector<Json> trace;
        struct Child {
            bool live = true;
            std::string bytes;
        };
        std::vector<std::shared_ptr<Child>> kids;
        PassionPowerOwner owner(
                PassionPointerWake::Binding{true, "session", "boot", "watch"}, [] {},
                [&](const auto &) {
                    auto child = std::make_shared<Child>();
                    const auto guest = 1200 + static_cast<std::int64_t>((host - 2600100) / 1000);
                    child->bytes = line_of(1, guest, guest);
                    kids.push_back(child);

                    return PassionWatchStream::Child{[child] {
                                                         return child->live;
                                                     },
                                                     [child](std::size_t) {
                                                         auto bytes = child->bytes;
                                                         child->bytes.clear();

                                                         return bytes;
                                                     },
                                                     [child] {
                                                         child->live = false;
                                                     },
                                                     [] {}};
                },
                [](const auto &, int) {
                    return Json{};
                },
                [&] {
                    return host;
                },
                [&] {
                    ++coarse_reads;
                    host += 2600000;

                    return 1200 + static_cast<std::int64_t>((host - 2600100) / 1000);
                },
                [&](const Json &record) {
                    trace.push_back(record);
                });
        owner.poll();
        assert(kids.size() == 1 && coarse_reads == 1 && trace.back().at("kind") == "admitted");
        host += 18000000;
        kids[0]->bytes = line_of(2, 19200, 19200);
        owner.poll();
        assert(kids.size() == 2 && kids[0]->live && kids[1]->live && coarse_reads == 1);
        kids[0]->bytes = line_of(3, 19200, 19200);
        owner.poll();
        assert(trace.back().at("kind") == "admitted" && trace.back().at("sequence") == 3 &&
               trace.back().at("receipt_host_monotonic_us") == 20600100u &&
               trace.back().at("verified_guest_utc_ms") == 1200);
        // Retaining the anchor does not hide a real four-second backlog on that same child.
        host += 4000000;
        kids[0]->bytes = line_of(4, 19400, 19400);
        bool stale = false;
        try {
            owner.poll();
        } catch (const std::exception &) {
            stale = true;
        }

        assert(stale && coarse_reads == 1 && trace.back().at("kind") == "rejected" &&
               trace.back().at("error").get<std::string>() ==
                       "PASSION_WATCH: Watch clock envelope discontinuity");
        owner.close();
    }
    unsigned launches = 0, commands = 0, stops = 0;
    bool alive = true;
    std::string bytes;
    Json event = {{"schema", 1},
                  {"kind", "display_state_event"},
                  {"session_id", "session"},
                  {"watch_id", "watch"},
                  {"boot_id", "boot"},
                  {"sequence", 1},
                  {"guest_time_ms", 1000},
                  {"states", Json::array({{{"display", 0},
                                           {"unique_id", "local:8141241408256768"},
                                           {"state", 1},
                                           {"kind", "applied"},
                                           {"request", 1},
                                           {"transaction", 1},
                                           {"order", 1},
                                           {"observed_ms", 1000}}})}};
    bytes = event.dump() + "\n";
    PassionPointerWake::Binding b{true, "session", "boot", "watch"};
    PassionPowerOwner owner(
            b, [] {},
            [&](const auto &) {
                ++launches;
                alive = true;

                return PassionWatchStream::Child{[&] {
                                                     return alive;
                                                 },
                                                 [&](std::size_t) {
                                                     auto s = bytes;
                                                     bytes.clear();

                                                     return s;
                                                 },
                                                 [&] {
                                                     alive = false;
                                                     ++stops;
                                                 },
                                                 [] {}};
            },
            [&](const std::vector<std::string> &args, int timeout) {
                ++commands;
                assert(timeout == 3500 && args.size() == 8 && args[5] == "wake" && args[6] == "0" &&
                       args[7] == "boot");

                return Json{{"ok", true},
                            {"kind", "wake_observed"},
                            {"boot_id", "boot"},
                            {"display", 0},
                            {"unique_id", "local:8141241408256768"},
                            {"state", 2},
                            {"request", 3},
                            {"transaction", 4},
                            {"observed_ms", 1001},
                            {"order", 4}};
            },
            [] {
                return 100u;
            },
            [] {
                return 1001;
            });
    owner.poll();
    assert(launches == 1);
    bool malformed = false;
    try {
        owner.gesture(Json{{"kind", "pointer"},
                           {"display_id", 0},
                           {"points", Json::array({{{"x", 1920}, {"y", 0}, {"at_ms", 0}}})}});
    } catch (...) {
        malformed = true;
    }

    assert(malformed && commands == 0);
    auto wake =
            owner.gesture(Json{{"kind", "pointer"},
                               {"display_id", 0},
                               {"points", Json::array({{{"x", 100}, {"y", 100}, {"at_ms", 0}}})}});
    assert(wake && wake->at("sdk_completed") == false &&
           wake->at("display_wake_completed") == true && commands == 1);

    for (int i = 0; i < 100; i++) {
        assert(!owner.gesture(0));
    }

    assert(commands == 1 && launches == 1);
    bool rejected = false;
    try {
        owner.handle({{"session_id", "foreign"},
                      {"boot_id", "boot"},
                      {"action", "wake"},
                      {"display_id", 0}});
    } catch (...) {
        rejected = true;
    }

    assert(rejected && commands == 1);
    owner.close();
    assert(stops == 1 && owner.record().at("states").empty());
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
