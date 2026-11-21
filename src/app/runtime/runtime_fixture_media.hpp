#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>

// Host fixture only: an authenticated, retained fake SDK serves synthetic pixels.
// Nothing in runtime_fixture_media_*.cpp is linked into the product or accepts a real SDK target.
class FixtureSdkMedia {
public:
    virtual ~FixtureSdkMedia() = default;
};
// The fake SDK at gRPC port grpc for the engine at console port serial and process pid on Windows,
// where the multimedia frame comes from the SDK; none on the other hosts.
std::unique_ptr<FixtureSdkMedia> start_fixture_sdk_media(const std::filesystem::path &root,
                                                         unsigned grpc, unsigned serial,
                                                         std::int64_t pid);
