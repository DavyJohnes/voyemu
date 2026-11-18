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

#define TRACE_TAG TRANSPORT

#include "sysdeps.h"

#include "transport.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <android-base/logging.h>
#include <android-base/no_destructor.h>
#include <android-base/parsenetaddress.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/thread_annotations.h>

#include "adb.h"
#include "adb_auth.h"
#include "adb_io.h"
#include "adb_trace.h"
#include "adb_utils.h"
#include "fdevent/fdevent.h"
#include "sysdeps/chrono.h"

#if ADB_HOST
#include "client/usb.h"
#endif

using namespace std::string_literals;
using android::base::ScopedLockAssertion;

static void remove_transport(atransport* transport);
static void transport_destroy(atransport* transport);

static auto& transport_lock = *new std::recursive_mutex();
// When a tranport is created, it is not started yet (and in the case of the host side, it has
// not yet sent CNXN). These transports are staged in the pending list.
static auto& pending_list = *new std::list<atransport*>();
// TODO: unordered_map<TransportId, atransport*>
static auto& transport_list = *new std::list<atransport*>();

const char* const kFeatureShell2 = "shell_v2";
const char* const kFeatureDelayedAck = "delayed_ack";
// TODO(joshuaduong): Bump to v2 when openscreen discovery is enabled by default

TransportId NextTransportId() {
    static std::atomic<TransportId> next(1);
    return next++;
}

void Connection::Reset() {
    LOG(INFO) << "Connection::Reset(): stopping";
    Stop();
}

std::string Connection::Serial() const {
    return transport_ ? transport_->serial_name() : "<unknown>";
}

BlockingConnectionAdapter::BlockingConnectionAdapter(std::unique_ptr<BlockingConnection> connection)
    : underlying_(std::move(connection)) {}

BlockingConnectionAdapter::~BlockingConnectionAdapter() {
    LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): destructing";
    Stop();
}

bool BlockingConnectionAdapter::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        LOG(FATAL) << "BlockingConnectionAdapter(" << Serial() << "): started multiple times";
    }

    StartReadThread();

    write_thread_ = std::thread([this]() {
        LOG(INFO) << Serial() << ": write thread spawning";
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            ScopedLockAssertion assume_locked(mutex_);
            cv_.wait(lock, [this]() REQUIRES(mutex_) {
                return this->stopped_ || !this->write_queue_.empty();
            });

            if (this->stopped_) {
                return;
            }

            std::unique_ptr<apacket> packet = std::move(this->write_queue_.front());
            this->write_queue_.pop_front();
            lock.unlock();

            if (!this->underlying_->Write(packet.get())) {
                break;
            }
        }
        std::call_once(this->error_flag_, [this]() { transport_->HandleError("write failed"); });
    });

    started_ = true;
    return true;
}

void BlockingConnectionAdapter::StartReadThread() {
    read_thread_ = std::thread([this]() {
        LOG(INFO) << Serial() << ": read thread spawning";
        while (true) {
            auto packet = std::make_unique<apacket>();
            if (!underlying_->Read(packet.get())) {
                PLOG(INFO) << Serial() << ": read failed";
                break;
            }

            bool got_stls_cmd = false;
            if (packet->msg.command == A_STLS) {
                got_stls_cmd = true;
            }

            if (!transport_->HandleRead(std::move(packet))) break;

            // If we received the STLS packet, we are about to perform the TLS
            // handshake. So this read thread must stop and resume after the
            // handshake completes otherwise this will interfere in the process.
            if (got_stls_cmd) {
                LOG(INFO) << Serial() << ": Received STLS packet. Stopping read thread.";
                return;
            }
        }
        std::call_once(this->error_flag_, [this]() { transport_->HandleError("read failed"); });
    });
}

bool BlockingConnectionAdapter::DoTlsHandshake(RSA* key, std::string* auth_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
    bool success = this->underlying_->DoTlsHandshake(key, auth_key);
    StartReadThread();
    return success;
}

void BlockingConnectionAdapter::Reset() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): not started";
            return;
        }

        if (stopped_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): already stopped";
            return;
        }
    }

    LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): resetting";
    this->underlying_->Reset();
    Stop();
}

void BlockingConnectionAdapter::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): not started";
            return;
        }

        if (stopped_) {
            LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): already stopped";
            return;
        }

        stopped_ = true;
    }

    LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): stopping";

    this->underlying_->Close();
    this->cv_.notify_one();

    // Move the threads out into locals with the lock taken, and then unlock to let them exit.
    std::thread read_thread;
    std::thread write_thread;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        read_thread = std::move(read_thread_);
        write_thread = std::move(write_thread_);
    }

    read_thread.join();
    write_thread.join();

    LOG(INFO) << "BlockingConnectionAdapter(" << Serial() << "): stopped";
    std::call_once(this->error_flag_, [this]() { transport_->HandleError("requested stop"); });
}

bool BlockingConnectionAdapter::Write(std::unique_ptr<apacket> packet) {
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        write_queue_.emplace_back(std::move(packet));
    }

    cv_.notify_one();
    return true;
}

void send_packet(apacket* p, atransport* t) {
    p->msg.magic = p->msg.command ^ 0xffffffff;
    // compute a checksum for connection/auth packets for compatibility reasons
    if (t->get_protocol_version() >= A_VERSION_SKIP_CHECKSUM) {
        p->msg.data_check = 0;
    } else {
        p->msg.data_check = calculate_apacket_checksum(p);
    }

    VLOG(TRANSPORT) << dump_packet(t->serial.c_str(), "to remote", p);

    if (t == nullptr) {
        LOG(FATAL) << "Transport is null";
    }

    if (t->Write(p) != 0) {
        D("%s: failed to enqueue packet, closing transport", t->serial.c_str());
        t->Kick();
    }
}

void kick_transport(atransport* t, bool reset) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    // As kick_transport() can be called from threads without guarantee that t is valid,
    // check if the transport is in transport_list first.
    //
    // TODO(jmgao): WTF? Is this actually true?
    if (std::find(transport_list.begin(), transport_list.end(), t) != transport_list.end()) {
        if (reset) {
            t->Reset();
        } else {
            t->Kick();
        }
    }

#if ADB_HOST

#endif
}

#if ADB_HOST
// Check if all of the USB transports are connected.
bool iterate_transports(std::function<bool(const atransport*)> fn) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (const auto& t : transport_list) {
        if (!fn(t)) {
            return false;
        }
    }
    for (const auto& t : pending_list) {
        if (!fn(t)) {
            return false;
        }
    }
    return true;
}

// Typed device notification replaces all server trackers/protobuf.
extern void adb_host_transport_changed();
void update_transports() { adb_host_transport_changed(); }
#endif

static void fdevent_unregister_transport(atransport* t) {
    VLOG(TRANSPORT) << "unregistering transport: " << t->serial;

    {
        std::lock_guard<std::recursive_mutex> lock(transport_lock);
        transport_list.remove(t);
        pending_list.remove(t);
    }

    delete t;

    update_transports();
}

static void fdevent_register_transport(atransport* t) {
    auto state = to_string(t->GetConnectionState());
    VLOG(TRANSPORT) << "registering: " << t->serial.c_str() << " state=" << state
                    << " type=" << t->type;

    /* don't create transport threads for inaccessible devices */
    if (t->GetConnectionState() != kCsNoPerm) {
        t->connection()->SetTransport(t);

#if ADB_HOST
        VLOG(TRANSPORT) << "transport:" << t->serial << "(" << state << ")";
        if (t->GetConnectionState() != kCsDetached) {
            VLOG(TRANSPORT) << "Starting transport:" << t->serial;
            if (t->connection()->Start()) {
                send_connect(t);
            } else {
                VLOG(TRANSPORT) << "transport:" << t->serial << " failed to start.";
                return;
            }
        }
#else
        VLOG(TRANSPORT) << "Starting transport:" << t->serial;
        t->connection()->Start();
#endif
    }

    {
        std::lock_guard<std::recursive_mutex> lock(transport_lock);
        auto it = std::find(pending_list.begin(), pending_list.end(), t);
        if (it != pending_list.end()) {
            pending_list.remove(t);
            transport_list.push_front(t);
        }
    }

    update_transports();
}

#if ADB_HOST
void init_reconnect_handler() {}
#endif

void kick_all_transports() {
#if ADB_HOST

#endif
    // To avoid only writing part of a packet to a transport after exit, kick all transports.
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto t : transport_list) {
        t->Kick();
    }
}

void kick_all_tcp_tls_transports() {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto t : transport_list) {
        if (t->IsTcpDevice() && t->use_tls) {
            t->Kick();
        }
    }
}

#if !ADB_HOST
void kick_all_transports_by_auth_key(std::string_view auth_key) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto t : transport_list) {
        if (auth_key == t->auth_key) {
            t->Kick();
        }
    }
}
#endif

void register_transport(atransport* transport) {
    fdevent_run_on_looper([=]() { fdevent_register_transport(transport); });
}

static void remove_transport(atransport* transport) {
    fdevent_run_on_looper([=]() { fdevent_unregister_transport(transport); });
}

static void transport_destroy(atransport* t) {
    fdevent_check_looper();
    CHECK(t != nullptr);

    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    VLOG(TRANSPORT) << "destroying transport " << t->serial_name();
    t->connection()->Stop();
    D("transport: %s destroy (kicking and closing)", t->serial.c_str());
    remove_transport(t);
}

#if ADB_HOST
static int qual_match(const std::string& to_test, const char* prefix, const std::string& qual,
                      bool sanitize_qual) {
    if (to_test.empty()) /* Return true if both the qual and to_test are empty strings. */
        return qual.empty();

    if (qual.empty()) return 0;

    const char* ptr = to_test.c_str();
    if (prefix) {
        while (*prefix) {
            if (*prefix++ != *ptr++) return 0;
        }
    }

    for (char ch : qual) {
        if (sanitize_qual && !isalnum(ch)) ch = '_';
        if (ch != *ptr++) return 0;
    }

    /* Everything matched so far.  Return true if *ptr is a NUL. */
    return !*ptr;
}

// Contains either a device serial string or a USB device address like "usb:2-6"
const char* __transport_server_one_device = nullptr;

void transport_set_one_device(const char* adb_one_device) {
    __transport_server_one_device = adb_one_device;
}

const char* transport_get_one_device() {
    return __transport_server_one_device;
}

bool transport_server_owns_device(std::string_view serial) {
    if (!__transport_server_one_device) {
        // If the server doesn't own one device, server owns all devices.
        return true;
    }
    return serial.compare(__transport_server_one_device) == 0;
}

bool transport_server_owns_device(std::string_view dev_path, std::string_view serial) {
    if (!__transport_server_one_device) {
        // If the server doesn't own one device, server owns all devices.
        return true;
    }
    return serial.compare(__transport_server_one_device) == 0 ||
           dev_path.compare(__transport_server_one_device) == 0;
}

atransport* acquire_one_transport(TransportType type, const char* serial, TransportId transport_id,
                                  bool* is_ambiguous, std::string* error_out,
                                  bool accept_any_state) {
    atransport* result = nullptr;

    if (transport_id != 0) {
        *error_out = android::base::StringPrintf("no device with transport id '%" PRIu64 "'",
                                                 transport_id);
    } else if (serial) {
        *error_out = android::base::StringPrintf("device '%s' not found", serial);
    } else if (type == kTransportLocal) {
        *error_out = "no emulators found";
    } else if (type == kTransportAny) {
        *error_out = "no devices/emulators found";
    } else {
        *error_out = "no devices found";
    }

    std::unique_lock<std::recursive_mutex> lock(transport_lock);
    for (const auto& t : transport_list) {
        if (t->GetConnectionState() == kCsNoPerm) {
            *error_out = "USB permission denied";
            continue;
        }

        if (transport_id) {
            if (t->id == transport_id) {
                result = t;
                break;
            }
        } else if (serial) {
            if (t->MatchesTarget(serial)) {
                if (result) {
                    *error_out = "more than one device with serial "s + serial;
                    if (is_ambiguous) *is_ambiguous = true;
                    result = nullptr;
                    break;
                }
                result = t;
            }
        } else {
            if (type == kTransportUsb && t->type == kTransportUsb) {
                if (result) {
                    *error_out = "more than one USB device";
                    if (is_ambiguous) *is_ambiguous = true;
                    result = nullptr;
                    break;
                }
                result = t;
            } else if (type == kTransportLocal && t->type == kTransportLocal) {
                if (result) {
                    *error_out = "more than one emulator";
                    if (is_ambiguous) *is_ambiguous = true;
                    result = nullptr;
                    break;
                }
                result = t;
            } else if (type == kTransportAny) {
                if (result) {
                    *error_out = "more than one device/emulator";
                    if (is_ambiguous) *is_ambiguous = true;
                    result = nullptr;
                    break;
                }
                result = t;
            }
        }
    }
    lock.unlock();

    if (result && !accept_any_state) {
        // The caller requires an active transport.
        // Make sure that we're actually connected.
        ConnectionState state = result->GetConnectionState();
        switch (state) {
            case kCsConnecting:
                *error_out = "device still connecting";
                result = nullptr;
                break;

            case kCsAuthorizing:
                *error_out = "device still authorizing";
                result = nullptr;
                break;

            case kCsUnauthorized: {
                *error_out = "device unauthorized.\n";
                char* ADB_VENDOR_KEYS = getenv("ADB_VENDOR_KEYS");
                *error_out += "This adb server's $ADB_VENDOR_KEYS is ";
                *error_out += ADB_VENDOR_KEYS ? ADB_VENDOR_KEYS : "not set";
                *error_out += "\n";
                *error_out += "Try 'adb kill-server' if that seems wrong.\n";
                *error_out += "Otherwise check for a confirmation dialog on your device.";
                result = nullptr;
                break;
            }

            case kCsOffline:
                *error_out = "device offline";
                result = nullptr;
                break;

            default:
                break;
        }
    }

    if (result) {
        *error_out = "success";
    }

    return result;
}

bool ConnectionWaitable::WaitForConnection(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    ScopedLockAssertion assume_locked(mutex_);
    return cv_.wait_for(lock, timeout, [&]() REQUIRES(mutex_) {
        return connection_established_ready_;
    }) && connection_established_;
}

void ConnectionWaitable::SetConnectionEstablished(bool success) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_established_ready_) return;
        connection_established_ready_ = true;
        connection_established_ = success;
        D("connection established with %d", success);
    }
    cv_.notify_one();
}
#endif

atransport::~atransport() {
#if ADB_HOST
    // If the connection callback had not been run before, run it now.
    SetConnectionEstablished(false);
#endif
}

int atransport::Write(apacket* p) {
    return this->connection()->Write(std::unique_ptr<apacket>(p)) ? 0 : -1;
}

void atransport::Reset() {
    if (!kicked_.exchange(true)) {
        LOG(INFO) << "resetting transport " << this << " " << this->serial;
        this->connection()->Reset();
    }
}

void atransport::Kick() {
    if (!kicked_.exchange(true)) {
        LOG(INFO) << "kicking transport " << this << " " << this->serial;
        this->connection()->Stop();
    }
}

ConnectionState atransport::GetConnectionState() const {
    return connection_state_;
}

void atransport::SetConnectionState(ConnectionState state) {
    fdevent_check_looper();
    connection_state_ = state;
    update_transports();
}

#if ADB_HOST
bool atransport::Attach(std::string* error) {
    D("%s: attach", serial.c_str());
    fdevent_check_looper();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_->SupportsDetach()) {
            *error = "attach/detach not supported";
            return false;
        }
    }

    if (GetConnectionState() != ConnectionState::kCsDetached) {
        *error = android::base::StringPrintf("transport %s is not detached", serial.c_str());
        return false;
    }

    ResetKeys();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_->Attach(error)) {
            return false;
        }
    }

    send_connect(this);
    return true;
}

bool atransport::Detach(std::string* error) {
    D("%s: detach", serial.c_str());
    fdevent_check_looper();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_->SupportsDetach()) {
            *error = "attach/detach not supported!";
            return false;
        }
    }

    if (GetConnectionState() == ConnectionState::kCsDetached) {
        *error = android::base::StringPrintf("transport %s is already detached", serial.c_str());
        return false;
    }

    handle_offline(this);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connection_->Detach(error)) {
            return false;
        }
    }

    this->SetConnectionState(kCsDetached);
    return true;
}
#endif  // ADB_HOST

void atransport::SetConnection(std::shared_ptr<Connection> connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    connection_ = std::shared_ptr<Connection>(std::move(connection));
}

namespace {
struct ReadBudget {
    std::mutex mutex;
    size_t bytes=0,packets=0;
};
static std::mutex read_budgets_mutex;
static std::map<TransportId,std::weak_ptr<ReadBudget>> read_budgets;
static std::shared_ptr<ReadBudget> read_budget(TransportId id) {
    std::lock_guard lock(read_budgets_mutex);
    for(auto it=read_budgets.begin();it!=read_budgets.end();) {
        if(it->second.expired())it=read_budgets.erase(it);else ++it;
    }
    auto& weak=read_budgets[id];auto budget=weak.lock();
    if(!budget) {budget=std::make_shared<ReadBudget>();weak=budget;}
    return budget;
}
struct QueuedPacket {
    std::unique_ptr<apacket> packet;
    std::shared_ptr<ReadBudget> budget;
    size_t bytes=0;
    ~QueuedPacket() {
        // Independent of atransport lifetime, including discarded callbacks.
        if(budget) {std::lock_guard lock(budget->mutex);budget->bytes-=bytes;--budget->packets;}
    }
};
}  // namespace

bool atransport::HandleRead(std::unique_ptr<apacket> p) {
    if (!check_header(p.get(), this)) {
        D("%s: remote read: bad header", serial.c_str());
        return false;
    }

    VLOG(TRANSPORT) << dump_packet(serial.c_str(), "from remote", p.get());
    auto queued=std::make_shared<QueuedPacket>();queued->packet=std::move(p);
    if(type==kTransportLocal) {
        auto budget=read_budget(id);const auto size=queued->packet->payload.size();
        std::lock_guard lock(budget->mutex);
        if(budget->packets>=64||size>4194304-budget->bytes)return false;
        ++budget->packets;budget->bytes+=size;queued->bytes=size;queued->budget=std::move(budget);
    }
    // Admission never waits for the looper, so Stop can join the reader. The
    // holder frees both packet and charge if shutdown discards this callback.
    fdevent_run_on_looper([queued, this]() { handle_packet(queued->packet.release(), this); });

    return true;
}

void atransport::HandleError(const std::string& error) {
    LOG(INFO) << serial_name() << ": connection terminated: " << error;
    fdevent_run_on_looper([this]() {
        handle_offline(this);
        transport_destroy(this);
    });
}

void atransport::update_version(int version, size_t payload) {
    protocol_version = std::min(version, A_VERSION);
    max_payload = std::min(payload, MAX_PAYLOAD);
}

int atransport::get_protocol_version() const {
    return protocol_version;
}

int atransport::get_tls_version() const {
    return tls_version;
}

size_t atransport::get_max_payload() const {
    return max_payload;
}

#if ADB_HOST
bool burst_mode_enabled() {
    static const char* env = getenv("ADB_BURST_MODE");
    static bool result = env && strcmp(env, "1") == 0;
    return result;
}
#endif

const FeatureSet& supported_features() {
    static const FeatureSet features { kFeatureShell2 };
    return features;
}

std::string FeatureSetToString(const FeatureSet& features) {
    return android::base::Join(features, ',');
}

FeatureSet StringToFeatureSet(const std::string& features_string) {
    if (features_string.empty()) {
        return FeatureSet();
    }

    return android::base::Split(features_string, ",");
}

template <class Range, class Value>
static bool contains(const Range& r, const Value& v) {
    return std::find(std::begin(r), std::end(r), v) != std::end(r);
}

bool CanUseFeature(const FeatureSet& feature_set, const std::string& feature) {
    return contains(feature_set, feature) && contains(supported_features(), feature);
}

bool atransport::has_feature(const std::string& feature) const {
    return contains(features_, feature);
}

void atransport::SetFeatures(const std::string& features_string) {
    features_ = StringToFeatureSet(features_string);
    delayed_ack_ = CanUseFeature(features_, kFeatureDelayedAck);
}

void atransport::AddDisconnect(adisconnect* disconnect) {
    disconnects_.push_back(disconnect);
}

void atransport::RemoveDisconnect(adisconnect* disconnect) {
    disconnects_.remove(disconnect);
}

void atransport::RunDisconnects() {
    for (const auto& disconnect : disconnects_) {
        disconnect->func(disconnect->opaque, this);
    }
    disconnects_.clear();
}

#if ADB_HOST
bool atransport::MatchesTarget(const std::string& target) const {
    if (!serial.empty()) {
        if (target == serial) {
            return true;
        } else if (type == kTransportLocal) {
            // Local transports can match [tcp:|udp:]<hostname>[:port].
            const char* local_target_ptr = target.c_str();

            // For fastboot compatibility, ignore protocol prefixes.
            if (android::base::StartsWith(target, "tcp:") ||
                android::base::StartsWith(target, "udp:")) {
                local_target_ptr += 4;
            }

            // Parse our |serial| and the given |target| to check if the hostnames and ports match.
            std::string serial_host, error;
            int serial_port = -1;
            if (android::base::ParseNetAddress(serial, &serial_host, &serial_port, nullptr,
                                               &error)) {
                // |target| may omit the port to default to ours.
                std::string target_host;
                int target_port = serial_port;
                if (android::base::ParseNetAddress(local_target_ptr, &target_host, &target_port,
                                                   nullptr, &error) &&
                    serial_host == target_host && serial_port == target_port) {
                    return true;
                }
            }
        }
    }

    return (target == devpath) || qual_match(target, "product:", product, false) ||
           qual_match(target, "model:", model, true) ||
           qual_match(target, "device:", device, false);
}

void atransport::SetConnectionEstablished(bool success) {
    connection_waitable_->SetConnectionEstablished(success);
}

ReconnectResult atransport::Reconnect() {
    return reconnect_(this);
}

// We use newline as our delimiter, make sure to never output it.
void close_usb_devices(std::function<bool(const atransport*)> predicate, bool reset) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto& t : transport_list) {
        if (predicate(t)) {
            if (reset) {
                t->Reset();
            } else {
                t->Kick();
            }
        }
    }
}

/* hack for osx */
void close_usb_devices(bool reset) {
    close_usb_devices([](const atransport*) { return true; }, reset);
}
#endif

#if ADB_HOST
atransport* find_transport(const char* serial) {
    atransport* result = nullptr;

    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto& t : transport_list) {
        if (strcmp(serial, t->serial.c_str()) == 0) {
            result = t;
            break;
        }
    }

    return result;
}

void kick_all_tcp_devices() {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    for (auto& t : transport_list) {
        if (t->IsTcpDevice()) {
            // Kicking breaks the read_transport thread of this transport out of any read, then
            // the read_transport thread will notify the main thread to make this transport
            // offline. Then the main thread will notify the write_transport thread to exit.
            // Finally, this transport will be closed and freed in the main thread.
            t->Kick();
        }
    }

}

#if ADB_HOST
void adb_host_register_emulator(std::shared_ptr<Connection> connection, const std::string& serial) {
    fdevent_check_looper();
    auto* t = new atransport(kTransportLocal, kCsOffline);
    t->serial = serial;
    t->SetConnection(std::move(connection));
    {
        std::lock_guard<std::recursive_mutex> lock(transport_lock);
        pending_list.push_front(t);
    }
    register_transport(t);
}

void register_libusb_transport(std::shared_ptr<Connection> connection, const char* serial,
                               const char* devpath, unsigned writeable) {
    atransport* t = new atransport(kTransportUsb, writeable ? kCsOffline : kCsNoPerm);
    if (serial) {
        t->serial = serial;
    }
    if (devpath) {
        t->devpath = devpath;
    }

    t->SetConnection(std::move(connection));

    {
        std::lock_guard<std::recursive_mutex> lock(transport_lock);
        pending_list.push_front(t);
    }

    register_transport(t);
}

// This should only be used for transports with connection_state == kCsNoPerm.
void unregister_usb_transport(usb_handle* usb) {
    std::lock_guard<std::recursive_mutex> lock(transport_lock);
    transport_list.remove_if([usb](atransport* t) {
        return t->GetUsbHandle() == usb && t->GetConnectionState() == kCsNoPerm;
    });
}
#endif

// Track reverse:forward commands, so that info can be used to develop
// an 'allow-list':
//   - adb reverse tcp:<device_port> localhost:<host_port> : responds with the
//   device_port
//   - adb reverse --remove tcp:<device_port> : responds OKAY
//   - adb reverse --remove-all : responds OKAY
void atransport::UpdateReverseConfig(std::string_view service_addr) {
    fdevent_check_looper();
    if (!android::base::ConsumePrefix(&service_addr, "reverse:")) {
        return;
    }

    if (android::base::ConsumePrefix(&service_addr, "forward:")) {
        // forward:[norebind:]<remote>;<local>
        bool norebind = android::base::ConsumePrefix(&service_addr, "norebind:");
        auto it = service_addr.find(';');
        if (it == std::string::npos) {
            return;
        }
        std::string remote(service_addr.substr(0, it));

        if (norebind && reverse_forwards_.contains(remote)) {
            // This will fail, don't update the map.
            LOG(DEBUG) << "ignoring reverse forward that will fail due to norebind";
            return;
        }

        std::string local(service_addr.substr(it + 1));
        reverse_forwards_[remote] = local;
    } else if (android::base::ConsumePrefix(&service_addr, "killforward:")) {
        // kill-forward:<remote>
        auto it = service_addr.find(';');
        if (it != std::string::npos) {
            return;
        }
        reverse_forwards_.erase(std::string(service_addr));
    } else if (service_addr == "killforward-all") {
        reverse_forwards_.clear();
    } else if (service_addr == "list-forward") {
        LOG(DEBUG) << __func__ << " ignoring --list";
    } else {  // Anything else we need to know about?
        LOG(FATAL) << "unhandled reverse service: " << service_addr;
    }
}

// Is this an authorized :connect request?
bool atransport::IsReverseConfigured(const std::string& local_addr) {
    fdevent_check_looper();
    for (const auto& [remote, local] : reverse_forwards_) {
        if (local == local_addr) {
            return true;
        }
    }
    return false;
}

#endif

bool check_header(apacket* p, atransport* t) {
    if (p->msg.magic != (p->msg.command ^ 0xffffffff)) {
        VLOG(RWX) << "check_header(): invalid magic command = " << std::hex << p->msg.command
                  << ", magic = " << p->msg.magic;
        return false;
    }

    if (p->msg.data_length > t->get_max_payload()) {
        VLOG(RWX) << "check_header(): " << p->msg.data_length
                  << " atransport::max_payload = " << t->get_max_payload();
        return false;
    }

    return true;
}

#if ADB_HOST
std::shared_ptr<RSA> atransport::Key() {
    if (keys_.empty()) {
        return nullptr;
    }

    std::shared_ptr<RSA> result = keys_[0];
    return result;
}

std::shared_ptr<RSA> atransport::NextKey() {
    if (keys_.empty()) {
        LOG(INFO) << "fetching keys for transport " << this->serial_name();
        keys_ = adb_auth_get_private_keys();

        // We should have gotten at least one key: the one that's automatically generated.
        // Explicit policy: never generate a key from library init.
        if (keys_.empty()) return nullptr;
    } else {
        keys_.pop_front();
    }

    return Key();
}

void atransport::ResetKeys() {
    keys_.clear();
}
#endif
