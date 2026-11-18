#include "qnx_passion_platform.hpp"
#include "process.hpp"
#include "qnx_imagefs.hpp"
#include "qnx_passion.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <vector>
using namespace voyemu;

static std::uint64_t le(std::string_view s, std::size_t a, unsigned n = 4) {
    std::uint64_t v = 0;

    for (unsigned i = 0; i < n; ++i) {
        v |= std::uint64_t(static_cast<unsigned char>(s.at(a + i))) << (8 * i);
    }

    return v;
}

template <class F> static void rejects(F f) {
    bool yes = false;

    try {
        f();
    } catch (...) {
        yes = true;
    }

    assert(yes);
}

struct DmaTrace {
    std::string bytes;
    bool error = false, complete = false;
    unsigned waits = 0;
};

static DmaTrace dma_flow(std::string_view manager, std::string_view payload, int copy_status,
                         int done_status) {
    DmaTrace t;

    t.bytes.assign(payload.size(), char(0xa5));

    // Actual OWN synchronous-copy contract; decode the real caller's branch.
    if (copy_status == 0) {
        std::copy(payload.begin(), payload.end(), t.bytes.begin());
    }

    const auto w = le(manager, 0x10134);
    assert((w & 0xff00001f) == 0x34000000);
    std::int32_t imm = (w >> 5) & 0x7ffff;

    if (imm & 0x40000) {
        imm -= 0x80000;
    }

    const auto next = copy_status == 0 ? 0x10134 + imm * 4 : 0x10138;

    if (next == 0x1017c) {
        t.waits = 1;

        return t;
    }

    if (next == 0x10138) {
        t.error = true;

        return t;
    }

    assert(next == 0x10198 && le(manager, 0x1019c) == 0x94001260 &&
           le(manager, 0x101a0) == 0x34000220);
    assert(le(manager, 0x101e4) == 0x4b1a02b5 && le(manager, 0x101e8) == 0x0b1a0339 &&
           le(manager, 0x101ec) == 0x17ffffb2);
    t.complete = done_status == 0;
    t.error = done_status != 0;

    return t;
}

int main(int argc, char **argv) {
    try {
        // OLD real GPT is missing config_data; new constructor supplies an OWN partition, not
        // vehicle data.
        const auto old_gpt = qnx_passion_gpt(200929280),
                   new_gpt = qnx_passion_11713_config_gpt(200929280);
        auto lookup = [](std::string_view cache, std::string_view name) {
            for (unsigned i = 0; i < 32; ++i) {
                bool equal = true;

                for (unsigned j = 0; j < 36; ++j) {
                    const auto c = le(cache, 4096 + i * 128 + 56 + j * 2, 2);

                    if (c != (j < name.size() ? static_cast<unsigned char>(name[j]) : 0)) {
                        equal = false;
                        break;
                    }
                }

                if (equal) {
                    return int(i);
                }
            }

            return -1;
        };
        assert(lookup(old_gpt.cache, "config_data") == -1 &&
               lookup(new_gpt.cache, "config_data") == 1 && lookup(new_gpt.cache, "ifs2") == 0);
        assert(new_gpt.cache.substr(4096, 128) == old_gpt.cache.substr(4096, 128));
        const auto cfg_entry = std::string_view(new_gpt.cache).substr(4096 + 128, 128);
        assert(le(cfg_entry, 32, 8) == 49216 && le(cfg_entry, 40, 8) == 49247 &&
               le(cfg_entry, 48, 8) == 0);
        assert(0xe4fc0000ULL + le(cfg_entry, 32, 8) * 4096 == 0xf1000000ULL &&
               0xe4fc0000ULL + (le(cfg_entry, 40, 8) + 1) * 4096 == 0xf1020000ULL);
        assert(new_gpt.cache.substr(4096 + 256) == std::string(4096 - 256, 0));
        assert(cfg_entry.substr(16, 16) != new_gpt.cache.substr(4096 + 16, 16));
        auto crc = [](std::string_view b) {
            std::uint32_t v = ~0U;

            for (unsigned char c : b) {
                v ^= c;

                for (unsigned i = 0; i < 8; ++i) {
                    v = (v >> 1) ^ ((v & 1) ? 0xedb88320 : 0);
                }
            }

            return ~v;
        };
        const auto array_crc = crc(std::string_view(new_gpt.cache).substr(4096, 4096));

        for (const auto &pair :
             std::array{std::pair{std::string_view(new_gpt.cache), std::size_t(0)},
                        std::pair{std::string_view(new_gpt.primary), std::size_t(4096)},
                        std::pair{std::string_view(new_gpt.backup), std::size_t(4096)}}) {
            auto h = std::string(pair.first.substr(pair.second, 92));
            const auto saved = le(h, 16);
            h.replace(16, 4, std::string(4, 0));
            assert(crc(h) == saved && le(h, 88) == array_crc && le(h, 80) == 32 &&
                   le(h, 84) == 128);
        }

        assert(new_gpt.cache.substr(4096) == new_gpt.primary.substr(8192) &&
               new_gpt.cache.substr(4096) == new_gpt.backup.substr(0, 4096));
        validate_qnx_passion_11713_config_gpt(new_gpt, 200929280);
        rejects([&] {
            validate_qnx_passion_11713_config_gpt(old_gpt, 200929280);
        });

        for (const auto at : {16U, 88U, 4096U + 128 + 32, 4096U + 128 + 40, 4096U + 128 + 56}) {
            auto bad = new_gpt;
            bad.cache[at] ^= 1;
            rejects([&] {
                validate_qnx_passion_11713_config_gpt(bad, 200929280);
            });
        }

        for (const auto bytes : {0ULL, 200929279ULL, 201330688ULL, 268435456ULL}) {
            rejects([&] {
                qnx_passion_11713_config_gpt(bytes);
            });
        }

        const auto blank = qnx_passion_11713_blank_config();
        assert(blank.size() == 131072 && blank == std::string(131072, 0) &&
               blank.find("mega_config_data_v1.0") == std::string::npos);

        if (argc == 1) {
            std::cout
                    << "Passion synthetic GPT and blank config guards passed; OEM part separate\n";

            return 0;
        }

        if (argc != 2 && argc != 6) {
            throw std::runtime_error("exact Passion original argument required");
        }

        if (!std::filesystem::is_regular_file(argv[1])) {
            std::cout << "Passion OEM original absent, skipped: " << argv[1] << '\n';

            return 77;
        }

        std::ifstream in(argv[1], std::ios::binary);
        const std::string original((std::istreambuf_iterator<char>(in)), {});
        assert(sha256_bytes(original) ==
               "1e7c487aae4f4353621f8d0301b352cc974e9037d114ff3ec2556d048512ad28");

        const auto baseline = prepare_qnx_passion_startup(original),
                   fdt = original.substr(205196, 20722);
        const auto changed = prepare_qnx_passion_11713_bootstrap(original);
        assert(changed.startup.size() == baseline.size() && changed.early_l2.size() == 4096);
        // Decode the real property stream, including both physical views of ONE cache.
        const auto cf = changed.startup.substr(205196 - 0x3000, 20722);
        auto be = [&](std::size_t at) {
            std::uint64_t v = 0;

            for (unsigned i = 0; i < 4; ++i) {
                v = (v << 8) | static_cast<unsigned char>(cf.at(at + i));
            }

            return v;
        };
        std::vector<std::string> path;
        std::string node, label;
        std::uint64_t base = 0, bytes = 0;
        unsigned aliases = 0;
        std::vector<std::pair<std::string, bool>> own_callbacks;
        unsigned own_regions = 0;

        for (std::size_t at = be(8); at < be(12);) {
            const auto token = be(at);
            at += 4;

            if (token == 1) {
                const auto z = cf.find(char(0), at);
                path.push_back(cf.substr(at, z - at));
                node = path.back();
                label.clear();
                base = bytes = 0;
                at = (z + 4) & ~std::size_t(3);
            } else if (token == 3) {
                const auto n = be(at), off = be(at + 4);
                at += 8;
                const auto pn = cf.substr(be(12) + off,
                                          cf.find(char(0), be(12) + off) - (be(12) + off));

                if (pn == "reg" && n == 16) {
                    base = (be(at) << 32) | be(at + 4);
                    bytes = (be(at + 8) << 32) | be(at + 12);
                }

                if (pn == "asinfo") {
                    assert(n && cf.at(at + n - 1) == 0);
                    label = cf.substr(at, n - 1);
                }

                at = (at + n + 3) & ~std::size_t(3);
            } else if (token == 2) {
                const std::array<std::string_view, 9> own_labels{
                        "bootguard", "hostpacked", "earlyl2",   "gptcache", "gptprimary",
                        "ifsb",      "varimg",     "gptbackup", "system"};

                for (unsigned j = 0; j < own_labels.size(); ++j) {
                    if (label == own_labels[j] && base && bytes) {
                        const std::array<std::uint64_t, 9> bases{
                                0xaffc0000, 0xe4d00000, 0xe4fa0000, 0xe4fb0000,    0xe4fc0000,
                                0xe5000000, 0xac800000, 0xf4fbe000, 0x27c000000ULL};
                        const std::array<std::uint64_t, 9> sizes{
                                0x40000,    0x200000,  4096, 8192,         12288,
                                0x0c7de000, 0x2200000, 8192, 2684404736ULL};
                        assert(node.size() >= 10 && node.at(1) == '@' && base == bases[j] &&
                               bytes == sizes[j]);
                        own_callbacks.emplace_back(label, !label.empty());
                        ++own_regions;
                    }
                }

                if ((node.starts_with("c0h@") || node.starts_with("c0a@") ||
                     node.starts_with("c5h@") || node.starts_with("c5a@"))) {
                    assert(path.size() == 3 && path[1] == "reserved-memory");
                    assert(label == "active_lun0_header" || label == "active_lun5_header" ||
                           label == "active_lun0_array" || label == "active_lun5_array");
                    const bool h = label.ends_with("header");
                    assert(base == (h ? 0xe4fb0000ULL : 0xe4fb1000ULL) &&
                           bytes == (h ? 92U : 4096U));
                    assert(base >= 0x40000000 && base + bytes <= 0x31c100000ULL);
                    ++aliases;
                    own_callbacks.emplace_back(label, !label.empty());
                }

                path.pop_back();
                node.clear();
            } else if (token == 9) {
                break;
            } else {
                assert(token == 4);
            }
        }

        assert(aliases == 4 && be(12) == 0x4694 + 968 && be(36) == 0x465c + 968 &&
               cf.size() == 20722);

        assert(cf.substr(be(12), 0x676) == fdt.substr(0x4694, 0x676));
        // Exact OEM iterator contract: callback nonzero ends traversal immediately.
        // Decoder requires asinfo; callbacks for our initialized ranges must all succeed.
        assert(own_regions == 9 && own_callbacks.size() == 13);
        auto traverse = [](const auto &callbacks) {
            unsigned visited = 0, found = 0;

            for (const auto &item : callbacks) {
                ++visited;

                if (!item.second) {
                    return std::array<unsigned, 3>{1, visited, found};
                }

                if (item.first.starts_with("active_lun")) {
                    ++found;
                }
            }

            return std::array<unsigned, 3>{0, visited, found};
        };
        assert((traverse(own_callbacks) == std::array<unsigned, 3>{0, 13, 4}));
        auto old_callbacks = own_callbacks;

        for (unsigned i = 0; i < 9; ++i) {
            old_callbacks[i].second = false;
        }

        assert((traverse(old_callbacks) == std::array<unsigned, 3>{1, 1, 0}));

        for (unsigned i = 0; i < 13; ++i) {
            auto missing = own_callbacks;
            missing[i].second = false;
            const auto status = traverse(missing);
            assert(status[0] == 1 && status[1] == i + 1);
        }

        // Execute only the four own immediate operations; allocator input registers are untouched.
        std::uint64_t x0 = 0x50000000, x1 = 0x60000000, x20 = x0, x21 = x1;

        for (const auto a : {0xe060U, 0xe068U, 0xe078U, 0xe07cU}) {
            const auto w = le(changed.startup, a - 0x3000);
            const auto rd = w & 31, hw = (w >> 21) & 3, imm = (w >> 5) & 65535;
            assert(rd <= 1);
            auto &r = rd ? x1 : x0;

            if ((w & 0xff800000) == 0xd2800000) {
                r = imm << (16 * hw);
            } else {
                assert((w & 0xff800000) == 0xf2800000);
                r = (r & ~(0xffffULL << (16 * hw))) | (imm << (16 * hw));
            }
        }

        assert(x0 == 0x40000000 && x1 == 0x31c0fffffULL && x20 == 0x50000000 && x21 == 0x60000000);
        assert(le(changed.startup, 0x63e18 - 0x3000, 8) == 0xe4fa041f);
        assert(le(changed.early_l2, 296 * 8, 8) == 0xe500041d);

        for (unsigned i = 0; i < 512; ++i) {
            assert(le(changed.early_l2, static_cast<std::size_t>(i) * 8, 8) ==
                   (i >= 296 && i < 392 ? ((0xe5000000ULL + (i - 296) * 0x200000ULL) | 0x41d) : 0));
        }

        const auto old_table = []() {
            std::string out(4096, 0);
            out.replace(296 * 8, 8, std::string("\x1d\x04\x00\xe5\0\0\0\0", 8));

            return out;
        }();
        assert(qnx_passion_11713_ifs2_early_map(old_table) == changed.early_l2);

        for (const auto at : {0U, 296U * 8, 297U * 8, 391U * 8, 392U * 8, 4095U}) {
            auto bad = old_table;
            bad[at] ^= 1;
            rejects([&] {
                qnx_passion_11713_ifs2_early_map(bad);
            });
        }

        rejects([&] {
            qnx_passion_11713_ifs2_early_map(old_table.substr(1));
        });
        rejects([&] {
            qnx_passion_11713_ifs2_early_map(changed.early_l2);
        });
        assert((le(changed.early_l2, 391 * 8, 8) & ~0x1fffffULL) + 0x200000 == 0xf1000000);

        for (const auto at : {0x63e00U, 0x63e08U, 0x63e10U}) {
            assert(changed.startup.substr(at - 0x3000, 8) == baseline.substr(at - 0x3000, 8));
        }

        assert(changed.startup.substr(0xe084 - 0x3000, 12) == baseline.substr(0xe084 - 0x3000, 12));
        std::uint32_t sum = 0;

        for (std::size_t i = 0; i < changed.startup.size(); i += 4) {
            sum += static_cast<std::uint32_t>(le(changed.startup, i));
        }

        assert(sum == 0);

        for (std::size_t i = 0; i < baseline.size(); ++i) {
            const auto a = i + 0x3000;
            const bool allowed = (a >= 205196 && a < 205196 + 20722) ||
                                 (a >= 0xe060 && a < 0xe064) || (a >= 0xe068 && a < 0xe06c) ||
                                 (a >= 0xe078 && a < 0xe080) || (a >= 0x63e18 && a < 0x63e20) ||
                                 (a >= 0x69104 && a < 0x69108);

            if (!allowed) {
                assert(changed.startup[i] == baseline[i]);
            }
        }

        for (const auto at : {0U, 4U, 8U, 12U, 16U, 20U, 24U, 28U, 32U, 36U, 0x4d0aU}) {
            auto bad = fdt;
            bad[at] ^= 1;
            rejects([&] {
                validate_qnx_passion_original_fdt_layout(bad);
            });
        }

        auto bad = fdt;
        bad[0x38] = char(0xff);
        rejects([&] {
            validate_qnx_passion_original_fdt_layout(bad);
        });
        rejects([&] {
            validate_qnx_passion_original_fdt_layout(fdt.substr(1));
        });
        rejects([&] {
            qnx_passion_11713_fdt(qnx_passion_11713_fdt(fdt));
        });
        auto wrong = original;
        wrong[0xe060] ^= 1;
        rejects([&] {
            prepare_qnx_passion_11713_bootstrap(wrong);
        });
        rejects([&] {
            prepare_qnx_passion_11713_bootstrap({});
        });

        // Complete chip16A dispatch, including the byte validator preceding the halfword table.
        validate_qnx_passion_11713_bootdev(original);
        const auto config_boot = prepare_qnx_passion_11713_config_bootstrap(original);
        assert(le(config_boot.startup, 0x3234 - 0x3000) == 0xd3411673);
        const auto signed_byte = [](unsigned char v) {
            return v < 128 ? int(v) : int(v) - 256;
        };
        const auto chip_target = 0x8570050cLL +
                                 static_cast<long long>(signed_byte(static_cast<unsigned char>(
                                         original.at(0x2c784 + 0x16a - 0x153)))) *
                                         4;
        assert(chip_target == 0x85700524LL);
        assert(le(original, 0x3324) == 0x7100227f && le(original, 0x3328) == 0x54ffff68);
        assert(le(original, 0x332c) == 0xb0000140 && le(original, 0x3330) == 0x91272000 &&
               le(original, 0x3334) == 0x38734800 && le(original, 0x3338) == 0x10000061 &&
               le(original, 0x333c) == 0x8b208820 && le(original, 0x3340) == 0xd61f0000);
        auto dispatch = [&](unsigned selector) {
            if (selector > 8) {
                return 0x85700514LL;
            }

            return 0x85700544LL + static_cast<long long>(signed_byte(static_cast<unsigned char>(
                                          original.at(0x2c7c8 + selector)))) *
                                          4;
        };

        for (unsigned selector = 0; selector < 32; ++selector) {
            const auto target = dispatch(selector);

            if (selector == 0 || selector == 1 || selector == 8) {
                assert(target == 0x8570056cLL);
            } else if (selector <= 5) {
                assert(target == 0x8570055cLL);
            } else {
                assert(target == 0x85700514LL);
            }
        }

        // Regression: the old model began at the halfword table and falsely accepted selector4.
        const auto half = static_cast<std::int16_t>(le(original, 0x2c7d4 + 4 * 2, 2));
        assert(0x8570055cLL + static_cast<long long>(half) * 4 == 0x8570056cLL &&
               dispatch(4) == 0x8570055cLL);
        assert(le(original, 0x336c) == 0x52800020 && le(original, 0x3370) == 0xb9000680);
        // No new selector, tag, validator, or startup checksum bytes are patched.
        assert(config_boot.startup == changed.startup);

        rejects([&] {
            validate_qnx_passion_11713_bootdev(original.substr(1));
        });
        auto reapplied = original;
        reapplied.replace(0x3000, config_boot.startup.size(), config_boot.startup);
        rejects([&] {
            prepare_qnx_passion_11713_config_bootstrap(reapplied);
        });

        if (argc == 6) {
            auto read = [](const char *path) {
                std::ifstream f(path, std::ios::binary);

                return std::string((std::istreambuf_iterator<char>(f)), {});
            };
            const auto host = read(argv[2]), reader = read(argv[3]), large_reader = read(argv[4]),
                       runtime = read(argv[5]);
            assert(sha256_bytes(host) ==
                   "b3dfc7b6028ff3d06514638f149cb558454be6ddbba76e2152a83d83e03d8266");
            const auto manager = read_qnx_imagefs_file(host, "bin/startupmgr"),
                       old_manager = adapt_qnx_passion_startupmgr_entry(manager, reader),
                       new_manager = adapt_qnx_passion_11713_startupmgr(manager, reader,
                                                                        large_reader);
            assert(old_manager.size() == new_manager.size());

            for (std::size_t i = 0; i < old_manager.size(); ++i) {
                if (!((i >= 0x10134 && i < 0x10138) || (i >= 0x30b58 && i < 0x30b5c) ||
                      (i >= 0x27018 && i < 0x2701c) || (i >= 0x27140 && i < 0x27144) ||
                      (i >= 0xf5f8 && i < 0xf5fc) || (i >= 0xf634 && i < 0xf638))) {
                    assert(old_manager[i] == new_manager[i]);
                }
            }

            validate_qnx_passion_11713_sync_dma(manager);
            std::string payload(2642 * 4096, 0);

            for (std::size_t i = 0; i < payload.size(); ++i) {
                payload[i] = char(i * 17);
            }

            const auto dma_old = dma_flow(old_manager, payload, 0, 0),
                       dma_new = dma_flow(new_manager, payload, 0, 0);
            assert(dma_old.waits == 1 && !dma_old.complete && dma_old.bytes == payload);
            assert(dma_new.waits == 0 && dma_new.complete && dma_new.bytes == payload);

            for (int status : {-1, 6}) {
                const auto failed = dma_flow(new_manager, payload, status, 0);
                assert(failed.error && !failed.complete && !failed.waits &&
                       failed.bytes == std::string(payload.size(), char(0xa5)));
            }

            const auto failed_done = dma_flow(new_manager, payload, 0, 6);
            assert(failed_done.error && !failed_done.complete && !failed_done.waits &&
                   failed_done.bytes == payload);
            assert(le(old_manager, 0x10134) == 0x34000240 &&
                   le(new_manager, 0x10134) == 0x34000320);

            for (auto at : {0x9b78U, 0x9b88U, 0x9b60U, 0x9b70U, 0x9c18U, 0x9ae0U}) {
                auto bad = manager;
                bad[at] ^= 1;
                rejects([&] {
                    validate_qnx_passion_11713_sync_dma(bad);
                });
            }

            rejects([&] {
                validate_qnx_passion_11713_sync_dma(new_manager);
            });
            validate_qnx_passion_11713_ifs2_names(manager);

            for (const auto at : {0x2bf88U, 0x2c15eU, 0x2c194U}) {
                auto drift = manager;
                drift[at] ^= 1;
                rejects([&] {
                    validate_qnx_passion_11713_ifs2_names(drift);
                });
            }

            rejects([&] {
                validate_qnx_passion_11713_ifs2_names(manager.substr(1));
            });

            for (const auto site : {0xf5f8U, 0xf634U}) {
                const auto w = le(new_manager, site);
                assert((w & 0x9f00001f) == 0x10000000);
                std::int64_t delta = (((w >> 5) & 0x7ffff) << 2) | ((w >> 29) & 3);

                if (delta & (1 << 20)) {
                    delta -= 1 << 21;
                }

                assert(std::int64_t(site) + delta == 0x2bf88);
                assert(manager.substr(0x2bf88, 5) == std::string_view("ifs2\0", 5));
            }

            for (const auto site :
                 {0xf5f0U, 0xf5f4U, 0xf5fcU, 0xf600U, 0xf630U, 0xf638U, 0xf63cU}) {
                assert(new_manager.substr(site, 4) == manager.substr(site, 4));
            }

            auto bad_reader = large_reader;
            bad_reader[4] ^= 1;
            rejects([&] {
                adapt_qnx_passion_11713_startupmgr(manager, reader, bad_reader);
            });
            bad_reader = large_reader;
            bad_reader[0x3c] ^= 1;
            rejects([&] {
                adapt_qnx_passion_11713_startupmgr(manager, reader, bad_reader);
            });
            rejects([&] {
                adapt_qnx_passion_11713_startupmgr(new_manager, reader, large_reader);
            });
            rejects([&] {
                adapt_qnx_passion_11713_startupmgr(manager, large_reader, large_reader);
            });

            validate_qnx_passion_11713_config_device(manager);
            const auto config_manager = adapt_qnx_passion_11713_config_startupmgr(manager, reader,
                                                                                  large_reader);
            assert(config_manager.substr(0x2ad2f, 18) ==
                   std::string("devb-ram") + std::string(10, 0));
            assert(config_manager.substr(0x2ad89, 4) == std::string_view("ram\0", 4));
            assert(config_manager.substr(0x2ad8d, 49) ==
                   std::string("capacity=65536,blksize=4096,address=0xE4FC0000") +
                           std::string(3, 0));
            assert(le(config_manager, 0xaaf0 + 16, 8) == 0x2ae6a &&
                   manager.substr(0x2ae6a, 15) == std::string_view("/dev/disk/uda0\0", 15));

            for (std::size_t i = 0; i < new_manager.size(); ++i) {
                if (!((i >= 0x2ad2f && i < 0x2ad41) || (i >= 0x2ad89 && i < 0x2adbe) ||
                      (i >= 0xab00 && i < 0xab08))) {
                    assert(config_manager[i] == new_manager[i]);
                }
            }

            assert(config_manager.substr(0xaaf0, 16) == manager.substr(0xaaf0, 16) &&
                   config_manager.substr(0xab08, 0xac28 - 0xab08) ==
                           manager.substr(0xab08, 0xac28 - 0xab08));
            assert(config_manager.substr(0x2ad41, 11) == manager.substr(0x2ad41, 11) &&
                   config_manager.substr(0x33620, 104) == manager.substr(0x33620, 104));

            for (const auto at :
                 {0x2ad2fU, 0x2ad41U, 0x2ad4cU, 0x2ad89U, 0x2ad8dU, 0x2ad20U, 0x2ae6aU, 0x33680U}) {
                auto bad = manager;
                bad[at] ^= 1;
                rejects([&] {
                    validate_qnx_passion_11713_config_device(bad);
                });
            }

            rejects([&] {
                validate_qnx_passion_11713_config_device(config_manager);
            });
            rejects([&] {
                validate_qnx_passion_11713_config_device(manager.substr(1));
            });
            const auto script = prepare_qnx_passion_11713_runtime(runtime);
            assert(script.starts_with("#!/bin/ksh\n") &&
                   script.find("capacity=2462832,address=0x7c008000") != std::string::npos &&
                   script.find("automount=hd0:/") != std::string::npos &&
                   script.find("export VOYEMU_PASSION_RAM64") == std::string::npos &&
                   script.find("LD_PRELOAD=/bin/qditest:secpol-preload.so") != std::string::npos &&
                   script.find("LD_PRELOAD=/bin/Snapdragon.rgb:secpol-preload.so") !=
                           std::string::npos &&
                   script.find("/bin/slm") != std::string::npos &&
                   script.find("/bin/sleep 0.1") != std::string::npos &&
                   script.find("/slm/ic_qt/ic_qt.xml") != std::string::npos &&
                   script.find("/usr/bin/ic_service") != std::string::npos);
            assert(script.find("/proc/boot/bin/") == std::string::npos &&
                   script.find("/proc/boot/lib64") == std::string::npos);
            auto bad_runtime = runtime;
            bad_runtime[0] ^= 1;
            rejects([&] {
                prepare_qnx_passion_11713_runtime(bad_runtime);
            });
            rejects([&] {
                prepare_qnx_passion_11713_runtime(script);
            });
        }

        std::cout
                << "B11713 metadata/FDT/own-table source guards passed; no guest or policy acceptance\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
