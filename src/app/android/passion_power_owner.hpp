#pragma once
#include "error.hpp"
#include "passion_session_power.hpp"
#include "passion_watch_clock.hpp"
#include "passion_watch_stream.hpp"
namespace voyemu::media {
// Session's existing lifecycle/owner mutex serializes every public call.
class PassionPowerOwner {
public:
    static Json capability() {
        return {{"schema", 1},
                {"protocol", "passion-display-power-v1"},
                {"profile", "passion-11-russian-20260403-2.1.4.80"},
                {"api", 30},
                {"sdk_input_sha256",
                 "c6992cdc2a3e23d5aeb6ab77e7a7f6889cfcf5c262c6ed86825e6fcbf601681d"},
                {"nexus_input_sha256",
                 "603805fbc8aa50ff473cd34d2082366bd5cfa6ed9439f37188e9301e1b812b11"},
                {"primary_unique_id", "local:8141241408256768"},
                {"secondary_unique_id", "virtual:com.android.emulator.multidisplay:1234562"}};
    }

    static bool enabled(const Json &specification) {
        if (!specification.contains("display_power_capability")) {
            return false;
        }

        if (specification.at("display_power_capability") != capability()) {
            throw VoyemuError("OEM_DISPLAY_POWER", "exact prepared graph capability required");
        }

        return true;
    }

    using Command = std::function<Json(const std::vector<std::string> &, int)>;
    using Clock = std::function<std::uint64_t()>;
    using GuestClock = std::function<std::int64_t()>;
    // Opt-in bounded causal trace (strict guard unchanged): one record per watch frame carrying the
    // complete admission triplet plus the projection/anchor state and the exact strict rejection.
    using Trace = std::function<void(const Json &)>;

    PassionPowerOwner(PassionPointerWake::Binding binding, PassionSessionPower::Fence fence,
                      PassionWatchStream::Launch launch, Command command, Clock clock,
                      GuestClock guest_clock, Trace trace = {})
        : binding_(std::move(binding)), command_(std::move(command)), clock_(std::move(clock)),
          guest_clock_(std::move(guest_clock)), trace_(std::move(trace)),
          power_(
                  binding_, fence,
                  [this](unsigned d, const std::string &boot) {
                      return originalWake(d, boot);
                  },
                  [this] {
                      watch_.recheck();
                  },
                  clock_),
          watch_(
                  binding_,
                  [this, launch = std::move(launch)](const std::vector<std::string> &args) {
                      if (!coarse_utc_) {
                          coarse_utc_ = guest_clock_();
                      }

                      const auto started = clock_();
                      projection_.launched(started);
                      trace_record_([&, this](Json &r) {
                          r["kind"] = "launched";
                          r["launch_host_monotonic_us"] = started;
                      });

                      return launch(args);
                  },
                  std::move(fence),
                  [this](const Json &event) {
                      const auto now = clock_();
                      const auto guest = event.at("guest_time_ms").get<std::int64_t>();

                      if (guest <= 0 ||
                          guest > std::numeric_limits<std::int64_t>::max() - 1000000) {
                          throw VoyemuError("PASSION_WATCH", "Watch guest clock range");
                      }

                      for (const auto &item : event.at("states")) {
                          const auto sample = item.at("observed_ms").get<std::int64_t>();

                          if (sample > guest + 250 || sample < guest - 1500) {
                              throw VoyemuError("PASSION_WATCH",
                                                "Watch sample outside guest envelope");
                          }
                      }

                      const auto verified = guest_clock_cached_();
                      const auto sequence = event.at("sequence").get<std::uint64_t>();
                      try {
                          const auto upper = projection_.event(guest, now, verified);
                          power_.event(event, now, upper);
                          trace_record_([&, this](Json &r) {
                              r["kind"] = "admitted";
                              r["sequence"] = sequence;
                              r["guest_time_ms"] = guest;
                              r["receipt_host_monotonic_us"] = now;
                              r["verified_guest_utc_ms"] = verified;
                              r["projection_upper_ms"] = upper;
                              r["projection_uncertainty_ms"] = projection_.uncertainty();
                          });
                      } catch (const std::exception &error) {
                          trace_record_([&, this](Json &r) {
                              r["kind"] = "rejected";
                              r["sequence"] = sequence;
                              r["guest_time_ms"] = guest;
                              r["receipt_host_monotonic_us"] = now;
                              r["verified_guest_utc_ms"] = verified;
                              r["projection_uncertainty_ms"] = projection_.uncertainty();
                              r["error"] = std::string(error.what());
                          });
                          throw;
                      }
                  },
                  [this] {
                      power_.lost();
                      projection_.reset();
                      coarse_utc_.reset();
                  },
                  [this](const std::string &raw) {
                      last_raw_event_ = raw;
                  }) {}

    void poll() {
        watch_.poll(clock_());
    }

    void close() {
        watch_.close();
        power_.lost();
    }

    Json record() const {
        return {{"schema", 1},
                {"protocol", "passion-display-power-v1"},
                {"session_id", binding_.session},
                {"boot_id", binding_.boot},
                {"watch_id", binding_.epoch},
                {"states", power_.record(clock_())},
                {"watch_diagnostic", power_.diagnostic()},
                {"last_raw_state_event", last_raw_event_},
                {"last_raw_state_maximum_bytes", 4096}};
    }

    // Only Session's closed operation dispatch calls this, using its retained endpoint.
    Json handle(const Json &operation) {
        if (operation.at("session_id") != binding_.session ||
            operation.at("boot_id") != binding_.boot) {
            throw VoyemuError("OEM_DISPLAY_POWER", "stale closed operation");
        }

        auto action = operation.at("action").get<std::string>();

        if (action == "recheck") {
            watch_.recheck();

            return {{"ok", true}, {"queued", true}, {"session_id", binding_.session}};
        }

        if (action != "wake") {
            throw VoyemuError("OEM_DISPLAY_POWER", "unsupported closed operation");
        }

        const auto &id = operation.at("display_id");

        if (!id.is_number_integer() || id.get<int>() < 0 || id.get<int>() > 1) {
            throw VoyemuError("OEM_DISPLAY_POWER", "invalid logical display");
        }

        return proof(power_.wake(id.get<unsigned>(), binding_.session, binding_.boot, clock_()));
    }

    std::optional<Json> gesture(const Json &input) {
        if (input.at("kind") != "pointer") {
            throw VoyemuError("OEM_DISPLAY_POWER", "pointer trajectory required");
        }

        auto integer = [](const Json &value, std::int64_t maximum) {
            if (!value.is_number_integer()) {
                throw VoyemuError("OEM_DISPLAY_POWER", "integer pointer field required");
            }

            auto n = value.get<std::int64_t>();

            if (n < 0 || n > maximum) {
                throw VoyemuError("OEM_DISPLAY_POWER", "pointer field out of bounds");
            }

            return n;
        };
        unsigned display = static_cast<unsigned>(integer(input.at("display_id"), 1));
        const auto &points = input.at("points");

        if (!points.is_array() || points.empty() || points.size() > 64) {
            throw VoyemuError("OEM_DISPLAY_POWER", "bounded pointer trajectory required");
        }

        std::int64_t previous = 0;

        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto &point = points[i];
            integer(point.at("x"), 1919);
            integer(point.at("y"), 719);
            auto at = integer(point.at("at_ms"), 2000);

            if ((i == 0 && at != 0) || at < previous) {
                throw VoyemuError("OEM_DISPLAY_POWER", "ordered pointer trajectory required");
            }

            previous = at;
        }

        return gesture(display);
    }

    // One complete API trajectory; false means original SDK route, true means consumed wake.
    std::optional<Json> gesture(unsigned display) {
        auto result = power_.gesture(display, clock_());

        if (!result) {
            return {};
        }

        auto reply = proof(*result);
        reply["sdk_completed"] = false;
        reply["display_wake_completed"] = true;
        reply["pointer_released"] = true;
        reply["visible_effect_verified"] = false;

        return reply;
    }

private:
    static Json proof(const PassionDisplayProof &p) {
        return {{"ok", true},
                {"kind", p.kind},
                {"session_id", p.session},
                {"boot_id", p.boot},
                {"watch_id", p.epoch},
                {"unique_id", p.unique},
                {"display_id", p.display},
                {"state", p.state},
                {"request", p.request},
                {"transaction", p.transaction},
                {"order", p.order},
                {"observed_ms", p.observed_ms},
                {"expires_monotonic_us", p.expires_us}};
    }

    // Trace is best-effort diagnostics only: it never changes guard decisions, is hard-bounded to
    // 32 records and 32KiB, and every record re-binds the session/watch/boot of this owner.
    void trace_record_(const std::function<void(Json &)> &fill) {
        if (!trace_ || trace_records_ >= 32) {
            return;
        }

        try {
            Json record = {{"schema", 1},
                           {"session_id", binding_.session},
                           {"watch_id", binding_.epoch},
                           {"boot_id", binding_.boot}};
            fill(record);
            const auto size = record.dump().size();

            if (size > 32768 || trace_bytes_ + size > 32768) {
                return;
            }

            trace_bytes_ += size;
            ++trace_records_;
            trace_(std::move(record));
        } catch (const std::exception
                         &) { /* Ignored: trace is best-effort; guard decisions are unaffected */
        } catch (...) {       /* Ignored: trace is best-effort; guard decisions are unaffected. */
        }
    }

    // verified_guest_utc_ms is a coarse anchor used by PassionWatchClock ONLY on the first admitted
    // frame (uncertainty_ derives from receipt-launch, not from it). verified_guest_utc_ms is
    // otherwise unused, so calling the synchronous ADB guest clock on EVERY buffered event is
    // redundant AND harmful: PassionWatchStream::drain dispatches accept_ over all buffered lines
    // in one owner tick, so a slow ADB read inside one accept_ blocks dispatch of already-queued
    // events and their now=clock_() receipts stamp late. Measured authentic run: one launch->first
    // receipt 107ms, but a single next-event receipt 2650ms (a stalled guest_clock_), inflating lag
    // beyond the strict 1500ms envelope for a fresh event. Cache the coarse clock for the whole
    // SESSION INCARNATION (this owner), read ONCE before
    // started=clock_() so its latency never lands between launched() and any frame receipt. The
    // read is guarded by if(!coarse_utc_) and MUST NOT be re-taken on an overlapping child
    // rotation: the 18s rotation retains the previous child, and an unconditional pre-launch
    // re-read (a) stalls the tick, delaying the retained slot's queued lines, and (b) manufactures
    // a fresh anchor mid-incarnation, so genuinely fresh retained-slot frames reject. Measured
    // fixture (rotation pair, single-line diff): unconditional rotation read -> calls=2, retained
    // seq3 rejected; cached incarnation -> calls=1, both new and retained slots admitted under the
    // SAME strict guard. coarse_utc_ resets ONLY at lost() and at a new session incarnation (new
    // owner); the guest clock is guest-wide, not per watch child. verified_guest_utc_ms is checked
    // only by the first-frame coarse-anchor bound, so reusing it for later events in the same
    // generation does not weaken any strict check: receipt now= is still captured at dispatch
    // (never manufactured), and guest-vs-monotonic lag, sample envelope, first-frame coarse bound,
    // expiry, and session/watch/boot identity are all UNCHANGED. A stale/backlogged event whose
    // guest_time_ms lags the monotonic projection by >1500ms still rejects; foreign/changed-
    // incarnation events never reach here.
    std::int64_t guest_clock_cached_() {
        if (!coarse_utc_) {
            coarse_utc_ = guest_clock_();
        }

        return *coarse_utc_;
    }

    std::string last_raw_event_;
    std::function<void(const Json &)> trace_;
    unsigned trace_records_{}, trace_bytes_{};
    std::optional<std::int64_t> coarse_utc_;

    PassionDisplayProof originalWake(unsigned display, const std::string &boot) {
        const auto sent = clock_();
        const auto sent_guest_upper = projection_.upper(sent);
        const auto reply = command_(
                {"shell", "CLASSPATH=/system/priv-app/MegaNexusService/MegaNexusService.apk",
                 "app_process", "/system/bin", "com.voyemu.display.OemDisplayPower", "wake",
                 std::to_string(display), boot},
                3500);

        if (!reply.value("ok", false) || reply.at("kind") != "wake_observed" ||
            reply.at("boot_id") != boot || reply.at("display").get<unsigned>() != display) {
            throw VoyemuError("OEM_DISPLAY_POWER", "original Nexus wake proof absent");
        }

        const auto received = clock_();

        if (received < sent || received - sent > 3500000) {
            throw VoyemuError("OEM_DISPLAY_POWER", "wake interval invalid");
        }

        auto sampled = reply.at("observed_ms").get<std::int64_t>(),
             guest_now = projection_.upper(received);

        if (sampled <
            sent_guest_upper - static_cast<std::int64_t>(projection_.uncertainty()) - 250) {
            throw VoyemuError("OEM_DISPLAY_POWER", "wake proof predates request");
        }

        if (sampled <= 0 || sampled < guest_now - 1500 || sampled > guest_now + 250) {
            throw VoyemuError("OEM_DISPLAY_POWER", "wake sample stale");
        }

        auto age = guest_now > sampled ? static_cast<std::uint64_t>(guest_now - sampled) : 0;

        return {binding_.session,
                boot,
                binding_.epoch,
                reply.at("unique_id").get<std::string>(),
                "wake_observed",
                display,
                reply.at("state").get<int>(),
                reply.at("request").get<std::uint64_t>(),
                reply.at("transaction").get<std::uint64_t>(),
                clock_() + (2500 - age) * 1000,
                reply.at("order").get<std::uint64_t>(),
                sampled};
    }

    PassionWatchClock projection_;
    PassionPointerWake::Binding binding_;
    Command command_;
    Clock clock_;
    GuestClock guest_clock_;
    PassionSessionPower power_;
    PassionWatchStream watch_;
};
} // namespace voyemu::media
