#include "qnx_boot_progress.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace voyemu;
namespace {
void put(std::string &value, std::size_t offset, std::uint64_t number, unsigned bytes = 8) {
    for (unsigned i = 0; i < bytes; ++i) {
        value[offset + i] = char(number >> (8 * i));
    }
}

struct Memory {
    std::map<std::uint64_t, std::string> regions;
    bool race = false, thread_race = false, slot_race = false, vector_race = false,
         slow_procnto = false, connection_race = false, connection_pointer_race = false,
         parent_race = false, context_race = false, context_state_race = false,
         context_connection_race = false, user_root_race = false, user_table_race = false,
         cached_before_remap = false, cached_after_remap = false, pending_remap = false;
    unsigned process_reads = 0, thread_reads = 0, slot_reads = 0, vector_reads = 0,
             connection_reads = 0, extended_thread_reads = 0, parent_reads = 0, context_reads = 0,
             context_header_reads = 0, context_connection_reads = 0, user_root_reads = 0,
             user_descriptor_reads = 0, cache_process_reads = 0;

    explicit Memory(std::string_view fixture = {}) {
        auto &root = regions[0x50d1f070];
        root.resize(16);
        put(root, 8, 0x50000003);
        auto &table = regions[0x50000000];
        table.resize(4096);
        put(table, 8, 0x40000401);
        put(table, 16, 0x80000401);
        regions[0x600b7d48].resize(0x48);
        regions[0x600726f8].resize(0x24);

        if (!fixture.empty()) {
            regions[0x600b7d48] = std::string(fixture.substr(0x57d48, 0x48));
            regions[0x600726f8] = std::string(fixture.substr(0x126f8, 0x24));

            for (const auto &[at, offset, count] :
                 std::array<std::array<std::uint64_t, 3>, 17>{{{0x600a42b4, 0x442b4, 0x18},
                                                               {0x600ca7e0, 0x6a7e0, 0x4c},
                                                               {0x600b32b8, 0x532b8, 0x38},
                                                               {0x600b4418, 0x54418, 0x14},
                                                               {0x600b32f0, 0x532f0, 0xc},
                                                               {0x600b3410, 0x53410, 0x18},
                                                               {0x601133b8, 0xb33b8, 0xe},
                                                               {0x600b35c8, 0x535c8, 0x38},
                                                               {0x600b2f84, 0x52f84, 0x10},
                                                               {0x600a301c, 0x4301c, 0x68},
                                                               {0x600b3330, 0x53330, 0x14},
                                                               {0x6008bf88, 0x2bf88, 0x24},
                                                               {0x601128c8, 0xb28c8, 8},
                                                               {0x6008a2a8, 0x2a2a8, 0x2c},
                                                               {0x6009a18c, 0x3a18c, 0x34},
                                                               {0x60098f00, 0x38f00, 0xbc},
                                                               {0x60098948, 0x38948, 0x54}}}) {
                regions[at] = std::string(fixture.substr(offset, count));
            }
        }

        auto &vector = regions[0x60126320];
        vector.resize(24);
        put(vector, 8, 0xffffff8081000000ULL);
        put(vector, 16, 4, 2);
        auto &slots = regions[0x81000000];
        slots.resize(32);
        put(slots, 0, 3);
        put(slots, 8, 0xffffff8081100000ULL);
        put(slots, 16, 0xffffff8081101000ULL);

        for (unsigned i = 0; i < 2; ++i) {
            auto &process = regions[0x81100000 + i * 0x1000];
            process.resize(512);
            put(process, 8, i ? 4098 : 1, 4);
            put(process, 0x1a0,
                0xffffff8081200000ULL + static_cast<unsigned long long>(i) * 0x1000);
            auto &name = regions[0x81200000 + i * 0x1000];
            name = i ? "bin/on" : "proc/boot/procnto-smp-instr";
            name.resize(128);
        }
    }

    void add_threads() {
        put(regions[0x81101000], 0xa8, 0xffffff8081300000ULL);
        put(regions[0x81101000], 0xb0, 2, 2);
        auto &slots = regions[0x81300000];
        slots.resize(16);
        put(slots, 0, 0xffffff8081400002ULL);
        put(slots, 8, 1);
        auto &thread = regions[0x81400000];
        thread.resize(0xec);
        put(thread, 0x48, 0xffffff8081101000ULL);
        put(thread, 0x13, 6, 1);
    }

    void add_peer() {
        put(regions[0x81400000], 0xa0, 0xffffff8081500000ULL);
        put(regions[0x81400000], 0xe8, 7, 4);
        auto &connection = regions[0x81500000];
        connection.resize(0x34);
        put(connection, 0x2c, 1, 4);
        put(connection, 0x30, 2, 4);
        put(regions[0x81101000], 0x98, 0xffffff8081100000ULL);
    }

    void add_saved_context() {
        auto &thread = regions[0x81400000];
        thread.resize(0x420);
        put(thread, 0x410, 0x3524850540ULL);
        put(thread, 0x400, 0x352482ca7cULL);
        put(thread, 0x408, 0x7fffff0000ULL);
        put(thread, 0x310, 0x40000000, 8);
        put(thread, 0x318, 0x7fffff0100ULL);
        put(thread, 0x320, 0xffffffffffffffe8ULL);
    }

    void add_user_space(std::string_view libc, bool cond_caller = false,
                        std::string_view msgque = {}) {
        put(regions[0x81400000], 0x400, 0x352482ca78ULL);
        put(regions[0x81400000], 0xe8, 0x40000000, 4);
        put(regions[0x81500000], 0x30, 1, 4);
        auto &generic = regions[0x60125890];
        generic.resize(8);
        put(generic, 0, 0xffffff806008a2a8ULL);
        auto &dispatch_pointer = regions[0x6011c010];
        dispatch_pointer.resize(8);
        put(dispatch_pointer, 0, 0xffffff8081700000ULL);
        auto &dispatch = regions[0x81700000];
        dispatch.resize(24);
        put(dispatch, 0, 0xffffff8060098f00ULL);
        put(dispatch, 16, 0xffffff8060098948ULL);
        put(regions[0x81101000], 0x10, 0xffffff8081701000ULL);
        auto &memory = regions[0x81701000];
        memory.resize(0x70);
        put(memory, 0x68, 0xffffff8081101000ULL);
        put(memory, 0x60, 0xffffff8081702000ULL);
        auto &arch = regions[0x81702000];
        arch.resize(0x20);
        put(arch, 8, 0xffffff8081101000ULL);
        put(arch, 0x18, 0x1000053000000ULL);
        regions[0x53000000].resize(4096);
        std::uint64_t next = 0x53001000;
        std::map<std::uint64_t, std::uint64_t> l2, l3;
        auto map_page = [&](std::uint64_t va, std::uint64_t pa) {
            if (!l2.contains(va >> 30)) {
                l2[va >> 30] = next;
                regions[next].resize(4096);
                next += 4096;
            }

            put(regions[0x53000000], ((va >> 30) & 511) * 8, l2[va >> 30] | 3);

            if (!l3.contains(va >> 21)) {
                l3[va >> 21] = next;
                regions[next].resize(4096);
                next += 4096;
            }

            put(regions[l2[va >> 30]], ((va >> 21) & 511) * 8, l3[va >> 21] | 3);
            put(regions[l3[va >> 21]], ((va >> 12) & 511) * 8, pa | 0x403);
        };

        for (const auto &[va, pa, offset, count] : std::array<std::array<std::uint64_t, 4>, 2>{
                     {{0x3524850530ULL, 0x84000000, 0x50530, 28},
                      {0x352482ca28ULL, 0x84001000, 0x2ca28, 112}}}) {
            map_page(va, pa);
            auto &page = regions[pa];
            page.resize(4096);
            page.replace(va & 4095, count, libc.substr(offset, count));
        }

        map_page(0x7fffff0100ULL, 0x84002000);
        auto &message = regions[0x84002000];
        message.resize(4096);
        put(message, 0x100, 0x10041, 4);
        put(message, 0x104, 5, 4);
        put(message, 0x108, 0x3524800000ULL);
        put(message, 0x110, 4096);

        if (cond_caller) {
            regions[0x81201000] = "usr/bin/rpcd";
            regions[0x81201000].resize(128);
            put(regions[0x81400000], 0x13, 14, 1);
            put(regions[0x81400000], 0x410, 0x35248501b8ULL);
            put(regions[0x81400000], 0x400, 0x3524831cecULL);
            put(regions[0x81400000], 0x408, 0x7fffff0100ULL);
            put(regions[0x81400000], 0x3f8, 0x7fffff0100ULL);

            for (const auto &[va, pa, offset] : std::array<std::array<std::uint64_t, 3>, 4>{
                         {{0x35248501a8ULL, 0x84003000, 0x501a8},
                          {0x3524831cdcULL, 0x84004000, 0x31cdc},
                          {0x3524860000ULL, 0x84005000, 0x2ca28},
                          {0x3524832cdcULL, 0x84006000, 0x31cdc}}}) {
                map_page(va, pa);
                regions[pa].resize(4096);
                regions[pa].replace(va & 4095, 32, libc.substr(offset, 32));
            }

            put(message, 0x100, 0x7fffff0200ULL);
            put(message, 0x108, 0x3524860010ULL);

            if (!msgque.empty()) {
                map_page(0x3524861210ULL, 0x84007000);
                regions[0x84007000].resize(4096);
                regions[0x84007000].replace(0x210, 24, msgque.substr(0x1210, 24));
                regions[0x84007000].replace(0x298, 32, msgque.substr(0x1298, 32));
                put(message, 0x100, 0x7fffff0110ULL);
                put(message, 0x108, 0x35248612a8ULL);
                put(message, 0x110, 0x7fffff0140ULL);
                put(message, 0x118, 0x3524860010ULL);
            }
        }
    }

    void add_procnto_thread() {
        put(regions[0x81100000], 0xa8, 0xffffff8081301000ULL);
        put(regions[0x81100000], 0xb0, 1, 2);
        auto &slots = regions[0x81301000];
        slots.resize(8);
        put(slots, 0, 0xffffff8081410000ULL);
        auto &thread = regions[0x81410000];
        thread.resize(0x58);
        put(thread, 0x48, 0xffffff8081100000ULL);
        put(thread, 0x13, 5, 1);
    }

    bool read(std::uint64_t address, std::span<std::byte> output) {
        if (slow_procnto && address == 0x81410000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(105));
        }

        assert(address >= 0x40000000 && address - 0x40000000 <= 0x200000000ULL - output.size() &&
               output.size() <= 0x40000);
        auto at = regions.upper_bound(address);

        if (at == regions.begin()) {
            return false;
        }

        --at;

        if (address - at->first > at->second.size() ||
            output.size() > at->second.size() - (address - at->first)) {
            return false;
        }

        std::copy_n(reinterpret_cast<const std::byte *>(at->second.data() + address - at->first),
                    output.size(), output.data());

        if ((cached_before_remap || cached_after_remap) && address == 0x81101000 &&
            output.size() == 512) {
            ++cache_process_reads;

            if (cached_after_remap && cache_process_reads == 2) {
                put(regions[0x50000000], 16, 0xc0000401);
            }
        }

        if (cached_before_remap && address == 0x81201000) {
            pending_remap = true;
        }

        if (pending_remap && address == 0x50000010) {
            put(regions[0x50000000], 16, 0xc0000401);
            pending_remap = false;
        }

        if (user_root_race && address == 0x81702000 && ++user_root_reads == 2) {
            output[0x18] ^= std::byte{16};
        }

        if (user_table_race && address >= 0x53000000 && address < 0x53001000 &&
            ++user_descriptor_reads == 2) {
            output[0] ^= std::byte{1};
        }

        if (context_race && address == 0x81400310 && ++context_reads == 2) {
            output[0x100] ^= std::byte{4};
        }

        if (context_state_race && address == 0x81400000 && output.size() == 0xec &&
            ++context_header_reads == 3) {
            output[0x13] ^= std::byte{1};
        }

        if (context_connection_race && address == 0x81500000 && ++context_connection_reads == 3) {
            output[0x2c] ^= std::byte{1};
        }

        if (connection_race && address == 0x81500000 && ++connection_reads == 2) {
            output[0x2c] ^= std::byte{1};
        }

        if (connection_pointer_race && address == 0x81400000 && output.size() == 0xec &&
            ++extended_thread_reads == 2) {
            output[0xa0] ^= std::byte{8};
        }

        if (parent_race && address == 0x81100000 && output.size() == 12 && ++parent_reads == 2) {
            output[8] ^= std::byte{1};
        }

        if (race && address == 0x81101000 && ++process_reads == 2) {
            output[8] ^= std::byte{1};
        }

        if (thread_race && address == 0x81400000 && ++thread_reads == 2) {
            output[0x13] ^= std::byte{1};
        }

        if (slot_race && address == 0x81000010 && ++slot_reads == 3) {
            output[0] ^= std::byte{1};
        }

        if (vector_race && address == 0x60126320 && ++vector_reads == 3) {
            output[8] ^= std::byte{1};
        }

        return true;
    }

    Json sample() {
        return read_qnx_boot_progress([&](auto address, auto output) {
            return read(address, output);
        });
    }
};
} // namespace

int main(int argc, char **argv) try {
    Memory synthetic;

    assert(synthetic.sample()["status"] == "unknown");
    {
        Memory ring;
        auto &raw = ring.regions[0x50010000];
        raw.resize(32 + 1024);
        put(raw, 0, 0xbeefeeee, 4);
        put(raw, 4, 1024, 4);
        put(raw, 8, 4, 4);
        raw.replace(32, 4, "tail");
        raw.replace(32 + 1020, 4, "head");
        auto decoded = ring.sample();
        assert(decoded["ring_console"] == "headtail" &&
               decoded["ring_snapshot_status"] == "stable_header");
        unsigned headers = 0;
        auto raced = read_qnx_boot_progress([&](auto at, auto out) {
            auto ok = ring.read(at, out);

            if (at == 0x50010000 && ++headers == 2) {
                out[8] ^= std::byte{1};
            }

            return ok;
        });
        assert(!raced.contains("ring_console") &&
               raced["ring_snapshot_status"] == "header_changed");
        put(raw, 4, 0x40000, 4);
        assert(!ring.sample().contains("ring_console"));
    }

    auto translate = [&](Memory &memory, std::uint64_t address) {
        return qnx_boot_candidate_physical(
                [&](auto at, auto output) {
                    return memory.read(at, output);
                },
                address);
    };
    assert(translate(synthetic, 0xffffff80600b7d48ULL) == 0x600b7d48);
    assert(!translate(synthetic, 0x600b7d48));
    Memory invalid;
    put(invalid.regions[0x50d1f070], 8, 0);
    assert(!translate(invalid, 0xffffff80600b7d48ULL));
    Memory bad_table;
    put(bad_table.regions[0x50d1f070], 8, 0x30000003);
    assert(!translate(bad_table, 0xffffff80600b7d48ULL));
    Memory cycle;
    put(cycle.regions[0x50000000], 8, 0x50000003);
    assert(!translate(cycle, 0xffffff80600b7d48ULL));
    Memory misaligned;
    put(misaligned.regions[0x50000000], 8, 0x40001401);
    assert(!translate(misaligned, 0xffffff80600b7d48ULL));
    Memory pages;
    put(pages.regions[0x50000000], 8, 0x50001003);
    auto &l2 = pages.regions[0x50001000];
    l2.resize(4096);
    put(l2, 0x100 * 8, 0x50002003);
    auto &l3 = pages.regions[0x50002000];
    l3.resize(4096);
    put(l3, 0xb7 * 8, 0x600b7403);
    assert(translate(pages, 0xffffff80600b7d48ULL) == 0x600b7d48);
    Memory blocks;
    put(blocks.regions[0x50000000], 8, 0x50001003);
    auto &block_l2 = blocks.regions[0x50001000];
    block_l2.resize(4096);
    put(block_l2, 0x100 * 8, 0x60000401);
    assert(translate(blocks, 0xffffff80600b7d48ULL) == 0x600b7d48);
    Memory no_access;
    put(no_access.regions[0x50000000], 8, 0x40000001);
    assert(!translate(no_access, 0xffffff80600b7d48ULL));
    Memory root_block;
    put(root_block.regions[0x50d1f070], 8, 0x40000401);
    assert(!translate(root_block, 0xffffff80600b7d48ULL));
    Memory outside_page;
    put(outside_page.regions[0x50000000], 8, 0x400000401ULL);
    assert(!translate(outside_page, 0xffffff80600b7d48ULL));
    unsigned ancestor_reads = 0;
    const auto ancestor_reads_read = [&](auto at, auto output) {
        const bool ok = synthetic.read(at, output);

        if (at == 0x50d1f078 && ++ancestor_reads == 3) {
            output[0] ^= std::byte{1};
        }

        return ok;
    };
    assert(!qnx_boot_candidate_physical(ancestor_reads_read, 0xffffff80600b7d48ULL));
    unsigned reads = 0;
    const auto reads_read = [&](auto at, auto output) {
        const bool ok = synthetic.read(at, output);

        if (at == 0x50d1f078 && ++reads == 2) {
            output[0] ^= std::byte{1};
        }

        return ok;
    };
    assert(!qnx_boot_candidate_physical(reads_read, 0xffffff80600b7d48ULL));
    auto delayed = read_qnx_boot_progress([](auto, auto) {
        std::this_thread::sleep_for(std::chrono::milliseconds(105));

        return false;
    });
    assert(delayed["status"] == "unknown" && delayed["read_budget_exhausted"] == true &&
           delayed["physical_read_calls"] == 1);
    auto unavailable = read_qnx_boot_progress([](auto, auto) -> bool {
        throw std::runtime_error("exited child");
    });
    assert(unavailable["status"] == "unknown");

    if (argc == 1) {
        return 0;
    }

    if ((argc != 3 && argc != 5 && argc != 7) || std::string_view(argv[1]) != "--fixture" ||
        (argc >= 5 && std::string_view(argv[3]) != "--libc-fixture") ||
        (argc == 7 && std::string_view(argv[5]) != "--threadmsgque-fixture")) {
        return 2;
    }

    std::ifstream input(argv[2], std::ios::binary | std::ios::ate);

    if (!input || input.tellg() != 827392) {
        return 2;
    }

    input.seekg(0);
    std::string fixture(827392, 0);
    input.read(fixture.data(), static_cast<std::streamsize>(fixture.size()));

    if (!input || input.peek() != std::char_traits<char>::eof()) {
        return 2;
    }

    if (sha256_bytes(fixture) !=
        "dff3442b6a7a01b6beae217b0e6228a4061158598114db3e7482e1dd8b621792") {
        return 2;
    }

    std::string libc, msgque;

    if (argc >= 5) {
        std::ifstream source(argv[4], std::ios::binary | std::ios::ate);

        if (!source || source.tellg() != 663161) {
            return 2;
        }

        source.seekg(0);
        libc.resize(663161);
        source.read(libc.data(), static_cast<std::streamsize>(libc.size()));

        if (!source || source.peek() != std::char_traits<char>::eof() ||
            sha256_bytes(libc) !=
                    "e359175048e592d729e8688c0ba69dbbb089addf45740e7d88ed45b08a392f53") {
            return 2;
        }
    }

    if (argc == 7) {
        std::ifstream source(argv[6], std::ios::binary | std::ios::ate);

        if (!source || source.tellg() != 25712) {
            return 2;
        }

        source.seekg(0);
        msgque.resize(25712);
        source.read(msgque.data(), static_cast<std::streamsize>(msgque.size()));

        if (!source || source.peek() != std::char_traits<char>::eof() ||
            sha256_bytes(msgque) !=
                    "3233316f27393cd2adf63e93a5862667059db47cae2d82c713d3c19d68516e10") {
            return 2;
        }
    }

    Memory good(fixture);
    good.add_threads();
    auto observed = good.sample();
    assert(observed["status"] == "verified" && observed["processes"].size() == 2 &&
           observed["processes"][1]["pid"] == 4098 && observed["processes"][1]["name"] == "bin/on");
    assert(observed["thread_layout"] == "live_code_verified" &&
           observed["thread_entries_observed"] == 1 &&
           observed["thread_processes"][0]["threads"][0]["state"] == "STATE_REPLY" &&
           observed["thread_processes"][0]["threads"][0]["tid"] == 1 &&
           observed["physical_read_calls"].get<unsigned>() <= 4096);
    {
        Memory late(fixture);
        late.add_threads();
        put(late.regions[0x60126320], 16, 64, 2);
        late.regions[0x81000000].resize(64 * 8);
        put(late.regions[0x81000000], 42 * 8, 0xffffff8081101000ULL);
        put(late.regions[0x81101000], 8, 4138, 4);
        late.regions[0x81201000] = "usr/bin/rpcd";
        late.regions[0x81201000].resize(128);
        put(late.regions[0x81000000], 2 * 8, 0xffffff8081102000ULL);
        auto &unrelated = late.regions[0x81102000];
        unrelated.resize(512);
        put(unrelated, 8, 4098, 4);
        put(unrelated, 0x1a0, 0xffffff8081202000ULL);
        late.regions[0x81202000] = "unrelated";
        late.regions[0x81202000].resize(128);
        const auto reader = [&](auto at, auto out) {
            if (at == 0x81102000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(105));
            }

            return late.read(at, out);
        };
        auto broad = read_qnx_boot_progress(reader);
        assert(broad["read_budget_exhausted"] == true &&
               !broad.contains("thread_entries_observed"));
        const std::array targets{QnxBootProcessTarget{4138, "usr/bin/rpcd"}};
        auto focused = read_qnx_boot_progress(reader, targets);
        assert(focused["status"] == "verified" &&
               focused["process_sampling"] == "validated_targets_only" &&
               focused["process_slots_sampled"] == 2 && focused["processes"].size() == 2 &&
               focused["thread_processes"][0]["observed_pid_with_index_guard"] == 4138 &&
               focused["thread_processes"][0]["threads"][0]["state"] == "STATE_REPLY" &&
               !focused["read_budget_exhausted"].get<bool>() &&
               focused["physical_read_calls"].get<unsigned>() <= 4096);
        auto blocked_memory = [&](unsigned state) {
            Memory memory = late;
            memory.add_saved_context();
            put(memory.regions[0x81400000], 0x13, state, 1);

            return memory;
        };

        for (const auto state : {5u, 12u, 13u, 14u, 15u}) {
            auto memory = blocked_memory(state);
            auto sample = read_qnx_boot_progress(
                    [&](auto at, auto out) {
                        return memory.read(at, out);
                    },
                    targets);
            const auto &saved =
                    sample["thread_processes"][0]["threads"][0]["saved_blocked_context"];
            assert(sample["saved_blocked_context_layout"] == "live_code_verified" &&
                   saved["status"] == "verified" &&
                   saved["source"] == "kernel_saved_context_not_live_cpu" &&
                   saved["pc"] == 0x3524850540ULL && saved["lr"] == 0x352482ca7cULL &&
                   saved["sp"] == 0x7fffff0000ULL && !saved.contains("caller"));
        }

        for (const auto state : {1u, 2u, 3u, 6u, 7u, 8u, 9u, 10u, 11u, 16u, 20u}) {
            auto memory = blocked_memory(state);
            auto sample = read_qnx_boot_progress(
                    [&](auto at, auto out) {
                        return memory.read(at, out);
                    },
                    targets);
            assert(!sample["thread_processes"][0]["threads"][0].contains("saved_blocked_context"));
        }

        auto changed_context = blocked_memory(12);
        changed_context.context_race = true;
        auto context_race = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    return changed_context.read(at, out);
                },
                targets)["thread_processes"][0]["threads"][0]["saved_blocked_context"];
        assert(context_race["status"] == "unknown" && context_race["reason"] == "race" &&
               !context_race.contains("pc"));
        auto changed_state = blocked_memory(12);
        unsigned state_reads = 0;
        auto state_race = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    const bool ok = changed_state.read(at, out);

                    if (at == 0x81400000 && out.size() == 0x58 && ++state_reads == 3) {
                        out[0x13] ^= std::byte{1};
                    }

                    return ok;
                },
                targets)["thread_processes"][0]["threads"][0]["saved_blocked_context"];
        assert(state_race["status"] == "unknown" && state_race["reason"] == "race" &&
               !state_race.contains("pc"));
        auto privileged = blocked_memory(13);
        put(privileged.regions[0x81400000], 0x418, 4);
        assert(read_qnx_boot_progress(
                       [&](auto at, auto out) {
                           return privileged.read(at, out);
                       },
                       targets)["thread_processes"][0]["threads"][0]["saved_blocked_context"]
                               ["reason"] == "not_saved_el0");
        auto no_context_code = blocked_memory(12);
        no_context_code.regions[0x600a301c][0] ^= 1;
        auto unverified_context = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    return no_context_code.read(at, out);
                },
                targets);
        assert(unverified_context["saved_blocked_context_layout"] == "unverified" &&
               !unverified_context["thread_processes"][0]["threads"][0].contains(
                       "saved_blocked_context"));
        auto mismatch = targets;
        mismatch[0].name = "usr/bin/pmonitor";
        auto wrong = read_qnx_boot_progress(reader, mismatch);
        assert(wrong["processes"].size() == 1 && wrong["skip_reasons"]["target_identity"] == 1);
        mismatch = targets;
        mismatch[0].pid += 4096;
        auto reused = read_qnx_boot_progress(reader, mismatch);
        assert(reused["processes"].size() == 1 && reused["skip_reasons"]["target_identity"] == 1);
        unsigned names = 0;
        auto raced = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    const bool ok = late.read(at, out);

                    if (at == 0x81201000 && ++names == 3) {
                        out[0] ^= std::byte{1};
                    }

                    return ok;
                },
                targets);
        assert(raced["thread_processes"][0]["status"] == "unknown" &&
               raced["thread_processes"][0]["threads"].empty() &&
               raced["thread_processes"][0]["reason"] == "target_identity_race");
        Memory epoch = late;
        epoch.vector_race = true;
        auto changed = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    return epoch.read(at, out);
                },
                targets);
        assert(changed["thread_processes"][0]["threads"].empty() &&
               changed["thread_processes"][0]["reason"] == "process_vector_race");
        std::array<QnxBootProcessTarget, 17> excessive{};
        assert(read_qnx_boot_progress(reader, excessive)["reason"] == "target_count");
        const std::array duplicate{targets[0], targets[0]};
        assert(read_qnx_boot_progress(reader, duplicate)["reason"] == "target_geometry");
        auto unpinned = late;
        unpinned.regions[0x600b7d48][0] ^= 1;
        assert(read_qnx_boot_progress(
                       [&](auto at, auto out) {
                           return unpinned.read(at, out);
                       },
                       targets)["reason"] == "kernel_code_unverified");
    }
    {
        Memory boundary(fixture);
        boundary.add_threads();
        put(boundary.regions[0x50000000], 16, 0x55000003);
        boundary.regions[0x55000000].resize(4096);
        put(boundary.regions[0x55000000], 8 * 8, 0x81000401);
        put(boundary.regions[0x55000000], 9 * 8, 0x55001003);
        boundary.regions[0x55001000].resize(4096);
        put(boundary.regions[0x55001000], 0, 0x81200403);
        put(boundary.regions[0x55001000], 4 * 8, 0x81204403);
        put(boundary.regions[0x55000000], 10 * 8, 0x81400401);
        put(boundary.regions[0x55001000], 0x100 * 8, 0x81300403);
        put(boundary.regions[0x81101000], 0x1a0, 0xffffff8081204ff9ULL);
        boundary.regions[0x81204ff9] = "bin/on";
        boundary.regions[0x81204ff9].push_back(char(0));
        auto accepted = boundary.sample();
        assert(accepted["status"] == "verified" && accepted["processes"].size() == 2 &&
               accepted["processes"][1]["name"] == "bin/on");
        const std::array boundary_targets{QnxBootProcessTarget{4098, "bin/on"}};
        auto target_boundary = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    return boundary.read(at, out);
                },
                boundary_targets);
        assert(target_boundary["thread_processes"][0]["status"] == "verified");
        auto no_zero = boundary;
        no_zero.regions[0x81204ff9][6] = char(120);
        auto missing = no_zero.sample();
        assert(missing["processes"].size() == 1 && missing["skip_reasons"]["name_read"] == 1);
        put(no_zero.regions[0x55001000], 5 * 8, 0x81205403);
        no_zero.regions[0x81205000].assign(121, char(120));
        auto unterminated = no_zero.sample();
        assert(unterminated["processes"].size() == 1 &&
               unterminated["skip_reasons"]["name_terminator"] == 1);
        unsigned vectors = 0;
        auto changed = read_qnx_boot_progress([&](auto at, auto out) {
            const bool ok = boundary.read(at, out);

            if (at == 0x60126320 && ++vectors == 2) {
                out[8] ^= std::byte{1};
            }

            return ok;
        });
        assert(changed["status"] == "unknown" && changed["reason"] == "process_vector_changed" &&
               changed["processes"].empty());
    }
    Memory peer(fixture);
    peer.add_threads();
    peer.add_peer();
    auto ipc = peer.sample();
    auto &client = ipc["thread_processes"][0];
    assert(ipc["reply_peer_layout"] == "live_code_verified" &&
           ipc["parent_layout"] == "live_code_verified" &&
           client["parent_pid_with_index_guard"] == 1);
    assert(client["threads"][0]["reply_peer"] ==
           Json({{"status", "verified"}, {"nd", 0}, {"pid", 1}, {"coid", 7}, {"chid", 2}}));
    Memory saved(fixture);
    saved.add_threads();
    saved.add_peer();
    saved.add_saved_context();
    auto saved_report = saved.sample();
    auto saved_context = saved_report["thread_processes"][0]["threads"][0]["saved_reply_context"];
    assert(saved_report["saved_reply_context_layout"] == "live_code_verified" &&
           saved_context["status"] == "verified" &&
           saved_context["source"] == "kernel_saved_context_not_live_cpu" &&
           saved_context["pc"] == 0x3524850540ULL && saved_context["lr"] == 0x352482ca7cULL &&
           saved_context["sp"] == 0x7fffff0000ULL && saved_context["x0"] == 0x40000000 &&
           saved_context["x2"] == 0xffffffffffffffe8ULL && !saved_context.contains("caller"));
    Memory saved_changed(fixture);
    saved_changed.add_threads();
    saved_changed.add_peer();
    saved_changed.add_saved_context();
    saved_changed.context_race = true;
    auto unstable_saved =
            saved_changed.sample()["thread_processes"][0]["threads"][0]["saved_reply_context"];
    assert(unstable_saved["status"] == "unknown" && unstable_saved["reason"] == "race" &&
           !unstable_saved.contains("pc"));
    Memory saved_state_changed(fixture);
    saved_state_changed.add_threads();
    saved_state_changed.add_peer();
    saved_state_changed.add_saved_context();
    saved_state_changed.context_state_race = true;
    assert(saved_state_changed.sample()["thread_processes"][0]["threads"][0]["saved_reply_context"]
                                       ["reason"] == "race");
    Memory saved_connection_changed(fixture);
    saved_connection_changed.add_threads();
    saved_connection_changed.add_peer();
    saved_connection_changed.add_saved_context();
    saved_connection_changed.context_connection_race = true;
    assert(saved_connection_changed.sample()["thread_processes"][0]["threads"][0]
                                            ["saved_reply_context"]["reason"] == "race");
    Memory privileged(fixture);
    privileged.add_threads();
    privileged.add_peer();
    privileged.add_saved_context();
    put(privileged.regions[0x81400000], 0x418, 4);
    assert(privileged.sample()["thread_processes"][0]["threads"][0]["saved_reply_context"]
                              ["reason"] == "not_saved_el0");
    Memory no_saved_layout(fixture);
    no_saved_layout.add_threads();
    no_saved_layout.add_peer();
    no_saved_layout.add_saved_context();
    no_saved_layout.regions[0x600a301c][0] ^= 1;
    auto no_saved = no_saved_layout.sample();
    assert(no_saved["saved_reply_context_layout"] == "unverified" &&
           !no_saved["thread_processes"][0]["threads"][0].contains("saved_reply_context"));
    Memory conn_race(fixture);
    conn_race.add_threads();
    conn_race.add_peer();
    conn_race.connection_race = true;
    auto bad_conn = conn_race.sample()["thread_processes"][0]["threads"][0]["reply_peer"];
    assert(bad_conn["status"] == "unknown" && bad_conn["reason"] == "race" &&
           !bad_conn.contains("pid"));
    Memory ptr_race(fixture);
    ptr_race.add_threads();
    ptr_race.add_peer();
    ptr_race.connection_pointer_race = true;
    assert(ptr_race.sample()["thread_processes"][0]["threads"][0]["reply_peer"]["reason"] ==
           "race");
    Memory parent_changed(fixture);
    parent_changed.add_threads();
    parent_changed.add_peer();
    parent_changed.parent_race = true;
    assert(!parent_changed.sample()["thread_processes"][0].contains("parent_pid_with_index_guard"));
    Memory special(fixture);
    special.add_threads();
    special.add_peer();
    put(special.regions[0x81500000], 0xe, 1, 2);
    assert(special.sample()["thread_processes"][0]["threads"][0]["reply_peer"]["reason"] ==
           "special_connection");
    Memory wrong_peer_layout(fixture);
    wrong_peer_layout.add_threads();
    wrong_peer_layout.add_peer();
    wrong_peer_layout.regions[0x601133b8][2] ^= 1;
    auto unpinned_peer = wrong_peer_layout.sample();
    assert(unpinned_peer["reply_peer_layout"] == "unverified" &&
           !unpinned_peer["thread_processes"][0]["threads"][0].contains("reply_peer"));
    Memory active(fixture);
    active.add_threads();
    active.add_peer();
    put(active.regions[0x81400000], 0x13, 1, 1);
    assert(!active.sample()["thread_processes"][0]["threads"][0].contains("reply_peer"));
    Memory fairness(fixture);
    fairness.add_threads();
    fairness.add_procnto_thread();
    fairness.slow_procnto = true;
    auto client_first = fairness.sample();
    assert(client_first["read_budget_exhausted"] == true &&
           client_first["thread_processes"][0]["observed_pid_with_index_guard"] == 4098 &&
           client_first["thread_processes"][0]["threads"][0]["state"] == "STATE_REPLY");
    Memory owner(fixture);
    owner.add_threads();
    put(owner.regions[0x81400000], 0x48, 0xffffff8081100000ULL);
    assert(owner.sample()["thread_processes"][0]["skip_reasons"]["owner"] == 1);
    Memory tid(fixture);
    tid.add_threads();
    put(tid.regions[0x81400000], 0x50, 1, 4);
    assert(tid.sample()["thread_processes"][0]["skip_reasons"]["tid_index"] == 1);
    Memory thread_changed(fixture);
    thread_changed.add_threads();
    thread_changed.thread_race = true;
    assert(thread_changed.sample()["thread_processes"][0]["skip_reasons"]["race"] == 1);
    Memory slot_changed(fixture);
    slot_changed.add_threads();
    slot_changed.slot_race = true;
    auto stale_slot = slot_changed.sample();
    assert(stale_slot["thread_processes"][0]["reason"] == "process_vector_race" &&
           stale_slot["thread_processes"][0]["threads"].empty());
    Memory vector_changed(fixture);
    vector_changed.add_threads();
    vector_changed.vector_race = true;
    auto stale_vector = vector_changed.sample();
    assert(stale_vector["thread_processes"][0]["reason"] == "process_vector_race" &&
           stale_vector["thread_processes"][0]["threads"].empty());
    Memory layout(fixture);
    layout.add_threads();
    layout.regions[0x600b32b8][0] ^= 1;
    auto no_layout = layout.sample();
    assert(no_layout["status"] == "verified" && no_layout["thread_layout"] == "unverified" &&
           !no_layout.contains("thread_processes"));
    Memory crowded(fixture);
    put(crowded.regions[0x60126320], 16, 18, 2);
    crowded.regions[0x81000000].resize(18 * 8);

    for (unsigned i = 0; i < 16; ++i) {
        const auto process_pa = 0x81500000ULL + static_cast<unsigned long long>(i) * 0x1000,
                   process_va = 0xffffff8000000000ULL + process_pa,
                   table_pa = 0x81600000ULL + static_cast<unsigned long long>(i) * 0x1000;
        put(crowded.regions[0x81000000], static_cast<std::size_t>(i + 2) * 8, process_va);
        auto &process = crowded.regions[process_pa];
        process.resize(512);
        put(process, 8, 4098 + i, 4);
        put(process, 0x1a0, 0xffffff8081201000ULL);
        put(process, 0xa8, 0xffffff8000000000ULL + table_pa);
        put(process, 0xb0, 16, 2);
        auto &slots = crowded.regions[table_pa];
        slots.resize(16 * 8);

        for (unsigned j = 0; j < 16; ++j) {
            const auto thread_pa = 0x82000000ULL + static_cast<unsigned long long>(i) * 0x10000 +
                                   static_cast<unsigned long long>(j) * 0x1000;
            put(slots, static_cast<std::size_t>(j) * 8, 0xffffff8000000000ULL + thread_pa);
            auto &thread = crowded.regions[thread_pa];
            thread.resize(0x58);
            put(thread, 0x48, process_va);
            put(thread, 0x50, j, 4);
            put(thread, 0x13, 6, 1);
        }
    }

    auto limited = crowded.sample();
    assert(limited["read_budget_exhausted"] == true &&
           limited["physical_read_calls"].get<unsigned>() <= 4096);

    if (limited.contains("thread_entries_observed")) {
        assert(limited["thread_entries_observed"].get<unsigned>() <= 256);
    }

    if (!libc.empty()) {
        auto prepare = [&]() {
            Memory memory(fixture);
            memory.add_threads();
            memory.add_peer();
            memory.add_saved_context();
            memory.add_user_space(libc);

            return memory;
        };
        const std::array code_targets{QnxBootProcessTarget{4098, "bin/on"}};
        auto target_context = [&](Memory &memory) {
            return read_qnx_boot_progress(
                    [&](auto at, auto out) {
                        return memory.read(at, out);
                    },
                    code_targets)["thread_processes"][0]["threads"][0]["saved_blocked_context"];
        };
        auto code_memory = [&]() {
            auto memory = prepare();
            put(memory.regions[0x81400000], 0x13, 12, 1);
            memory.regions[0x84000000].replace(0x530, 32, libc.substr(0x50530, 32));

            return memory;
        };
        auto code_good = code_memory();
        auto code_proof = target_context(code_good)["user_text"];
        assert(code_proof["status"] == "verified" &&
               code_proof["scope"] == "stable_owner_user_text_hashes_not_caller_classification" &&
               code_proof["root_physical"] == 0x53000000 &&
               code_proof["pc"]["address"] == 0x3524850530ULL && code_proof["pc"]["bytes"] == 32 &&
               code_proof["pc"]["sha256"] ==
                       sha256_bytes(std::string_view(libc).substr(0x50530, 32)) &&
               code_proof["lr"]["sha256"] ==
                       sha256_bytes(std::string_view(libc).substr(0x2ca68, 32)) &&
               !code_proof.contains("caller") && !code_proof["pc"].contains("data"));
        const std::array caller_targets{QnxBootProcessTarget{4098, "usr/bin/rpcd"}};
        auto caller_memory = [&]() {
            Memory memory(fixture);
            memory.add_threads();
            memory.add_peer();
            memory.add_saved_context();
            memory.add_user_space(libc, true);

            return memory;
        };
        auto sample_caller = [&](Memory &memory) {
            return read_qnx_boot_progress(
                    [&](auto at, auto out) {
                        return memory.read(at, out);
                    },
                    caller_targets);
        };
        auto caller_text = [](const Json &report) {
            return report["thread_processes"][0]["threads"][0]["saved_blocked_context"]
                         ["user_text"];
        };
        auto caller_good = caller_memory();
        auto caller_report = sample_caller(caller_good),
             single = caller_text(caller_report)["single_saved_caller"];
        assert(single["status"] == "verified" && single["return_address"] == 0x3524860010ULL &&
               single["text"]["sha256"] ==
                       sha256_bytes(std::string_view(libc).substr(0x2ca28, 32)) &&
               single["source"] == "exact_libc_prologue_single_saved_frame_not_live" &&
               !single.contains("data") &&
               caller_report["physical_read_calls"].get<unsigned>() <= 4096);
        auto cross_base = caller_memory();
        put(cross_base.regions[0x81400000], 0x400, 0x3524832cecULL);
        assert(!caller_text(sample_caller(cross_base)).contains("single_saved_caller"));
        auto no_fp = caller_memory();
        put(no_fp.regions[0x81400000], 0x3f8, 0x7fffff0200ULL);
        assert(!caller_text(sample_caller(no_fp)).contains("single_saved_caller"));
        auto wrong_prologue = caller_memory();
        wrong_prologue.regions[0x84004000][0xcdc] ^= 1;
        assert(!caller_text(sample_caller(wrong_prologue)).contains("single_saved_caller"));
        auto wrong_wrapper = caller_memory();
        wrong_wrapper.regions[0x84003000][0x1a8] ^= 1;
        assert(!caller_text(sample_caller(wrong_wrapper)).contains("single_saved_caller"));
        auto no_stack = caller_memory();
        no_stack.regions.erase(0x84002000);
        assert(caller_text(sample_caller(no_stack))["single_saved_caller"]["status"] == "unknown");
        auto bad_return = caller_memory();
        put(bad_return.regions[0x84002000], 0x108, 1ULL << 39);
        assert(caller_text(sample_caller(bad_return))["single_saved_caller"]["reason"] ==
               "caller_geometry");
        auto no_caller_code = caller_memory();
        no_caller_code.regions.erase(0x84005000);
        assert(caller_text(sample_caller(no_caller_code))["single_saved_caller"]["status"] ==
               "unknown");
        auto frame_race = caller_memory();
        unsigned frame_reads = 0;
        auto frame_stale = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    bool ok = frame_race.read(at, out);

                    if (at == 0x84002100 && out.size() == 16 && ++frame_reads == 2) {
                        out[8] ^= std::byte{4};
                    }

                    return ok;
                },
                caller_targets);
        assert(caller_text(frame_stale)["reason"] == "saved_caller_race");
        auto caller_race = caller_memory();
        unsigned caller_reads = 0;
        auto caller_stale = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    bool ok = caller_race.read(at, out);

                    if (at == 0x84005000 && out.size() == 32 && ++caller_reads == 2) {
                        out[0] ^= std::byte{1};
                    }

                    return ok;
                },
                caller_targets);
        assert(caller_text(caller_stale)["reason"] == "saved_caller_race");
        auto caller_owner_race = caller_memory();
        caller_owner_race.user_root_race = true;
        assert(caller_text(sample_caller(caller_owner_race))["status"] == "unknown");

        if (!msgque.empty()) {
            auto second_memory = [&]() {
                Memory memory(fixture);
                memory.add_threads();
                memory.add_peer();
                memory.add_saved_context();
                memory.add_user_space(libc, true, msgque);

                return memory;
            };
            auto next_caller = [&](Memory &memory) {
                return caller_text(
                        sample_caller(memory))["single_saved_caller"]["next_saved_caller"];
            };
            auto second = second_memory();
            auto second_report = sample_caller(second);
            auto next = caller_text(second_report)["single_saved_caller"]["next_saved_caller"];
            assert(next["status"] == "verified" && next["return_address"] == 0x3524860010ULL &&
                   next["source"] == "exact_msgque_prologue_second_saved_frame_not_live" &&
                   next["text"]["sha256"] ==
                           sha256_bytes(std::string_view(libc).substr(0x2ca28, 32)) &&
                   !next.contains("data") &&
                   second_report["physical_read_calls"].get<unsigned>() <= 4096);
            auto adjacent = second_memory();
            put(adjacent.regions[0x84002000], 0x100, 0x7fffff0120ULL);
            assert(next_caller(adjacent)["reason"] == "msgque_frame_geometry");
            auto bad_base = second_memory();
            put(bad_base.regions[0x84002000], 0x108, 0x35248612acULL);
            bad_base.regions[0x84007000].replace(0x29c, 32, msgque.substr(0x1298, 32));
            assert(next_caller(bad_base)["reason"] == "msgque_frame_geometry");
            auto prologue = second_memory();
            prologue.regions[0x84007000][0x210] ^= 1;
            assert(next_caller(prologue)["reason"] == "msgque_prologue_unverified");
            auto bad_next = second_memory();
            put(bad_next.regions[0x84002000], 0x118, 1ULL << 39);
            assert(next_caller(bad_next)["reason"] == "next_caller_geometry");
            auto missing_next = second_memory();
            missing_next.regions.erase(0x84005000);
            assert(next_caller(missing_next)["status"] == "unknown");

            for (unsigned race = 0; race < 4; ++race) {
                auto memory = second_memory();
                unsigned reads = 0;
                auto report = read_qnx_boot_progress(
                        [&](auto at, auto out) {
                            bool ok = memory.read(at, out);
                            const bool match = race == 0 ? (at == 0x84002110 && out.size() == 0x30)
                                               : race == 1 ? (at == 0x84005000 && out.size() == 32)
                                               : race == 2 ? (at == 0x84007210 && out.size() == 24)
                                                           : (at == 0x84002100 && out.size() == 16);

                            if (match && ++reads == (race == 3 ? 3 : 2)) {
                                out[0] ^= std::byte{1};
                            }

                            return ok;
                        },
                        caller_targets);
                assert(caller_text(report)["reason"] == "saved_second_caller_race");
            }

            auto second_owner = second_memory();
            second_owner.user_root_race = true;
            assert(caller_text(sample_caller(second_owner))["status"] == "unknown");
            auto second_state = second_memory();
            bool second_seen = false;
            auto state_report = read_qnx_boot_progress(
                    [&](auto at, auto out) {
                        bool ok = second_state.read(at, out);

                        if (at == 0x84002110 && out.size() == 0x30) {
                            second_seen = true;
                        }

                        if (second_seen && at == 0x81400000 && out.size() == 0x58) {
                            out[0x13] ^= std::byte{1};
                        }

                        return ok;
                    },
                    caller_targets);
            assert(caller_text(state_report)["status"] == "unknown");
            auto slow = second_memory();
            auto bounded = read_qnx_boot_progress(
                    [&](auto at, auto out) {
                        bool ok = slow.read(at, out);

                        if (at == 0x84002110 && out.size() == 0x30) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(105));
                        }

                        return ok;
                    },
                    caller_targets);
            assert(bounded["read_budget_exhausted"] == true &&
                   bounded["physical_read_calls"].get<unsigned>() <= 4096 &&
                   bounded.dump().find("exact_msgque_prologue_second_saved_frame_not_live") ==
                           std::string::npos);
            std::cout << Json({{"case", "second_saved_caller"},
                               {"physical_read_calls", second_report["physical_read_calls"]},
                               {"raw_OEM_bytes", false}})
                                 .dump()
                      << "\n";
        }

        auto caller_default = caller_memory();
        auto caller_unspecified = caller_default.sample();
        assert(!caller_unspecified["thread_processes"][0]["threads"][0].contains(
                "saved_blocked_context"));
        auto unmapped = code_memory();
        put(unmapped.regions[0x53000000], ((0x3524850540ULL >> 30) & 511) * 8, 0);
        auto no_mapping = target_context(unmapped)["user_text"];
        assert(no_mapping["status"] == "unknown" && !no_mapping.contains("pc"));
        auto outside = code_memory();
        put(outside.regions[0x81400000], 0x400, 1ULL << 39);
        assert(target_context(outside)["user_text"]["reason"] == "user_window_geometry");
        auto kernel_context = code_memory();
        put(kernel_context.regions[0x81400000], 0x418, 4);
        assert(!target_context(kernel_context).contains("user_text"));
        auto changed_owner = code_memory();
        put(changed_owner.regions[0x81701000], 0x68, 0xffffff8081100000ULL);
        assert(target_context(changed_owner)["user_text"]["reason"] == "memory_owner");
        auto changed_root = code_memory();
        changed_root.user_root_race = true;
        auto stale_root = target_context(changed_root)["user_text"];
        assert(stale_root["reason"] == "race" && !stale_root.contains("pc"));
        auto changed_code = code_memory();
        unsigned code_reads = 0;
        auto stale_code = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    const bool ok = changed_code.read(at, out);

                    if (at == 0x84000530 && out.size() == 32 && ++code_reads == 2) {
                        out[0] ^= std::byte{1};
                    }

                    return ok;
                },
                code_targets)["thread_processes"][0]["threads"][0]["saved_blocked_context"]
                             ["user_text"];
        assert(stale_code["status"] == "unknown" && stale_code["reason"] == "race" &&
               !stale_code.contains("pc"));
        auto no_walk_code = code_memory();
        no_walk_code.regions[0x6009a18c][0] ^= 1;
        assert(target_context(no_walk_code)["user_text"]["reason"] ==
               "user_walk_layout_unverified");
        auto crowded_target = prepare();
        put(crowded_target.regions[0x81101000], 0xb0, 16, 2);
        crowded_target.regions[0x81300000].resize(16 * 8);

        for (unsigned i = 1; i < 16; ++i) {
            const auto physical = 0x85000000ULL + static_cast<unsigned long long>(i) * 0x1000,
                       base = 0x100000000ULL + i * 0x40000000ULL,
                       l2 = 0x54000000ULL + static_cast<unsigned long long>(i) * 0x4000,
                       l3 = l2 + 0x1000, wrapper = l2 + 0x2000, caller = l2 + 0x3000;
            put(crowded_target.regions[0x81300000], static_cast<std::size_t>(i) * 8,
                0xffffff8000000000ULL + physical + 2);
            auto &thread = crowded_target.regions[physical];
            thread = crowded_target.regions[0x81400000];
            put(thread, 0x50, i, 4);
            put(thread, 0x410, base + 0x50540);
            put(thread, 0x400, base + 0x2ca78);
            put(crowded_target.regions[0x53000000], ((base >> 30) & 511) * 8, l2 | 3);
            crowded_target.regions[l2].resize(4096);
            put(crowded_target.regions[l2], 0, l3 | 3);
            crowded_target.regions[l3].resize(4096);
            put(crowded_target.regions[l3], 0x50 * 8, wrapper | 0x403);
            put(crowded_target.regions[l3], 0x2c * 8, caller | 0x403);
            crowded_target.regions[wrapper].resize(4096);
            crowded_target.regions[wrapper].replace(0x530, 28, libc.substr(0x50530, 28));
            crowded_target.regions[caller].resize(4096);
            crowded_target.regions[caller].replace(0xa28, 112, libc.substr(0x2ca28, 112));
        }

        std::vector<QnxBootProcessTarget> crowded_targets{code_targets[0]};
        put(crowded_target.regions[0x60126320], 16, 18, 2);
        crowded_target.regions[0x81000000].resize(18 * 8);

        for (unsigned i = 3; i < 18; ++i) {
            const auto physical = 0x86000000ULL + static_cast<unsigned long long>(i) * 0x1000,
                       name = 0x87000000ULL + static_cast<unsigned long long>(i) * 0x1000;
            put(crowded_target.regions[0x81000000], static_cast<std::size_t>(i) * 8,
                0xffffff8000000000ULL + physical);
            auto &process = crowded_target.regions[physical];
            process.resize(512);
            put(process, 8, 4096 + i, 4);
            put(process, 0x1a0, 0xffffff8000000000ULL + name);
            crowded_target.regions[name] = "bin/sh";
            crowded_target.regions[name].resize(128);
            crowded_targets.push_back({4096 + i, "bin/sh"});
        }

        auto reserved = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    return crowded_target.read(at, out);
                },
                crowded_targets);
        std::cout << Json({{"case", "target_guard_reserve"},
                           {"reads", reserved["physical_read_calls"]},
                           {"elapsed_ms", reserved["read_elapsed_ms"]},
                           {"threads", reserved["thread_entries_observed"]}})
                             .dump()
                  << "\n";
        assert(reserved["read_guard_reserve_calls"] == 128 &&
               reserved["target_sampling_partial"] == true &&
               reserved["read_budget_exhausted"] == true &&
               reserved["physical_read_calls"].get<unsigned>() <= 4096 &&
               reserved["thread_processes"][0]["status"] == "verified" &&
               !reserved["thread_processes"][0]["threads"].empty());
        unsigned capped_reads = 0;
        auto stale_reserved = read_qnx_boot_progress(
                [&](auto at, auto out) {
                    const bool ok = crowded_target.read(at, out);

                    if (++capped_reads > 3968 && at == 0x81101000) {
                        out[8] ^= std::byte{1};
                    }

                    return ok;
                },
                crowded_targets);
        assert(stale_reserved["physical_read_calls"].get<unsigned>() <= 4096 &&
               stale_reserved["thread_processes"][0]["status"] == "unknown" &&
               stale_reserved["thread_processes"][0]["threads"].empty());
        auto memory = prepare();
        auto decoded = memory.sample();
        auto request = decoded["thread_processes"][0]["threads"][0]["reply_request"];
        std::cout << Json({{"case", "owner_user_request"},
                           {"physical_read_calls", decoded["physical_read_calls"]},
                           {"translation_proof_entries", decoded["translation_proof_entries"]}})
                             .dump()
                  << "\n";
        assert(decoded["translation_proof_entries"].get<unsigned>() <= 512);
        assert(decoded["reply_request_layout"] == "live_code_verified" &&
               request["status"] == "verified" && request["request"] == "mprotect_r" &&
               request["type"] == 0x41 && request["subtype"] == 1 && request["prot"] == 5 &&
               request["address"] == 0x3524800000ULL && request["length"] == 4096 &&
               request["message_hex"].get<std::string>().size() == 48 &&
               decoded["physical_read_calls"].get<unsigned>() <= 700);
        auto owner = prepare();
        put(owner.regions[0x81701000], 0x68, 0xffffff8081100000ULL);
        assert(owner.sample()["thread_processes"][0]["threads"][0]["reply_request"]["reason"] ==
               "memory_owner");
        auto arch_owner = prepare();
        put(arch_owner.regions[0x81702000], 8, 0xffffff8081100000ULL);
        assert(arch_owner.sample()["thread_processes"][0]["threads"][0]["reply_request"]
                                  ["reason"] == "arch_owner");
        auto code = prepare();
        code.regions[0x84000000][0x530] ^= 1;
        assert(code.sample()["thread_processes"][0]["threads"][0]["reply_request"]["reason"] ==
               "user_code_unverified");
        auto root_changed = prepare();
        root_changed.user_root_race = true;
        auto stale = root_changed.sample()["thread_processes"][0]["threads"][0]["reply_request"];
        assert(stale["status"] == "unknown" && stale["reason"] == "race" &&
               !stale.contains("request"));
        auto table_changed = prepare();
        table_changed.user_table_race = true;
        assert(table_changed.sample()["thread_processes"][0]["threads"][0]["reply_request"]
                                     ["reason"] == "user_code_unverified");
        auto bad_header = prepare();
        bad_header.regions[0x84002000][0x100] = 0;
        assert(bad_header.sample()["thread_processes"][0]["threads"][0]["reply_request"]
                                  ["reason"] == "message_header");
        auto dispatch = prepare();
        put(dispatch.regions[0x81700000], 16, 0xffffff806009894cULL);
        assert(dispatch.sample()["thread_processes"][0]["threads"][0]["reply_request"]["reason"] ==
               "arch_dispatch");
    }

    Memory before_cached(fixture);
    before_cached.cached_before_remap = true;
    auto before_stale = before_cached.sample();
    assert(before_cached.cache_process_reads == 1 && before_stale["processes"].size() == 1 &&
           before_stale["skip_reasons"]["stability_read_unavailable"] == 1);
    Memory after_cached(fixture);
    after_cached.cached_after_remap = true;
    auto after_stale = after_cached.sample();
    assert(after_cached.cache_process_reads == 2 && after_stale["processes"].size() == 1 &&
           after_stale["skip_reasons"]["stability_read_unavailable"] == 1);
    Memory corrupt(fixture);
    corrupt.regions[0x600b7d48][0] ^= 1;
    assert(corrupt.sample()["status"] == "unknown");
    Memory outside(fixture);
    put(outside.regions[0x60126320], 8, 0xffffff9000000000ULL);
    assert(outside.sample()["status"] == "unknown");
    Memory wrong_pid(fixture);
    put(wrong_pid.regions[0x81101000], 8, 4099, 4);
    auto mismatched = wrong_pid.sample();
    assert(mismatched["processes"].size() == 1 && mismatched["skip_reasons"]["pid_index"] == 1 &&
           !mismatched["unverified_entries"][0].contains("observed_pid_with_index_guard"));
    Memory changed(fixture);
    changed.race = true;
    auto unstable = changed.sample();
    assert(unstable["status"] == "verified" && unstable["processes"].size() == 1 &&
           unstable["skipped_unverified_entries"] == 1 && unstable["skip_reasons"]["race"] == 1 &&
           unstable["unverified_entries"][0]["observed_pid_with_index_guard"] == 4098);
    Memory invalid_name(fixture);
    invalid_name.regions[0x81201000][0] = char(0x80);
    auto non_ascii = invalid_name.sample();
    assert(non_ascii["processes"].size() == 1 && non_ascii["skip_reasons"]["name_non_ascii"] == 1);
    Memory no_name(fixture);
    put(no_name.regions[0x81101000], 0x1a0, 0);
    auto unnamed = no_name.sample();
    assert(unnamed["processes"].size() == 1 && unnamed["skip_reasons"]["nameptr"] == 1 &&
           unnamed["unverified_entries"][0]["observed_pid_with_index_guard"] == 4098);
    Memory unreadable(fixture);
    unreadable.regions.erase(0x81201000);
    assert(unreadable.sample()["skip_reasons"]["name_read"] == 1);
    Memory identity(fixture);
    identity.regions[0x81200000] = "other";
    identity.regions[0x81200000].resize(128);
    assert(identity.sample()["status"] == "unknown");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
