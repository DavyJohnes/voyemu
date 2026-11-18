#pragma once
#include <filesystem>
#include <memory>
#include <string>

namespace voyemu::frames {
class GrpcServer {
public:
    GrpcServer(const std::filesystem::path &frame, std::string token);
    ~GrpcServer();
    GrpcServer(const GrpcServer &) = delete;
    GrpcServer &operator=(const GrpcServer &) = delete;

    unsigned port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace voyemu::frames
