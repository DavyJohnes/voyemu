// Regression for the QNX cluster language relay. The relay is a unity fragment
// (language_relay.c); this harness injects the OEM/transport symbols it uses and
// asserts the verified mcd message 24 (setting id 3, bytes 8, destination 8)
// request, the value fence, the already-acknowledged suppression, the
// retry-on-native-failure rule, the malformed-reply fence, the unavailable-symbol
// fence and the truthfulness rule that a submission is never an observed effect.
#include <cassert>
#include <cstdio>
#include <cstring>

typedef unsigned long long u64;

struct FakePacket {
    unsigned out_len;
    unsigned char data[3840];
};
static FakePacket fake_packet;
static FakePacket *packet = &fake_packet;
static const char *relay_env = nullptr;
static char emitted[4096];
static u64 queued_seq = 0, queued_value = 0;
static bool malformed_reply = false;
static bool issue_available = true;
static unsigned issued_message = 0, issued_id = 0, issued_value = 0, issued_bytes = 0,
                issued_dest = 0, issue_count = 0;
static int issue_result = 0;
static unsigned report_count = 0;
static u64 report_seq = 0, report_issued = 0;

extern char *getenv(const char *name) {
    if (std::strcmp(name, "QGFX_LANGUAGE_RELAY") == 0) {
        return const_cast<char *>(relay_env);
    }

    return nullptr;
}

extern void *dlopen(const char *, int) {
    return reinterpret_cast<void *>(1);
}

static int fake_issue(unsigned message, void *data, unsigned bytes, unsigned destination) {
    const unsigned *request = static_cast<const unsigned *>(data);

    issued_message = message;
    issued_id = request[0];
    issued_value = request[1];
    issued_bytes = bytes;
    issued_dest = destination;
    ++issue_count;

    return issue_result;
}

extern void *dlsym(void *, const char *symbol) {
    if (issue_available && std::strcmp(symbol, "_Z18engnCore_issue2mcdjPvjj") == 0) {
        return reinterpret_cast<void *>(&fake_issue);
    }

    return nullptr;
}

static void emit(const char *text) {
    std::strncat(emitted, text, sizeof(emitted) - std::strlen(emitted) - 1);
}

static void acquire() {}

static void release() {}

static u64 call(unsigned op, const u64 *args, unsigned, const void *, unsigned) {
    if (op == 366) {
        report_seq = args[0];
        report_issued = args[1];
        ++report_count;

        return 1;
    }

    if (op != 365) {
        fake_packet.out_len = 0;

        return 0;
    }

    if (queued_seq) {
        if (malformed_reply) {
            fake_packet.out_len = 3;
            fake_packet.data[0] = 0;
            fake_packet.data[1] = 0;
            fake_packet.data[2] = 0;
        } else {
            fake_packet.out_len = 1;
            fake_packet.data[0] = static_cast<unsigned char>(queued_value);
        }
    } else {
        fake_packet.out_len = 0;
    }

    return queued_seq;
}

extern int usleep(unsigned) {
    return 0;
}

int getpid() {
    return 1;
}

extern int pthread_create(int *, const void *, void *(*)(void *), void *) {
    return 0;
}

extern int pthread_join(int, void **) {
    return 0;
}

#include "language_relay.c"

int main() {
    relay_env = nullptr;
    queued_seq = 5;
    queued_value = 0;
    relay_language();
    assert(issue_count == 0 && report_count == 0 && "disabled relay must not issue");

    relay_env = "1";
    queued_seq = 0;
    relay_language();
    assert(issue_count == 0 && "no queued command must not issue");

    // A value outside the OEM language domain is fenced and never submitted.
    queued_seq = 5;
    queued_value = 2;
    relay_language();
    assert(issue_count == 0 && report_count == 0 && "out-of-domain language must be fenced");

    // The verified ABI: mcd message 24, bytes 8, destination 8, setting id 3.
    queued_seq = 5;
    queued_value = 0;
    issue_result = 0;
    relay_language();
    assert(issue_count == 1 && issued_message == 24 && issued_bytes == 8 && issued_dest == 8 &&
           issued_id == 3 && issued_value == 0);
    assert(report_count == 1 && report_seq == 5 && report_issued == 1 && "submission reported");

    // A lower or equal sequence is suppressed (already submitted).
    queued_seq = 5;
    queued_value = 1;
    relay_language();
    assert(issue_count == 1 && report_count == 1 && "stale sequence must be suppressed");

    // A failed original request is reported as not-issued and retried, never acknowledged.
    queued_seq = 6;
    queued_value = 1;
    issue_result = -1;
    relay_language();
    assert(issue_count == 2 && report_count == 2 && report_seq == 6 && report_issued == 0 &&
           "native failure must report not-issued");
    queued_seq = 6;
    queued_value = 1;
    issue_result = 0;
    relay_language();
    assert(issue_count == 3 && report_count == 3 && report_issued == 1 &&
           "failed request must be retried");

    // A malformed reply length must be fenced: stale packet bytes are never a command.
    queued_seq = 7;
    queued_value = 0;
    malformed_reply = true;
    relay_language();
    assert(issue_count == 3 && report_count == 3 && "malformed reply length must be fenced");
    malformed_reply = false;

    // An unavailable original-setting symbol must fail closed, never guess.
    queued_seq = 7;
    queued_value = 0;
    issue_available = false;
    relay_language();
    assert(issue_count == 3 && report_count == 3 &&
           std::strstr(emitted, "unavailable=original-setting-api") != nullptr);
    issue_available = true;

    // The report only ever carries submission truth, never an observed language.
    assert(std::strstr(emitted, "value=1 native-setting-rc=0") != nullptr);
    start_language_worker(); // stub pthread_create records creation without running the loop
    std::printf("qnx language relay regression passed\n");

    return 0;
}
