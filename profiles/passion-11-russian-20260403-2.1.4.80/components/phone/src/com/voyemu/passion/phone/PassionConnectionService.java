package com.voyemu.passion.phone;

import android.net.Uri;
import android.os.SystemClock;
import android.telecom.Connection;
import android.telecom.ConnectionRequest;
import android.telecom.ConnectionService;
import android.telecom.DisconnectCause;
import android.telecom.PhoneAccountHandle;
import android.telecom.TelecomManager;

/** Only callback delivery for an already reserved local synthetic call is accepted. */
public final class PassionConnectionService extends ConnectionService {
    @Override
    public Connection onCreateIncomingConnection(
            PhoneAccountHandle handle, ConnectionRequest request) {
        return create(handle, request, false);
    }

    @Override
    public Connection onCreateOutgoingConnection(
            PhoneAccountHandle handle, ConnectionRequest request) {
        return create(handle, request, true);
    }

    private Connection create(
            PhoneAccountHandle handle, ConnectionRequest request, boolean outgoing) {
        try {
            if (!PassionPhoneAccount.accepts(
                            PassionPhoneAdapter.account(this), handle, request.getAccountHandle())
                    || request.getExtras() == null) {
                throw new IllegalArgumentException("PHONE_ACCOUNT");
            }

            Uri address = request.getAddress();

            if (address == null || !"tel".equals(address.getScheme())) {
                throw new IllegalArgumentException("PHONE_ADDRESS");
            }

            String number = address.getSchemeSpecificPart();
            long id = request.getExtras().getLong(PassionPhoneAdapter.CALL_ID, -1);
            LocalConnection connection = new LocalConnection(id);
            connection.setAddress(address, TelecomManager.PRESENTATION_ALLOWED);
            PassionSyntheticPhone.Contact contact = PassionPhoneAdapter.peer().contact(number);

            if (contact == null) {
                throw new IllegalArgumentException("PHONE_NUMBER");
            }

            connection.setCallerDisplayName(contact.name, TelecomManager.PRESENTATION_ALLOWED);
            connection.setAudioModeIsVoip(true);

            if (!PassionPhoneAdapter.attach(connection, id, number, outgoing)) {
                throw new IllegalStateException("PHONE_STALE_CALL");
            }

            if (outgoing) {
                connection.setDialing();
            } else {
                connection.setRinging();
            }

            return connection;
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: Telecom receives any failed setup as a failed
            // connection
            return Connection.createFailedConnection(new DisconnectCause(DisconnectCause.ERROR));
        }
    }

    @Override
    public void onCreateIncomingConnectionFailed(
            PhoneAccountHandle handle, ConnectionRequest request) {
        failed(handle, request);
    }

    @Override
    public void onCreateOutgoingConnectionFailed(
            PhoneAccountHandle handle, ConnectionRequest request) {
        failed(handle, request);
    }

    private void failed(PhoneAccountHandle handle, ConnectionRequest request) {
        if (PassionPhoneAccount.accepts(
                        PassionPhoneAdapter.account(this), handle, request.getAccountHandle())
                && request.getExtras() != null) {
            PassionPhoneAdapter.peer()
                    .end(
                            request.getExtras().getLong(PassionPhoneAdapter.CALL_ID, -1),
                            "telecom_failed");
        }
    }

    @Override
    public void onDestroy() {
        PassionPhoneAdapter.serviceDestroyed();
        super.onDestroy();
    }

    static final class LocalConnection extends Connection {
        final long id;
        private boolean closed;

        LocalConnection(long id) {
            this.id = id;
        }

        @Override
        public void onAnswer() {
            answer();
        }

        @Override
        public void onAnswer(int videoState) {
            answer();
        }

        @Override
        public void onReject() {
            finish(DisconnectCause.REJECTED);
        }

        @Override
        public void onReject(String message) {
            finish(DisconnectCause.REJECTED);
        }

        @Override
        public void onDisconnect() {
            finish(DisconnectCause.LOCAL);
        }

        @Override
        public void onAbort() {
            finish(DisconnectCause.CANCELED);
        }

        boolean answer() {
            if (closed || !PassionPhoneAdapter.peer().answer(id, SystemClock.elapsedRealtime())) {
                return false;
            }

            setActive();

            return true;
        }

        void finish(int cause) {
            if (closed) {
                return;
            }

            closed = true;
            PassionPhoneAdapter.peer().end(id, "ended");
            setDisconnected(new DisconnectCause(cause));
            destroy();
            PassionPhoneAdapter.detach(this);
        }
    }
}
