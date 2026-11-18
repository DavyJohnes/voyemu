#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using hv_vcpu_t = std::uint64_t;
using hv_return_t = int;
constexpr hv_return_t HV_SUCCESS = 0;
constexpr unsigned HV_GIC_DISTRIBUTOR_REG_GICD_TYPER = 4;
enum MemTxResult {
    MEMTX_OK,
    MEMTX_ERROR
};
static unsigned dist_reads, dist_writes, redist_reads, redist_writes, syncs, writebacks;
static std::map<unsigned, std::uint64_t> dist_registers;

static hv_return_t hv_gic_get_distributor_reg(unsigned reg, std::uint64_t *value) {
    ++dist_reads;
    *value = dist_registers[reg];

    return HV_SUCCESS;
}

static hv_return_t hv_gic_set_distributor_reg(unsigned reg, std::uint64_t value) {
    ++dist_writes;
    dist_registers[reg] = value;

    return HV_SUCCESS;
}

static hv_return_t hv_gic_get_redistributor_reg(hv_vcpu_t, unsigned, std::uint64_t *value) {
    ++redist_reads;
    *value = 0;

    return HV_SUCCESS;
}

static hv_return_t hv_gic_set_redistributor_reg(hv_vcpu_t, unsigned, std::uint64_t) {
    ++redist_writes;

    return HV_SUCCESS;
}

// Generated from the actual owned bridge in qnx-host.patch, never a test copy.
#include VOYEMU_HVF_GIC_TRANSFER_HEADER

struct Env {
    bool a64 = true, big_endian = false;
};
struct ARMCPU {
    Env env;
};
struct Accel {
    hv_vcpu_t fd = 0;
};
struct CPUState {
    int cpu_index = 0;
    ARMCPU arm;
    Accel data;
    Accel *accel = &data;
};
struct AddressSpace {};
static CPUState current_cpu;
static std::uint64_t regs[33];
static std::uint32_t instruction = 0xf8008402U;
constexpr unsigned HV_REG_PC = 32;
constexpr unsigned MEMTXATTRS_UNSPECIFIED = 0;

static ARMCPU *ARM_CPU(CPUState *cpu) {
    return &cpu->arm;
}

static bool is_a64(Env *env) {
    return env->a64;
}

static bool arm_cpu_data_is_big_endian(Env *env) {
    return env->big_endian;
}

static void cpu_synchronize_state(CPUState *) {
    ++syncs;
}

static hv_return_t hv_vcpu_get_reg(hv_vcpu_t, unsigned reg, std::uint64_t *value) {
    *value = regs[reg];

    return HV_SUCCESS;
}

static int cpu_memory_rw_debug(CPUState *, std::uint64_t, std::uint8_t *bytes, unsigned length,
                               bool write) {
    if (length != 4 || write) {
        return -1;
    }

    std::memcpy(bytes, &instruction, 4);

    return 0;
}

static std::uint32_t ldl_le_p(const std::uint8_t *bytes) {
    std::uint32_t value;

    std::memcpy(&value, bytes, 4);

    return value;
}

static void stq_le_p(std::uint8_t *bytes, std::uint64_t value) {
    std::memcpy(bytes, &value, 8);
}

static std::uint64_t hvf_get_reg(CPUState *, unsigned reg) {
    return reg == 31 ? 0 : regs[reg];
}

static void hvf_set_reg(CPUState *, unsigned reg, std::uint64_t value) {
    ++writebacks;
    regs[reg] = value;
}

static void error_report(const char *, ...) {}

static MemTxResult address_space_write(AddressSpace *, std::uint64_t ipa, unsigned,
                                       const std::uint8_t *bytes, unsigned length) {
    if (ipa < 0x08000000 || ipa >= 0x08010000 || length > 8) {
        return MEMTX_ERROR;
    }

    std::uint64_t value = 0;
    std::memcpy(&value, bytes, length);
    HVFGICMMIO op = {};
    op.offset = static_cast<std::uint32_t>(ipa - 0x08000000);
    op.length = length;
    op.value = value;
    op.writing = true;
    hvf_gic_mmio_transfer(0, &op);

    return op.result;
}

// Actual patched QEMU decoder/handler with a fake CPU and actual bridge above.
#include VOYEMU_HVF_STR_WRITEBACK_HEADER

static void check(bool ok, std::string_view message) {
    if (!ok) {
        throw std::runtime_error(std::string(message));
    }
}

static unsigned calls() {
    return dist_reads + dist_writes + redist_reads + redist_writes;
}

static void reset() {
    dist_reads = dist_writes = redist_reads = redist_writes = syncs = writebacks = 0;
    dist_registers.clear();
    std::memset(regs, 0, sizeof(regs));
    current_cpu.arm.env = {};
    instruction = 0xf8008402U;
}

static HVFGICMMIO transfer(bool redist, unsigned offset, unsigned length, bool writing,
                           std::uint64_t value = 0) {
    HVFGICMMIO op = {};

    op.redist = redist;
    op.writing = writing;
    op.offset = offset;
    op.length = length;
    op.value = value;
    hvf_gic_mmio_transfer(0, &op);

    return op;
}

int main() {
    try {
        reset();

        for (unsigned offset : {0x6000U, 0x6008U, 0x60f8U}) {
            auto read = transfer(false, offset, 8, false, ~std::uint64_t{0});
            check(read.result == MEMTX_OK && read.value == 0,
                  "reserved IROUTER IRQ0..31 must read zero");
            auto write = transfer(false, offset, 8, true, ~std::uint64_t{0});
            check(write.result == MEMTX_OK && calls() == 0,
                  "reserved IROUTER IRQ0..31 must ignore writes without HV API");
        }

        for (unsigned offset : {0x6000U, 0x6004U, 0x60f8U, 0x60fcU}) {
            auto read = transfer(false, offset, 4, false, ~std::uint64_t{0});
            check(read.result == MEMTX_OK && read.value == 0, "reserved 32-bit lanes read zero");
            check(transfer(false, offset, 4, true, 0x12345678).result == MEMTX_OK && calls() == 0,
                  "reserved 32-bit writes ignored without API");
        }

        auto final_byte = transfer(false, 0x60ff, 1, false, 0xff);
        check(final_byte.result == MEMTX_OK && final_byte.value == 0 && calls() == 0,
              "reserved final byte lane");

        for (auto [offset, length] :
             {std::pair{0x6000U, 16U}, std::pair{0x6001U, 8U}, std::pair{0x60fcU, 8U},
              std::pair{0x6000U, 0U}, std::pair{0x6000U, 3U}}) {
            check(transfer(false, offset, length, true).result == MEMTX_ERROR && calls() == 0,
                  "invalid reserved access rejected");
        }

        check(transfer(false, 0x5ff8, 8, true).result == MEMTX_ERROR && calls() == 0,
              "previous unknown distributor register stays unknown");
        check(transfer(true, 0x6000, 8, true).result == MEMTX_ERROR && calls() == 0,
              "redistributor offset6000 stays unknown");
        check(transfer(false, 0x6100, 8, true, 0x1122334455667788ULL).result == MEMTX_OK &&
                      dist_writes == 1 && dist_reads == 0,
              "first valid SPI retains API write");
        auto first_spi = transfer(false, 0x6100, 8, false);
        check(first_spi.result == MEMTX_OK && first_spi.value == 0x1122334455667788ULL &&
                      dist_reads == 1,
              "first valid SPI retains API read");
        check(transfer(false, 0x6104, 4, true, 0xaabbccdd).result == MEMTX_OK &&
                      dist_registers[0x6100] == 0xaabbccdd55667788ULL,
              "valid SPI partial update preserves other lane");
        check(transfer(false, 0x7fd8, 8, true, 0x55).result == MEMTX_OK &&
                      dist_registers[0x7fd8] == 0x55,
              "last normal SPI route remains implemented");
        reset();
        unsigned rn, rt, length;
        std::int32_t offset;
        bool preindex;
        check(hvf_decode_str_writeback(instruction, &rn, &rt, &offset, &length, &preindex) &&
                      rn == 0 && rt == 2 && offset == 8 && length == 8 && !preindex,
              "actual OEM instruction decoded as STR X2,[X0],#8");
        AddressSpace space;
        regs[0] = 0x08006000;
        regs[2] = 0xfedcba9876543210ULL;
        regs[HV_REG_PC] = 0x85708214;
        check(hvf_handle_str_writeback_mmio(&current_cpu, &space, regs[0], regs[0]),
              "actual reserved IROUTER STR completed");
        check(regs[0] == 0x08006008 && writebacks == 1 && calls() == 0 &&
                      regs[2] == 0xfedcba9876543210ULL && regs[HV_REG_PC] == 0x85708214,
              "successful reserved store advances base once without changing PC/source or HV API");
        reset();
        regs[0] = 0x08006100;
        regs[2] = 0x1234;
        regs[HV_REG_PC] = 0x85708214;
        check(hvf_handle_str_writeback_mmio(&current_cpu, &space, regs[0], regs[0]) &&
                      regs[0] == 0x08006108 && dist_registers[0x6100] == 0x1234 && dist_writes == 1,
              "valid route store and writeback still reach API");
        reset();
        regs[0] = 0x08005ff8;
        regs[2] = 0x1234;
        check(!hvf_handle_str_writeback_mmio(&current_cpu, &space, regs[0], regs[0]) &&
                      regs[0] == 0x08005ff8 && writebacks == 0 && calls() == 0,
              "unknown bus rejection cannot advance base");
        reset();
        regs[0] = 0x08006001;
        check(!hvf_handle_str_writeback_mmio(&current_cpu, &space, regs[0], regs[0]) &&
                      writebacks == 0,
              "misaligned store rejected without writeback");
        reset();
        regs[0] = 0x08006000;
        current_cpu.arm.env.big_endian = true;
        check(!hvf_handle_str_writeback_mmio(&current_cpu, &space, regs[0], regs[0]) &&
                      writebacks == 0 && calls() == 0,
              "unsupported endian rejected");
        std::cout << "HVF reserved IROUTER and actual STR writeback regression passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
