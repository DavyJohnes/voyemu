#include "qnx_host_adapt.hpp"
#include "process.hpp"
#include "qnx_imagefs.hpp"
#include "qnx_lzo.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
void dma_need(bool ok, const char *why) {
    if (!ok) {
        throw std::runtime_error(why);
    }
}

std::string dma_read(const char *path) {
    std::ifstream file(path, std::ios::binary);

    dma_need(bool(file), "DMA fixture input");

    return {std::istreambuf_iterator<char>(file), {}};
}

std::uint32_t dma_word(std::string_view bytes, std::size_t at) {
    std::uint32_t w = 0;

    for (unsigned i = 0; i < 4; ++i) {
        w |= std::uint32_t(static_cast<unsigned char>(bytes.at(at + i))) << (i * 8);
    }

    return w;
}

struct DmaTrace {
    std::string output;
    bool error = false, complete = false;
    unsigned interrupt_wait = 0;
};

DmaTrace synchronous_dma_completion(std::string_view manager, std::string_view source, int status) {
    DmaTrace trace;

    trace.output.assign(source.size(), char(0xa5));

    // Model the pinned own RAM callback contract, then interpret the actual caller CBZ.
    if (status == 0) {
        std::copy(source.begin(), source.end(), trace.output.begin());
    }

    const auto instruction = dma_word(manager, 0xfa2c);
    dma_need((instruction & 0xff00001f) == 0x34000000, "DMA fixture CBZ W0");
    std::int32_t displacement = (instruction >> 5) & 0x7ffff;

    if (displacement & 0x40000) {
        displacement -= 0x80000;
    }

    const auto next = status == 0 ? 0xfa2c + displacement * 4 : 0xfa30;

    if (next == 0xfa74) {
        trace.interrupt_wait = 1;

        return trace;
    }

    if (next == 0xfa30) {
        trace.error = true;

        return trace;
    }

    dma_need(next == 0xfa90, "DMA fixture completion target");
    dma_need(dma_word(manager, 0xfa94) == 0x94001258 && dma_word(manager, 0xfadc) == 0x4b1b02d6 &&
                     dma_word(manager, 0xfae0) == 0x0b1b02f7,
             "DMA status/chunk accounting");
    trace.complete = true;

    return trace;
}

void qinggan11_dma_fixture(std::string_view original, std::string_view stub,
                           std::string_view baseline) {
    const auto output = qnx_qinggan11_developer_candidate(original, stub, std::string(64, '1'),
                                                          std::string(64, '2'));
    std::string payload(257, 0);

    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = char(i);
    }

    const auto old = synchronous_dma_completion(original, payload, 0);
    dma_need(!old.complete && old.interrupt_wait == 1 && old.output == payload,
             "old real copy must precede modeled IRQ stall");
    const auto completed = synchronous_dma_completion(output, payload, 0);
    dma_need(completed.complete && completed.interrupt_wait == 0 && completed.output == payload,
             "own RAM copy complete but OEM IRQ wait stalled");

    for (const auto status : {-1, 6}) {
        const auto failed = synchronous_dma_completion(output, payload, status);
        dma_need(failed.error && !failed.complete && failed.interrupt_wait == 0 &&
                         failed.output == std::string(payload.size(), char(0xa5)),
                 "copy error path/output changed");
    }

    const auto dispatch = adapt_qnx_qinggan11_startupmgr_dispatch(original, stub),
               gpt = adapt_qnx_qinggan11_startupmgr_gpt(original);
    dma_need(dma_word(dispatch, 0xfa2c) == 0x34000240 && dma_word(gpt, 0xfa2c) == 0x34000240,
             "default asynchronous stages changed");
    dma_need(output.size() == baseline.size(), "prepared comparison extent");

    for (std::size_t i = 0; i < output.size(); ++i) {
        if ((i < 0xfa2c || i >= 0xfa30) && (i < 0x86e8 || i >= 0x86f0) &&
            (i < 0x8898 || i >= 0x88a0)) {
            dma_need(output[i] == baseline[i], "unrelated prepared byte changed");
        }
    }

    for (const auto at : {0xfa28, 0xfa2c, 0xfa78, 0xfa94, 0x141dc, 0x143ec}) {
        auto bad = std::string(original);
        bad[at] ^= 1;
        bool refused = false;
        try {
            qnx_qinggan11_developer_candidate(bad, stub, std::string(64, '1'),
                                              std::string(64, '2'));
        } catch (...) {
            refused = true;
        }

        dma_need(refused, "DMA source mutation accepted");
    }

    bool replay = false;
    try {
        qnx_qinggan11_developer_candidate(output, stub, std::string(64, '1'), std::string(64, '2'));
    } catch (...) {
        replay = true;
    }

    dma_need(replay, "DMA reapply accepted");
    std::cout
            << "Actual old CBZ plus real synchronous copy stalls at modeled IRQ; adapted success completes with no IRQ; copy errors and other prepared bytes preserved\n";
}

std::uint64_t boot_word(std::string_view b, std::size_t at, unsigned width) {
    std::uint64_t v = 0;

    for (unsigned i = 0; i < width; ++i) {
        v |= std::uint64_t(static_cast<unsigned char>(b.at(at + i))) << (i * 8);
    }

    return v;
}

std::vector<std::string> boot_fields(std::string_view b) {
    const auto end = 0xd60 + boot_word(b, 0xd60, 2);
    std::vector<std::string> out;
    std::size_t at = 0xd6c;

    for (unsigned i = 0; i < 19; ++i) {
        const auto zero = b.find('\0', at);
        dma_need(zero != std::string_view::npos && zero < end, "declared boot field truncated");
        out.emplace_back(b.substr(at, zero - at));
        at = zero + 1;
    }

    dma_need(b.substr(at).find_first_not_of('\0') == std::string_view::npos,
             "boot field count/padding");

    return out;
}

void qinggan11_boot_fixture(std::string_view script, std::string_view manager,
                            std::string_view stub, std::string_view old_manager,
                            std::string_view old_script) {
    const auto out = adapt_qnx_qinggan11_boot_script(script),
               patched = qnx_qinggan11_developer_candidate(manager, stub, std::string(64, '1'),
                                                           std::string(64, '2'));
    const auto before = boot_fields(script), after = boot_fields(out);

    dma_need(out.size() > script.size() && out.size() <= 4096 &&
                     out.size() - script.size() <= 300 &&
                     out.substr(0, 0xd60) == old_script.substr(0, 0xd60),
             "boot changed earlier records/slot capacity");
    dma_need(out.substr(0xd62, 10) == script.substr(0xd62, 10) &&
                     out.size() == 0xd60 + boot_word(out, 0xd60, 2) + 4,
             "boot header/terminator semantics");

    for (unsigned i = 0; i < 19; ++i) {
        if (i == 5) {
            continue;
        }

        const auto expected =
                before[i].starts_with("PATH=")
                        ? "PATH=/proc/boot/bin:/proc/boot:/bin:/usr/bin:/sbin"
                : before[i].starts_with("LD_LIBRARY_PATH=")
                        ? "LD_LIBRARY_PATH=/proc/boot/lib64/:/proc/boot/lib64/dll/:/usr/lib64/:/lib64/"
                        : before[i];
        dma_need(after[i] == expected, "original argument/environment field lost");
    }

    dma_need(after.back().starts_with("PS1=") && after[5].ends_with(")&exec login"),
             "console login/environment lost");
    const auto &command = after[5];
    const auto pipe =
            command.find("test -e /dev/pipe||$b/pipe& w e /dev/pipe;echo x|read x||exit 1;");
    dma_need(pipe != std::string::npos &&
                     command.find("$b/pipe", command.find("$b/pipe") + 1) == std::string::npos,
             "real pipe single spawn/pipeline prerequisite missing");
    dma_need(command.find("for f in devb-ram screen ssplash ksh;do w x $b/$f;done;w r $c;") <
                             pipe &&
                     command.find("$b/screen -c $c&$b/ksh $b/ssplash") > pipe,
             "prerequisite/Screen/runtime order");

    for (const auto at : {0x86d8, 0x8888}) {
        dma_need(boot_word(patched, at + 16, 8) == 0x42138 &&
                         boot_word(old_manager, at + 16, 8) == 0x42100,
                 "display ownership not redirected");
        dma_need(patched.substr(at, 16) == manager.substr(at, 16),
                 "display relocation target/type changed");
    }

    dma_need(boot_word(manager, 0x32138, 8) == 0 && boot_word(manager, 0x9eb8, 8) == 0x40ac0,
             "empty vector/original Screen reference");

    for (std::size_t i = 0; i < patched.size(); ++i) {
        if ((i < 0x86e8 || i >= 0x86f0) && (i < 0x8898 || i >= 0x88a0)) {
            dma_need(patched[i] == old_manager[i],
                     "OEM worker/error/sentinel or unrelated manager byte changed");
        }
    }

    for (const auto at : {0xd60, 0xd64, 0xd6a, 0xd6b, 0xecf}) {
        auto bad = std::string(script);
        bad[at] ^= 1;
        bool refused = false;
        try {
            adapt_qnx_qinggan11_boot_script(bad);
        } catch (...) {
            refused = true;
        }

        dma_need(refused, "mutated boot record accepted");
    }

    bool refused = false;
    try {
        adapt_qnx_qinggan11_boot_script(out);
    } catch (...) {
        refused = true;
    }

    dma_need(refused, "boot record reapply accepted");
    std::cout
            << "Free final boot record preserves 5 arguments/13 environments; real pipe prerequisites, one display owner, OEM errors/sentinel and all other manager bytes retained\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 7 && std::string_view(argv[1]) == "--qinggan11-611") {
            const auto a = dma_read(argv[2]), b = dma_read(argv[3]), stub = dma_read(argv[6]);
            const auto patched = qnx_qinggan11_developer_candidate(a, stub, std::string(64, '1'),
                                                                   std::string(64, '2'));
            dma_need(patched.size() == a.size() &&
                             adapt_qnx_qinggan11_symlinks(b).size() == b.size(),
                     "6.11 host transforms");

            bool rejected = false;
            try {
                auto bad = a;
                bad[0xfa2c] ^= 1;
                qnx_qinggan11_developer_candidate(bad, stub, std::string(64, '1'),
                                                  std::string(64, '2'));
            } catch (...) {
                rejected = true;
            }

            dma_need(rejected, "6.11 mutated host accepted");
            std::cout << "6.11 host transformations and mutation guard passed\n";

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-boot-baseline") {
            const auto out = adapt_qnx_qinggan11_boot_script(dma_read(argv[2]));
            dma_need(!std::filesystem::exists(argv[3]), "fresh boot output");
            std::ofstream file(argv[3], std::ios::binary);
            file.write(out.data(), out.size());
            file.close();
            dma_need(bool(file), "boot output write");

            return 0;
        }

        if (argc == 7 && std::string_view(argv[1]) == "--qinggan11-boot") {
            qinggan11_boot_fixture(dma_read(argv[2]), dma_read(argv[3]), dma_read(argv[4]),
                                   dma_read(argv[5]), dma_read(argv[6]));

            return 0;
        }

        if (argc == 5 && std::string_view(argv[1]) == "--qinggan11-dma-baseline") {
            const auto output =
                    qnx_qinggan11_developer_candidate(dma_read(argv[2]), dma_read(argv[3]),
                                                      std::string(64, '1'), std::string(64, '2'));
            dma_need(!std::filesystem::exists(argv[4]), "fresh DMA baseline");
            std::ofstream file(argv[4], std::ios::binary);
            file.write(output.data(), output.size());
            file.close();
            dma_need(bool(file), "DMA baseline write");

            return 0;
        }

        if (argc == 5 && std::string_view(argv[1]) == "--qinggan11-sync-dma") {
            qinggan11_dma_fixture(dma_read(argv[2]), dma_read(argv[3]), dma_read(argv[4]));

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-developer") {
            auto read = [](const char *path) {
                std::ifstream f(path, std::ios::binary);

                return std::string((std::istreambuf_iterator<char>(f)), {});
            };
            const auto original = read(argv[2]), stub = read(argv[3]);
            const std::string display(64, '1'), disk(64, '2');
            const auto output = qnx_qinggan11_developer_candidate(original, stub, display, disk),
                       dispatch = adapt_qnx_qinggan11_startupmgr_dispatch(original, stub),
                       gpt = adapt_qnx_qinggan11_startupmgr_gpt(original);
            auto need = [](bool ok) {
                if (!ok) {
                    throw std::runtime_error("developer candidate invariant");
                }
            };
            auto word = [&](std::size_t at, unsigned n) {
                std::uint64_t v = 0;

                for (unsigned i = 0; i < n; ++i) {
                    v |= std::uint64_t(static_cast<unsigned char>(output.at(at + i))) << (8 * i);
                }

                return v;
            };
            need(output.size() == original.size());
            need(word(0xf148, 4) == 0x91183000 && word(0xf184, 4) == 0x91183000 &&
                 word(0x1d39c, 4) == 0x52a00020);
            need(word(0x8eb0, 8) == 0x2a334 && word(0xa500, 8) == 0x2a334 &&
                 word(0xa4e8, 8) == 0x2a315);
            need(output.substr(0x2a334, 5) == std::string("pipe\0", 5));
            need(output.substr(0x2a5fd, 5) == original.substr(0x2a5fd, 5) && output[0x2a5f9] == 0);
            need(output.substr(0x28008, 0x8c) == original.substr(0x28008, 0x8c));
            need(output.substr(0x14150, 0x20) == original.substr(0x14150, 0x20));

            for (std::size_t at = 0; at < output.size(); ++at) {
                if (output[at] != original[at]) {
                    bool allowed = dispatch[at] != original[at] || gpt[at] != original[at];

                    for (const auto &[lo, hi] :
                         std::array<std::pair<std::size_t, std::size_t>, 16>{{{0x86e8, 0x86f0},
                                                                              {0x8898, 0x88a0},
                                                                              {0xfa2c, 0xfa30},
                                                                              {0xf148, 0xf14c},
                                                                              {0xf184, 0xf188},
                                                                              {0x14170, 0x14174},
                                                                              {0xed38, 0xed3c},
                                                                              {0x2a55b, 0x2a57c},
                                                                              {0x2a620, 0x2a655},
                                                                              {0x2a315, 0x2a339},
                                                                              {0x2a5f9, 0x2a5fa},
                                                                              {0x8eb0, 0x8eb8},
                                                                              {0xa500, 0xa508},
                                                                              {0x29ec5, 0x29f05},
                                                                              {0x29fb4, 0x29ff4},
                                                                              {0, 0}}}) {
                        allowed |= at >= lo && at < hi;
                    }

                    need(allowed);
                }
            }

            for (unsigned which = 0; which < 7; ++which) {
                bool rejected = false;
                try {
                    if (which == 0) {
                        qnx_qinggan11_developer_candidate(output, stub, display, disk);
                    } else if (which == 1) {
                        auto bad = original;
                        bad[0x2a5fd] ^= 1;
                        qnx_qinggan11_developer_candidate(bad, stub, display, disk);
                    } else if (which == 2) {
                        qnx_qinggan11_developer_candidate(original, stub, "bad", disk);
                    } else if (which < 6) {
                        auto bad = original;
                        bad[which == 4 ? 0xf14c : 0xf188] ^= 1;
                        qnx_qinggan11_developer_candidate(bad, stub, display, disk);
                    } else {
                        qnx_qinggan11_developer_candidate("", stub, display, disk);
                    }
                } catch (...) {
                    rejected = true;
                }

                need(rejected);
            }

            std::cout
                    << "Developer-only A11 candidate source/ABI/shared-string/reference/changed-range guards passed; guest untested; SHA256 "
                    << sha256_bytes(output) << '\n';

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--passion-symlinks") {
            const auto original = dma_read(argv[2]), output = adapt_qnx_passion_symlinks(original);
            dma_need(output.size() == original.size() && dma_word(output, 0x1034) == 0x1400000a &&
                             dma_word(output, 0x1488) == 0x90000000 &&
                             dma_word(output, 0x148c) == 0x91314000 &&
                             dma_word(output, 0x1548) == 0xd503201f,
                     "passion symlink transform");
            dma_need(output.substr(0x1c50, 24) == "sa8155_mega_mon_v11.0.33" &&
                             dma_word(output, 0x1c68) == 0,
                     "passion symlink suffix");
            auto qword = [&](std::size_t at) {
                std::uint64_t v = 0;

                for (unsigned i = 0; i < 8; ++i) {
                    v |= std::uint64_t(static_cast<unsigned char>(output.at(at + i))) << (8 * i);
                }

                return v;
            };
            dma_need(qword(0xd0) == 0x1c70 && qword(0xd8) == 0x1c70, "passion symlink phdr extent");
            unsigned changed = 0;

            for (std::size_t at = 0; at < output.size(); ++at) {
                changed += output[at] != original[at];
            }

            dma_need(changed == 40, "passion symlink changed range");

            for (unsigned which = 0; which < 2; ++which) {
                bool rejected = false;
                try {
                    if (which == 0) {
                        auto bad = original;
                        bad[0x1034] ^= 1;
                        adapt_qnx_passion_symlinks(bad);
                    } else {
                        adapt_qnx_passion_symlinks(output);
                    }
                } catch (...) {
                    rejected = true;
                }

                dma_need(rejected, "passion symlink mutation or replay accepted");
            }

            std::cout << "Exact Passion SA8155 symlink vector selection; SHA256 "
                      << sha256_bytes(output) << '\n';

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11") {
            auto read = [](const std::filesystem::path &file) {
                std::ifstream input(file, std::ios::binary);

                if (!input) {
                    throw std::runtime_error("A11 external fixture open");
                }

                return std::string((std::istreambuf_iterator<char>(input)), {});
            };
            const auto directory = std::filesystem::canonical(argv[2]);
            const auto script = read(directory / ".script"),
                       procnto = read(directory / "procnto-smp-instr");
            const auto symlinks = read(directory / "make_symlinks");
            const auto new_symlinks = adapt_qnx_qinggan11_symlinks(symlinks);

            if (new_symlinks.size() != symlinks.size() ||
                new_symlinks.find("/lib64/libdalconfig-sa8155_adp_star_v1.0.0.so") ==
                        std::string::npos) {
                throw std::runtime_error("A11 symlink transform");
            }

            for (unsigned which = 0; which < 3; ++which) {
                bool rejected = false;
                try {
                    if (which == 0) {
                        adapt_qnx_symlinks(symlinks);
                    } else if (which == 1) {
                        adapt_qnx_qinggan11_symlinks(new_symlinks);
                    } else {
                        auto bad = symlinks;
                        bad[0xf40] ^= 1;
                        adapt_qnx_qinggan11_symlinks(bad);
                    }
                } catch (...) {
                    rejected = true;
                }

                if (!rejected) {
                    throw std::runtime_error("A11 symlink family/mutation guard");
                }
            }

            const auto new_script = adapt_qnx_qinggan11_boot_script(script),
                       new_procnto = adapt_qnx_qinggan11_procnto_environment(procnto);

            if ((new_script.size() <= script.size() || new_script.size() > 4096) ||
                new_procnto.size() != procnto.size() ||
                new_script.find("PATH=/proc/boot/bin:") == std::string::npos ||
                new_procnto.find("LD_LIBRARY_PATH=/proc/boot/lib64:") == std::string::npos) {
                throw std::runtime_error("A11 environment transform");
            }

            std::cout
                    << "Exact A11 script/procnto environment transforms and symlink family/mutation guards passed; "
                    << sha256_bytes(new_script) << ' ' << sha256_bytes(new_procnto) << ' '
                    << sha256_bytes(new_symlinks) << '\n';

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-dispatch") {
            auto read = [](const std::filesystem::path &path) {
                std::ifstream input(path, std::ios::binary);

                if (!input) {
                    throw std::runtime_error("dispatch fixture open");
                }

                return std::string((std::istreambuf_iterator<char>(input)), {});
            };
            const auto original = read(argv[2]), stub = read(argv[3]);
            const auto output = adapt_qnx_qinggan11_startupmgr_dispatch(original, stub),
                       gpt = adapt_qnx_qinggan11_startupmgr_gpt(original);

            if (gpt.size() != original.size()) {
                throw std::runtime_error("GPT stage extent");
            }

            bool gpt_failed = false;
            try {
                adapt_qnx_qinggan11_startupmgr_gpt(gpt);
            } catch (...) {
                gpt_failed = true;
            }

            if (!gpt_failed) {
                throw std::runtime_error("GPT replay accepted");
            }

            if (output.size() != original.size()) {
                throw std::runtime_error("dispatch extent changed");
            }

            for (unsigned which = 0; which < 2; ++which) {
                bool failed = false;
                try {
                    if (which == 0) {
                        adapt_qnx_qinggan11_startupmgr_dispatch(output, stub);
                    } else {
                        auto changed = original;
                        changed[0x9878] ^= 1;
                        adapt_qnx_qinggan11_startupmgr_dispatch(changed, stub);
                    }
                } catch (...) {
                    failed = true;
                }

                if (!failed) {
                    throw std::runtime_error("dispatch input guard accepted");
                }
            }

            std::cout
                    << "Exact A11 RAM dispatch stage and source/stub mutation guards passed; not a complete startupmgr adapter; "
                    << sha256_bytes(output) << ' ' << sha256_bytes(gpt) << '\n';

            return 0;
        }

        if (argc == 5 && std::string(argv[1]) == "--run") {
            const auto root = std::filesystem::canonical(argv[2]),
                       built = std::filesystem::canonical(argv[3]),
                       reference = std::filesystem::canonical(argv[4]);
            auto read = [](const std::filesystem::path &file) {
                std::ifstream in(file, std::ios::binary);

                if (!in) {
                    throw std::runtime_error("input open");
                }

                return std::string((std::istreambuf_iterator<char>(in)), {});
            };
            const auto hyp =
                    read(root / ".local/profiles/free-09-chinese-20260106-6.0.3/qnx/hyp_la.img");

            if (sha256_bytes(hyp) !=
                "0af64f396f7de8dafe3488f21ec7e4cd989b3d02d3de0412aec020fdb1424b89") {
                throw std::runtime_error("rawhyp pin");
            }

            std::uint32_t end = 0;

            for (unsigned i = 0; i < 4; ++i) {
                end |= std::uint32_t(static_cast<unsigned char>(hyp.at(0x3024 + i))) << (i * 8);
            }

            const auto host = decode_qnx_boot_stream(hyp.substr(0x5a108, end - 0x57108)),
                       ifs = read(reference / "ifs2-native.img");
            const auto script = adapt_qnx_boot_script(
                               read_qnx_imagefs_file(host, "proc/boot/.script")),
                       procnto = adapt_qnx_procnto_environment(
                               read_qnx_imagefs_file(host, "proc/boot/procnto-smp-instr")),
                       symlinks =
                               adapt_qnx_symlinks(read_qnx_imagefs_file(host, "bin/make_symlinks")),
                       manager = adapt_qnx_startupmgr(
                               read_qnx_imagefs_file(host, "bin/startupmgr"),
                               read(built / "readstub.bin"),
                               sha256_bytes(read_qnx_imagefs_file(ifs, "ifs_display.img")),
                               sha256_bytes(read_qnx_imagefs_file(ifs, "ifs_disk.img")));
            const auto diagnostic = root / ".local/build/tmp/startupmgr-adapt-parity-v1.bin";

            if (!std::filesystem::exists(diagnostic)) {
                std::ofstream out(diagnostic, std::ios::binary);
                out.write(manager.data(), manager.size());
            }

            for (const auto &[expected, bytes] : std::array<std::pair<std::string, std::string>, 4>{
                         {{"59ab22811f12bb7a3c72b46f8a5657def480b04f0d8f379521343945ffec9f1d",
                           script},
                          {"dff3442b6a7a01b6beae217b0e6228a4061158598114db3e7482e1dd8b621792",
                           procnto},
                          {"44e28c038797d5e2cb620c5bb4fc96499ea68f346b150d09c6b54758c5dead70",
                           symlinks},
                          {"0838fce14c91ad59a80e11506a391bfedc964e9c68aa03c1d2147fd59e39002a",
                           manager}}}) {
                if (sha256_bytes(bytes) != expected) {
                    throw std::runtime_error("source adaptation SHA differs: " +
                                             sha256_bytes(bytes) + " expected " + expected);
                }
            }

            std::cout
                    << "all four host IFS components reconstructed from OEM bytes; exact accepted component equality\n";

            return 0;
        }

        bool failed = false;
        try {
            adapt_qnx_startupmgr("", "", "", "");
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("unknown firmware accepted");
        }

        std::cout << "QNX host adapter input guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
