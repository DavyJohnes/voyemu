#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

struct BlockingConnection;
struct Connection;

// Numeric IPv4 loopback only; adb_host_attach_emulator validates console_port.
std::unique_ptr<BlockingConnection> adb_host_connect_emulator(uint32_t console_port,
                                                              const std::atomic<bool> &stopping);
void adb_host_register_emulator(std::shared_ptr<Connection> connection, const std::string &serial);
