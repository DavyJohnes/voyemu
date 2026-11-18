/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Modified for Voyemu; sources.json records the upstream revision and SHA-256.

#define TRACE_TAG ADB
#include "sysdeps.h"
#include "adb.h"
#include "adb_auth.h"
#include "adb_utils.h"
#include "transport.h"
#include <android-base/strings.h>
#include <cstring>
#include <chrono>
#include <thread>
const char* adb_device_banner = "host";
uint32_t calculate_apacket_checksum(const apacket* p) {
    uint32_t sum = 0;
    for (size_t i = 0; i < p->msg.data_length; ++i) {
        sum += static_cast<uint8_t>(p->payload[i]);
    }
    return sum;
}

std::string command_to_string(uint32_t cmd) {
    switch (cmd) {
        case A_SYNC:
            return "A_SYNC";
        case A_CNXN:
            return "A_CNXN";
        case A_OPEN:
            return "A_OPEN";
        case A_OKAY:
            return "A_OKAY";
        case A_CLSE:
            return "A_CLSE";
        case A_WRTE:
            return "A_WRTE";
        case A_AUTH:
            return "A_AUTH";
        case A_STLS:
            return "A_STLS";
        default:
            return "UNKNOWN (" + std::to_string(cmd) + ")";
    }
}

std::string to_string(ConnectionState state) {
    switch (state) {
        case kCsOffline:
            return "offline";
        case kCsBootloader:
            return "bootloader";
        case kCsDevice:
            return "device";
        case kCsHost:
            return "host";
        case kCsRecovery:
            return "recovery";
        case kCsRescue:
            return "rescue";
        case kCsNoPerm:
            return "no-permissions";
        case kCsSideload:
            return "sideload";
        case kCsUnauthorized:
            return "unauthorized";
        case kCsAuthorizing:
            return "authorizing";
        case kCsConnecting:
            return "connecting";
        case kCsDetached:
            return "detached";
        case kCsAny:
            return "any";
    }
}

apacket* get_apacket() {
    apacket* p = new apacket();
    if (p == nullptr) {
        LOG(FATAL) << "failed to allocate an apacket";
    }

    memset(&p->msg, 0, sizeof(p->msg));
    return p;
}

void put_apacket(apacket *p)
{
    delete p;
}

void handle_online(atransport *t)
{
    D("adb: online");
    t->online = 1;
#if ADB_HOST
    t->SetConnectionEstablished(true);
#elif defined(__ANDROID__)
    IncrementActiveConnections();
#endif
}

void handle_offline(atransport *t)
{
    if (t->GetConnectionState() == kCsOffline) {
        VLOG(ADB) << t->serial_name() << ": already offline";
        return;
    }

    VLOG(ADB) << t->serial_name() << ": offline";

#if !ADB_HOST && defined(__ANDROID__)
    DecrementActiveConnections();
#endif

    t->SetConnectionState(kCsOffline);

    // Close the associated usb
    t->online = 0;

    // This is necessary to avoid a race condition that occurred when a transport closes
    // while a client socket is still active.
    close_all_sockets(t);

    t->RunDisconnects();
}

#if DEBUG_PACKETS
#define DUMPMAX 32
void print_packet(const char *label, apacket *p)
{
    const char* tag;
    unsigned count;

    switch(p->msg.command){
    case A_SYNC: tag = "SYNC"; break;
    case A_CNXN: tag = "CNXN" ; break;
    case A_OPEN: tag = "OPEN"; break;
    case A_OKAY: tag = "OKAY"; break;
    case A_CLSE: tag = "CLSE"; break;
    case A_WRTE: tag = "WRTE"; break;
    case A_AUTH: tag = "AUTH"; break;
    case A_STLS:
        tag = "STLS";
        break;
    default: tag = "????"; break;
    }

    fprintf(stderr, "%s: %s %08x %08x %04x \"",
            label, tag, p->msg.arg0, p->msg.arg1, p->msg.data_length);
    count = p->msg.data_length;
    const char* x = p->payload.data();
    if (count > DUMPMAX) {
        count = DUMPMAX;
        tag = "\n";
    } else {
        tag = "\"\n";
    }
    while (count-- > 0) {
        if ((*x >= ' ') && (*x < 127)) {
            fputc(*x, stderr);
        } else {
            fputc('.', stderr);
        }
        x++;
    }
    fputs(tag, stderr);
}
#endif

void send_ready(unsigned local, unsigned remote, atransport* t, uint32_t ack_bytes) {
    D("Calling send_ready");
    apacket *p = get_apacket();
    p->msg.command = A_OKAY;
    p->msg.arg0 = local;
    p->msg.arg1 = remote;
    if (t->SupportsDelayedAck()) {
        p->msg.data_length = sizeof(ack_bytes);
        p->payload.resize(sizeof(ack_bytes));
        memcpy(p->payload.data(), &ack_bytes, sizeof(ack_bytes));
    }

    send_packet(p, t);
}

static void send_close(unsigned local, unsigned remote, atransport *t)
{
    D("Calling send_close");
    apacket *p = get_apacket();
    p->msg.command = A_CLSE;
    p->msg.arg0 = local;
    p->msg.arg1 = remote;
    send_packet(p, t);
}

std::string get_connection_string() {
    std::vector<std::string> connection_properties;

#if !ADB_HOST
    static const char* cnxn_props[] = {
        "ro.product.name",
        "ro.product.model",
        "ro.product.device",
    };

    for (const auto& prop : cnxn_props) {
        std::string value = std::string(prop) + "=" + android::base::GetProperty(prop, "");
        connection_properties.push_back(value);
    }
#endif

    connection_properties.push_back(android::base::StringPrintf(
        "features=%s", FeatureSetToString(supported_features()).c_str()));

    return android::base::StringPrintf(
        "%s::%s", adb_device_banner,
        android::base::Join(connection_properties, ';').c_str());
}

void send_tls_request(atransport* t) {
    D("Calling send_tls_request");
    apacket* p = get_apacket();
    p->msg.command = A_STLS;
    p->msg.arg0 = A_STLS_VERSION;
    p->msg.data_length = 0;
    send_packet(p, t);
}

void send_connect(atransport* t) {
    D("Calling send_connect");
    apacket* cp = get_apacket();
    cp->msg.command = A_CNXN;
    // Send the max supported version, but because the transport is
    // initialized to A_VERSION_MIN, this will be compatible with every
    // device.
    cp->msg.arg0 = A_VERSION;
    cp->msg.arg1 = t->get_max_payload();

    std::string connection_str = get_connection_string();
    // Connect and auth packets are limited to MAX_PAYLOAD_V1 because we don't
    // yet know how much data the other size is willing to accept.
    if (connection_str.length() > MAX_PAYLOAD_V1) {
        LOG(FATAL) << "Connection banner is too long (length = "
                   << connection_str.length() << ")";
    }

    cp->payload.assign(connection_str.begin(), connection_str.end());
    cp->msg.data_length = cp->payload.size();

    send_packet(cp, t);
}

void parse_banner(const std::string& banner, atransport* t) {
    D("parse_banner: %s", banner.c_str());

    // The format is something like:
    // "device::ro.product.name=x;ro.product.model=y;ro.product.device=z;".
    std::vector<std::string> pieces = android::base::Split(banner, ":");

    // Reset the features list or else if the server sends no features we may
    // keep the existing feature set (http://b/24405971).
    t->SetFeatures("");

    if (pieces.size() > 2) {
        const std::string& props = pieces[2];
        for (const auto& prop : android::base::Split(props, ";")) {
            // The list of properties was traditionally ;-terminated rather than ;-separated.
            if (prop.empty()) continue;

            std::vector<std::string> key_value = android::base::Split(prop, "=");
            if (key_value.size() != 2) continue;

            const std::string& key = key_value[0];
            const std::string& value = key_value[1];
            if (key == "ro.product.name") {
                t->product = value;
            } else if (key == "ro.product.model") {
                t->model = value;
            } else if (key == "ro.product.device") {
                t->device = value;
            } else if (key == "features") {
                t->SetFeatures(value);
            }
        }
    }

    const std::string& type = pieces[0];
    if (type == "bootloader") {
        D("setting connection_state to kCsBootloader");
        t->SetConnectionState(kCsBootloader);
    } else if (type == "device") {
        D("setting connection_state to kCsDevice");
        t->SetConnectionState(kCsDevice);
    } else if (type == "recovery") {
        D("setting connection_state to kCsRecovery");
        t->SetConnectionState(kCsRecovery);
    } else if (type == "sideload") {
        D("setting connection_state to kCsSideload");
        t->SetConnectionState(kCsSideload);
    } else if (type == "rescue") {
        D("setting connection_state to kCsRescue");
        t->SetConnectionState(kCsRescue);
    } else {
        D("setting connection_state to kCsHost");
        t->SetConnectionState(kCsHost);
    }
}

static void handle_new_connection(atransport* t, apacket* p) {
    handle_offline(t);

    t->update_version(p->msg.arg0, p->msg.arg1);
    std::string banner(p->payload.begin(), p->payload.end());
    parse_banner(banner, t);

#if ADB_HOST
    handle_online(t);
#else
    ADB_LOG(Connection) << "received CNXN: version=" << p->msg.arg0 << ", maxdata = " << p->msg.arg1
                        << ", banner = '" << banner << "'";

    if (t->use_tls) {
        // We still handshake in TLS mode. If auth_required is disabled,
        // we'll just not verify the client's certificate. This should be the
        // first packet the client receives to indicate the new protocol.
        send_tls_request(t);
    } else if (!auth_required) {
        LOG(INFO) << "authentication not required";
        handle_online(t);
        send_connect(t);
    } else {
        send_auth_request(t);
    }
#endif
}

void handle_packet(apacket *p, atransport *t)
{
    D("handle_packet() %c%c%c%c", ((char*) (&(p->msg.command)))[0],
            ((char*) (&(p->msg.command)))[1],
            ((char*) (&(p->msg.command)))[2],
            ((char*) (&(p->msg.command)))[3]);
    print_packet("recv", p);
    CHECK_EQ(p->payload.size(), p->msg.data_length);

    switch(p->msg.command){
    case A_CNXN:  // CONNECT(version, maxdata, "system-id-string")
        handle_new_connection(t, p);
        break;
    case A_STLS:  // Explicitly unsupported by the embedded direct host.
        t->Kick();
        break;

    case A_AUTH:
        // All AUTH commands are ignored in TLS mode
        if (t->use_tls) {
            break;
        }
        switch (p->msg.arg0) {
#if ADB_HOST
            case ADB_AUTH_TOKEN:
                if (t->GetConnectionState() != kCsAuthorizing) {
                    t->SetConnectionState(kCsAuthorizing);
                }
                send_auth_response(p->payload.data(), p->msg.data_length, t);
                break;
#else
            case ADB_AUTH_SIGNATURE: {
                // TODO: Switch to string_view.
                std::string signature(p->payload.begin(), p->payload.end());
                std::string auth_key;
                if (adbd_auth_verify(t->token, sizeof(t->token), signature, &auth_key)) {
                    adbd_auth_verified(t);
                    t->failed_auth_attempts = 0;
                    t->auth_key = auth_key;
                    adbd_notify_framework_connected_key(t);
                } else {
                    if (t->failed_auth_attempts++ > 256) std::this_thread::sleep_for(1s);
                    send_auth_request(t);
                }
                break;
            }

            case ADB_AUTH_RSAPUBLICKEY:
                t->auth_key = std::string(p->payload.data());
                adbd_auth_confirm_key(t);
                break;
#endif
            default:
                t->SetConnectionState(kCsOffline);
                handle_offline(t);
                break;
        }
        break;

    case A_OPEN: {
        /* OPEN(local-id, [send-buffer], "destination") */
        if (!t->online || p->msg.arg0 == 0) {
            break;
        }

        // This library never accepts device-initiated local service opens.
        send_close(0, p->msg.arg0, t);
        break;
    }

    case A_OKAY: /* READY(local-id, remote-id, "") */
        if (t->online && p->msg.arg0 != 0 && p->msg.arg1 != 0) {
            asocket* s = find_local_socket(p->msg.arg1, 0);
            if (s && s->transport == t) {
                std::optional<int32_t> acked_bytes;
                if (p->payload.size() == sizeof(int32_t)) {
                    int32_t value;
                    memcpy(&value, p->payload.data(), sizeof(value));
                    // acked_bytes can be negative!
                    //
                    // In the future, we can use this to preemptively supply backpressure, instead
                    // of waiting for the writer to hit its limit.
                    acked_bytes = value;
                } else if (p->payload.size() != 0) {
                    LOG(ERROR) << "invalid A_OKAY payload size: " << p->payload.size();
                    break;
                }

                if (s->peer == nullptr) {
                    /* On first READY message, create the connection. */
                    s->peer = create_remote_socket(p->msg.arg0, t);
                    s->peer->peer = s;

                    local_socket_ack(s, acked_bytes);
                } else if (s->peer->id == p->msg.arg0) {
                    /* Other READY messages must use the same local-id */
                    local_socket_ack(s, acked_bytes);
                } else {
                    D("Invalid A_OKAY(%d,%d), expected A_OKAY(%d,%d) on transport %s", p->msg.arg0,
                      p->msg.arg1, s->peer->id, p->msg.arg1, t->serial.c_str());
                }
            } else {
                // When receiving A_OKAY from device for A_OPEN request, the host server may
                // have closed the local socket because of client disconnection. Then we need
                // to send A_CLSE back to device to close the service on device.
                send_close(p->msg.arg1, p->msg.arg0, t);
            }
        }
        break;

    case A_CLSE: /* CLOSE(local-id, remote-id, "") or CLOSE(0, remote-id, "") */
        if (t->online && p->msg.arg1 != 0) {
            asocket* s = find_local_socket(p->msg.arg1, p->msg.arg0);
            if (s && s->transport == t) {
                /* According to protocol.txt, p->msg.arg0 might be 0 to indicate
                 * a failed OPEN only. However, due to a bug in previous ADB
                 * versions, CLOSE(0, remote-id, "") was also used for normal
                 * CLOSE() operations.
                 *
                 * This is bad because it means a compromised adbd could
                 * send packets to close connections between the host and
                 * other devices. To avoid this, only allow this if the local
                 * socket has a peer on the same transport.
                 */
                if (p->msg.arg0 == 0 && s->peer && s->peer->transport != t) {
                    D("Invalid A_CLSE(0, %u) from transport %s, expected transport %s", p->msg.arg1,
                      t->serial.c_str(), s->peer->transport->serial.c_str());
                } else {
                    s->close(s);
                }
            }
        }
        break;

    case A_WRTE: /* WRITE(local-id, remote-id, <data>) */
        if (t->online && p->msg.arg0 != 0 && p->msg.arg1 != 0) {
            asocket* s = find_local_socket(p->msg.arg1, p->msg.arg0);
            if (s && s->transport == t) {
                s->enqueue(s, std::move(p->payload));
            }
        }
        break;

    default:
        printf("handle_packet: what is %08x?!\n", p->msg.command);
    }

    put_apacket(p);
}

