#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using u64 = unsigned long long;
using usize = unsigned long;
struct Packet {
    unsigned fault = 0, out_len = 0;
    unsigned char data[32]{};
};
static Packet storage, *packet = &storage;
static unsigned uniform_hits, uniform_missing_table, uniform_unknown_name, uniform_evictions;
static unsigned mode_now = 1, skin_now = 1, mode_offer = 2, skin_offer = 1001, extent = 8, fault;
static u64 offered = 1;
static unsigned queries, set_modes, set_skins;
static int mode_rc, skin_rc;
static bool bad_readback, entry_required;
static std::string missing;
static std::array<u64, 14> report;
static std::vector<std::array<unsigned, 2>> requests;

static unsigned get_mode() {
    return mode_now;
}

static unsigned get_skin() {
    return skin_now;
}

static void set_mode(unsigned v) {
    ++set_modes;

    if (!bad_readback) {
        mode_now = v;
    }
}

static void set_skin(unsigned v) {
    ++set_skins;
    skin_now = v;
}

static int issue(unsigned op, void *data, unsigned n, unsigned destination) {
    assert(op == 24 && n == 8 && destination == 8);
    unsigned *words = (unsigned *)data;
    requests.push_back({words[0], words[1]});

    return words[0] == 4 ? mode_rc : skin_rc;
}

static void change() {}

static unsigned library_opens, symbol_lookups;

static void *test_dlopen(const char *, int) {
    ++library_opens;

    return (void *)1;
}

static void *test_dlsym(void *, const char *n) {
    ++symbol_lookups;

    if (missing == n) {
        return nullptr;
    }

    if (!std::strcmp(n, "_ZN12CSettingInfo12getTheme_iviEv")) {
        return (void *)get_mode;
    }

    if (!std::strcmp(n, "_ZN12CSettingInfo12getThemeskinEv")) {
        return (void *)get_skin;
    }

    if (!std::strcmp(n, "_ZN12CSettingInfo8setThemeEj")) {
        return (void *)set_mode;
    }

    if (!std::strcmp(n, "_ZN12CSettingInfo12setThemeskinEj")) {
        return (void *)set_skin;
    }

    if (!std::strcmp(n, "_Z18engnCore_issue2mcdjPvjj")) {
        return (void *)issue;
    }

    if (!std::strcmp(n, "_ZN7CCommon14changethemeReqEv")) {
        return (void *)change;
    }

    return nullptr;
}

static char *test_getenv(const char *n) {
    if (!std::strcmp(n, "QGFX_THEME_RELAY")) {
        return (char *)"1";
    }

    if (!std::strcmp(n, "QGFX_THEME_REQUEST_ENTRY") && entry_required) {
        return (char *)"1";
    }

    return nullptr;
}

static int test_getpid() {
    return 7;
}

static void acquire() {}

static void release() {}

static void emit(const char *) {}

static void copy(void *d, const void *s, usize n) {
    std::memcpy(d, s, n);
}

static u64 call(unsigned op, const u64 *a, unsigned n, const void *, unsigned) {
    assert(op == 355 && n == 14 && a[6] == 0x6110001);
    ++queries;
    std::copy(a, a + 14, report.begin());
    storage = {};
    storage.fault = fault;
    storage.out_len = extent;
    unsigned values[] = {mode_offer, skin_offer};
    std::memcpy(storage.data, values, 8);

    return offered;
}

#define QGFX_QINGGAN11_611 1
#define getenv test_getenv
#define getpid test_getpid
#define dlopen test_dlopen
#define dlsym test_dlsym
#include "theme_relay.c"
#include <exception>
#undef getenv
#undef getpid
#undef dlopen
#undef dlsym

static void reset() {
    mode_now = skin_now = 1;
    mode_offer = 2;
    skin_offer = 1001;
    extent = 8;
    fault = 0;
    offered = 1;
    queries = set_modes = set_skins = 0;
    mode_rc = skin_rc = 0;
    bad_readback = entry_required = false;
    missing.clear();
    requests.clear();
    report = {};
    qinggan11_theme_ack = qinggan11_theme_attempted = 0;
    qinggan11_theme_outcome = qinggan11_theme_after_mode = qinggan11_theme_after_skin = 0;
}

int main() try {
    const std::vector<std::array<unsigned, 2>> expected{{4, 2}, {13, 1001}};

    // A resolved original stays resolved for the process, so the missing getter runs first.
    reset();
    missing = "_ZN12CSettingInfo12getThemeskinEv";
    relay_theme();
    assert(!queries && !set_modes && !set_skins && !qinggan11_theme_ack);
    reset();
    relay_theme();
    assert(qinggan11_theme_ack == 1 && qinggan11_theme_outcome == 4 && requests == expected);
    relay_theme();
    assert(requests.size() == 2 && report[0] == 1 && report[1] == 2 && report[7] == 1001 &&
           report[8] == 1 && report[9] == 4);
    reset();
    mode_now = mode_offer = 4;
    skin_now = 3001;
    skin_offer = 3002;
    relay_theme();
    assert(qinggan11_theme_ack == 1 && skin_now == 3002 && requests[1][1] == 3002);

    for (unsigned invalid : {0, 1002, 2999, 3004, 6000}) {
        reset();
        skin_offer = invalid;
        relay_theme();
        assert(!set_modes && !set_skins && requests.empty() && !qinggan11_theme_ack);
    }

    reset();
    mode_offer = 2;
    skin_offer = 3001;
    relay_theme();
    assert(!set_modes && !set_skins && !qinggan11_theme_ack);

    for (unsigned bytes : {0, 1, 4, 7, 9}) {
        reset();
        extent = bytes;
        relay_theme();
        assert(!set_modes && !set_skins && !qinggan11_theme_ack);
    }

    reset();
    fault = 1;
    relay_theme();
    assert(!set_modes && !qinggan11_theme_ack);
    reset();
    offered = 0x100000000ULL;
    relay_theme();
    assert(!set_modes && !qinggan11_theme_ack);
    reset();
    mode_rc = -1;
    relay_theme();
    assert(set_modes == 1 && !set_skins && requests.size() == 1 && qinggan11_theme_outcome == 1 &&
           !qinggan11_theme_ack);
    relay_theme();
    assert(requests.size() == 1 && report[9] == 1);
    reset();
    skin_rc = -2;
    relay_theme();
    assert(set_modes == 1 && set_skins == 1 && requests.size() == 2 &&
           qinggan11_theme_outcome == 2 && !qinggan11_theme_ack);
    relay_theme();
    assert(requests.size() == 2 && report[9] == 2);
    reset();
    bad_readback = true;
    relay_theme();
    assert(qinggan11_theme_outcome == 3 && !qinggan11_theme_ack);
    relay_theme();
    assert(requests.size() == 2 && report[9] == 3);
    reset();
    entry_required = true;
    missing = "_ZN7CCommon14changethemeReqEv";
    relay_theme();
    assert(qinggan11_theme_outcome == 5 && !qinggan11_theme_ack);
    // Each original of libenginecore is resolved once per process: a dlopen per poll leaves a
    // loader handle behind that every later dlsym walks under the loader lock.
    const unsigned opened = library_opens, resolved = symbol_lookups;

    for (unsigned i = 0; i < 1000; ++i) {
        reset();
        relay_theme();
    }

    assert(library_opens == opened && symbol_lookups == resolved);
    std::cout
            << "QGFX_QINGGAN11_611 mode4 + skin13, pre-mutation contract, partial failures and no false ACK passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
