#include "api.hpp"
#include "api_test_host.hpp"
#include "guest_android_fake.hpp"
#include "model.hpp"
#include "platform_file.hpp"
#include "profile_catalog.hpp"
#include "qinggan11_startup.hpp"
#include "session.hpp"
#include "session_recovery.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace voyemu;

void image_dimensions(const std::filesystem::path &path, std::size_t width, std::size_t height) {
    auto name = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    auto url = CFURLCreateWithFileSystemPath(nullptr, name, kCFURLPOSIXPathStyle, false);

    CFRelease(name);
    auto source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    assert(source);
    auto image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    assert(image && CGImageGetWidth(image) == width && CGImageGetHeight(image) == height);
    CGImageRelease(image);
}

void owned_recovery_tests(const std::filesystem::path &, const std::filesystem::path &) {}

void wheel_release_admission_tests(const std::filesystem::path &base) {
    const auto root = base / ("test-wheel-cancel-admission-" + random_token());

    ensure_private_directory(root);
    const std::string name = qinggan11_wheel_profile(), id = random_token(), owner = random_token();
    const auto directory = root / ".local/machines" / name / "sessions" / id;
    ensure_private_directory(directory);
    Session session(directory, id);
    auto select = [&](const std::string &selected, const std::string &epoch) {
        const auto path = root / ".local/machines" / selected / "sessions" / epoch;
        ensure_private_directory(path);
        write_private_json(root / ".local/machines" / selected / "active" / (selected + ".json"),
                           {{"schema", 1},
                            {"session_id", epoch},
                            {"directory", path.string()},
                            {"endpoint", "owned-cancel-fixture"}});
        write_private_json(path / "automation-owner.json",
                           {{"schema", 1}, {"session_id", epoch}, {"ownership_id", owner}});
    };
    select(name, id);
    const std::string other = qinggan09_profile(), other_id = random_token();
    select(other, other_id);
    auto request = [&](const char *op) {
        return Json{{"schema", 1},
                    {"request_id", random_token()},
                    {"op", op},
                    {"profile", name},
                    {"session", {{"id", id}, {"ownership_id", owner}}}};
    };
    std::mutex mutex;
    std::condition_variable condition;
    bool started = false, resume = false;
    unsigned releases = 0, accepted_ups = 0;
    api::Transports transport;
    transport.control = [&](const auto &, const Json &command, int) -> Json {
        const auto op = command.at("op").get<std::string>();

        if (op == "status") {
            auto reply = session.handle(command);
            reply["wheel_capability"] =
                    qinggan11_partial_wheel_capability(qinggan11_wheel_profile());

            return reply;
        }

        if (op == "wheel_pair") {
            {
                std::unique_lock lock(mutex);
                started = true;
                condition.notify_all();
                condition.wait(lock, [&] {
                    return resume;
                });
            }

            return Json{{"ok", true},
                        {"session_id", id},
                        {"down", {{"accepted", true}}},
                        {"up", {{"accepted", true}}},
                        {"released", true},
                        {"guest_acknowledged", false},
                        {"visible_effect_verified", false}};
        }

        auto reply = session.handle(command);

        if (op == "wheel" && command.at("phase") == "down") {
            std::unique_lock lock(mutex);
            started = true;
            condition.notify_all();
            condition.wait(lock, [&] {
                return resume;
            });
        }

        if (op == "release_owner") {
            ++releases;
        }

        if (op == "wheel" && command.at("phase") == "up" && reply.value("ok", false)) {
            ++accepted_ups;
        }

        return reply;
    };
    api::Api pressing(root, "unused-cli", transport), canceling(root, "unused-cli", transport);
    auto busy = [&](const Json &value) {
        const auto result = canceling.call(value);
        assert(!result.at("ok").get<bool>() && result.at("error").at("code") == "TEST_BUSY");
    };
    Json press_result;
    auto press = request("input.wheel.press");
    press["button"] = "volume_down";
    press["hold_ms"] = 201;
    auto unowned = press;
    unowned["request_id"] = random_token();
    unowned["session"]["ownership_id"] = random_token();
    assert(pressing.call(unowned).at("ok") == false && !started &&
           !std::filesystem::exists(directory / "automation/wheel-writer.json"));
    std::thread held([&] {
        press_result = pressing.call(press);
    });
    {
        std::unique_lock lock(mutex);
        assert(condition.wait_for(lock, std::chrono::seconds(2), [&] {
            return started;
        }));
    }
    auto capture = request("session.screenshot");
    busy(capture);
    auto foreign = request("input.wheel.release");
    foreign["session"]["ownership_id"] = random_token();
    const auto rejected = canceling.call(foreign);
    assert(!rejected.at("ok").get<bool>() && releases == 0);
    auto other_session = request("input.wheel.release");
    other_session["profile"] = other;
    other_session["session"]["id"] = other_id;
    const auto independent = canceling.call(other_session);
    assert(independent.value("error", Json::object()).value("code", std::string{}) != "TEST_BUSY");
    const auto marker = directory / "automation/wheel-writer.json",
               lease = directory / "automation/wheel-writer.lock";
    const auto valid_marker = read_private_json(marker);
    auto denied = [&] {
        const auto before = releases;
        assert(canceling.call(request("input.wheel.release")).at("ok") == false &&
               releases == before);
    };

    for (const auto &patch : Json::array({Json{{"schema", 1.0}}, Json{{"unexpected", true}},
                                          Json{{"operation_id", "../outside"}},
                                          Json{{"operation_id", std::string(32, 'A')}},
                                          Json{{"ownership_sha256", std::string(64, 'f')}},
                                          Json{{"session_id", random_token()}}})) {
        auto invalid = valid_marker;
        invalid.update(patch);
        write_private_json(marker, invalid);
        denied();
        write_private_json(marker, valid_marker);
    }

    for (const auto mode :
         {std::filesystem::perms::owner_read,
          std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                  std::filesystem::perms::group_read | std::filesystem::perms::others_read}) {
        for (const auto &path : {marker, lease}) {
            std::filesystem::permissions(path, mode);
            denied();
            std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                                       std::filesystem::perms::owner_write);
        }
    }

    for (const auto &path : {marker, lease}) {
        const auto saved = path.string() + ".saved";
        std::filesystem::rename(path, saved);
        std::filesystem::create_symlink(saved, path);
        denied();
        std::filesystem::remove(path);
        std::filesystem::rename(saved, path);
    }

    const auto pending = root / ".local/build/automation/operations" /
                         (valid_marker.at("operation_id").get<std::string>() + ".json");
    const auto original_pending = read_private_json(pending);
    auto terminal = original_pending;
    terminal["status"] = "terminal";
    write_private_json(pending, terminal);
    denied();
    write_private_json(pending, original_pending);
    const auto released = canceling.call(request("input.wheel.release"));
    {
        std::lock_guard lock(mutex);
        resume = true;
        condition.notify_all();
    }
    held.join();
    // Real admission/FileLock overlap, rather than direct transport calls, must permit the owner's
    // cancellation.
    assert(released.at("ok") == true && releases >= 1 && accepted_ups == 0);
    assert(!press_result.at("ok").get<bool>() &&
           press_result.at("error").at("code") == "TEST_INPUT_CANCELLED_UNKNOWN" &&
           press_result.at("error").at("outcome") == "unknown");
    // A retained marker plus an old client/capture writer without the live wheel lease grants no
    // bypass.
    {
        api::FileLock capture_writer(directory / "automation/writer.lock");
        const auto before = releases;
        busy(request("input.wheel.release"));
        assert(releases == before);
    }
    started = false;
    resume = false;
    press = request("input.wheel.press");
    press["button"] = "volume_down";
    press["hold_ms"] = 100;
    std::thread short_press([&] {
        press_result = pressing.call(press);
    });
    {
        std::unique_lock lock(mutex);
        assert(condition.wait_for(lock, std::chrono::seconds(2), [&] {
            return started;
        }));
    }
    const auto before = releases;
    busy(request("input.wheel.release"));
    assert(releases == before);
    {
        std::lock_guard lock(mutex);
        resume = true;
        condition.notify_all();
    }
    short_press.join();
    assert(press_result.at("ok") == true);
    session.stop();
    std::filesystem::remove_all(root);
}

ConnectedPipe::ConnectedPipe(const std::filesystem::path &) {
    throw std::logic_error("named pipes are Windows endpoints");
}

ConnectedPipe::~ConnectedPipe() = default;

void ConnectedPipe::close() {}
