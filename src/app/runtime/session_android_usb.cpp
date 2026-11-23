#include "android_usb_drive.hpp"
#include "session_internal.hpp"
#include "usb_drive_image.hpp"

// The USB drive of the multimedia guest: QMP hotplug of the usb-storage device and the guest
// volume that vold mounts from it.
namespace voyemu {
using namespace session_detail;
namespace {
using Clock = std::chrono::steady_clock;

int remaining_ms(Clock::time_point deadline) {
    return static_cast<int>(std::max<std::int64_t>(
            1, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())
                       .count()));
}

// One QMP conversation: the greeting, capability negotiation, then commands whose replies may
// be preceded by asynchronous events, which are kept for wait_event.
class QmpChannel {
public:
    QmpChannel(const Json &endpoint, Clock::time_point deadline)
        : stream_(qmp_connect(endpoint, remaining_ms(deadline))) {
        if (!read(deadline).contains("QMP")) {
            throw VoyemuError("USB_DRIVE", "QMP greeting missing");
        }

        execute("qmp_capabilities", nullptr, deadline);
    }

    // The "return" value; a QEMU error is a USB_DRIVE failure with its description.
    Json execute(const std::string &command, const Json &arguments, Clock::time_point deadline) {
        Json message{{"execute", command}};

        if (!arguments.is_null()) {
            message["arguments"] = arguments;
        }

        stream_->write(message.dump() + "\n", remaining_ms(deadline));

        for (;;) {
            auto reply = read(deadline);

            if (reply.contains("event")) {
                events_.push_back(std::move(reply));
            } else if (reply.contains("return")) {
                return reply.at("return");
            } else if (reply.contains("error")) {
                throw VoyemuError("USB_DRIVE",
                                  command + " rejected: " +
                                          reply.at("error").value("desc", std::string("error")));
            } else {
                throw VoyemuError("USB_DRIVE_UNKNOWN", "malformed QMP reply");
            }
        }
    }

    // Whether DEVICE_DELETED for the device arrived before the deadline.
    bool device_deleted(std::string_view id, Clock::time_point deadline) {
        const auto matches = [&](const Json &event) {
            return event.value("event", std::string{}) == "DEVICE_DELETED" &&
                   event.value("data", Json::object()).value("device", std::string{}) == id;
        };

        while (true) {
            if (std::any_of(events_.begin(), events_.end(), matches)) {
                return true;
            }

            if (Clock::now() >= deadline) {
                return false;
            }

            events_.push_back(read(deadline));
        }
    }

private:
    Json read(Clock::time_point deadline) {
        auto value = Json::parse(stream_->read_line(remaining_ms(deadline)), nullptr, false);

        if (!value.is_object()) {
            throw VoyemuError("USB_DRIVE_UNKNOWN", "malformed QMP message");
        }

        return value;
    }

    std::unique_ptr<QmpStream> stream_;
    std::vector<Json> events_;
};

std::string trimmed(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }

    return text;
}
} // namespace

Json Session::usb_drive(bool attached) {
    if (android_spec_.empty()) {
        throw VoyemuError("TEST_UNSUPPORTED", "no prepared multimedia");
    }

    if (stopped_ || test_services_closed_ || !android_powered_ || android_qmp_.is_null()) {
        throw VoyemuError("USB_DRIVE", "multimedia is not running");
    }

    const auto changed = attached != usb_drive_attached_;

    if (changed) {
        plug_usb_drive(attached);
    }

    return {{"attached", attached},
            {"changed", changed},
            {"android_generation", android_generation_}};
}

void Session::plug_usb_drive(bool attached) {
    const auto image = std::filesystem::path(android_spec_.at("machine").get<std::string>()) /
                       "usb-drive.img";

    // The drive keeps the writes of the guest like a real stick; a machine gets the test set on
    // its first attach.
    if (attached && !std::filesystem::exists(image)) {
        usb_drive::write_test_drive(image);
    }

    const auto deadline = Clock::now() + std::chrono::seconds(10);
    std::optional<QmpChannel> channel;
    try {
        channel.emplace(android_qmp_, deadline);
    } catch (const std::exception &error) {
        throw VoyemuError("USB_DRIVE", "QMP channel unavailable: " + std::string(error.what()));
    }

    // Past this point a lost channel leaves the port in an unknown state.
    try {
        if (attached) {
            const auto added = channel->execute("human-monitor-command",
                                                {{"command-line", usb_drive_add_command(image)}},
                                                deadline);

            if (trimmed(added.get<std::string>()) != "OK") {
                throw VoyemuError("USB_DRIVE",
                                  "drive image rejected: " + trimmed(added.get<std::string>()));
            }

            try {
                channel->execute("device_add", usb_drive_device_arguments(), deadline);
            } catch (const VoyemuError &error) {
                if (!std::string_view(error.what()).starts_with("USB_DRIVE:")) {
                    throw;
                }

                (void)channel->execute(
                        "human-monitor-command",
                        {{"command-line", "drive_del " + std::string(usb_drive_block_id)}},
                        deadline);
                throw;
            }
        } else {
            channel->execute("device_del", {{"id", usb_drive_device_id}}, deadline);

            if (!channel->device_deleted(usb_drive_device_id, deadline)) {
                throw VoyemuError("USB_DRIVE_UNKNOWN", "device removal was not confirmed");
            }
        }
    } catch (const VoyemuError &error) {
        const std::string_view reason(error.what());

        if (reason.starts_with("USB_DRIVE:") || reason.starts_with("USB_DRIVE_UNKNOWN:")) {
            throw;
        }

        throw VoyemuError("USB_DRIVE_UNKNOWN", "QMP channel lost: " + std::string(reason));
    } catch (const Json::exception &) {
        throw VoyemuError("USB_DRIVE_UNKNOWN", "malformed QMP reply");
    }

    usb_drive_attached_ = attached;
    deliveries_["android"]["usb_drive"] = {{"attached", attached}};
    logging::write(logging::Level::info, "android", attached ? "usb.attached" : "usb.detached",
                   {{"generation", android_generation_}});
}

std::optional<std::string> Session::usb_drive_volume_state(Clock::time_point deadline) {
    if (!android_transport_ready_ || Clock::now() >= deadline) {
        return std::nullopt;
    }

    const auto listed = adb({"shell", "sm", "list-volumes", "public"},
                            std::min(5000, remaining_ms(deadline)));

    if (!listed.timed_out && !listed.exit_code) {
        if (const auto state = usb_volume_state(listed.output, usb_drive_volume_uuid)) {
            return state;
        }
    }

    if (Clock::now() >= deadline) {
        return std::nullopt;
    }

    const auto mounts = adb({"shell", "cat", "/proc/mounts"},
                            std::min(5000, remaining_ms(deadline)));

    if (mounts.timed_out || mounts.exit_code) {
        return std::nullopt;
    }

    return usb_volume_mount_state(mounts.output, usb_drive_volume_uuid);
}
} // namespace voyemu
