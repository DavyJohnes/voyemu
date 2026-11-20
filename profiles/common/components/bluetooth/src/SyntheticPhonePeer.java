package com.qinggan.bluetooth.internal.ivt8;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;

/** Finite in-memory fixture; never owns a physical radio or an external call. */
public final class SyntheticPhonePeer {
    public static final int ACTIVE = 128, DIALING = 129, INCOMING = 130, TERMINATED = 131;
    public static final String ADDRESS = "02:00:00:00:00:01";

    public static final class Contact {
        public final String name, number;

        public Contact(String name, String number) {
            validateLabel(name, 80, 128);
            validateNumber(number);
            this.name = name;
            this.number = number;
        }
    }

    private volatile List<Contact> contacts = defaults();
    private final List<String> outgoing = new ArrayList<>(), journal = new ArrayList<>();
    private volatile boolean connected, outgoingCall;
    private volatile int state = TERMINATED;
    private volatile String number = "", peerName = "Voyemu Phone";
    private final AtomicLong sequence = new AtomicLong();

    private static List<Contact> defaults() {
        List<Contact> result = new ArrayList<>();

        result.add(new Contact("Иван Иванов", "+70000000001"));
        result.add(new Contact("Анна Петрова", "+70000000002"));

        return Collections.unmodifiableList(result);
    }

    public static void validateNumber(String value) {
        if (value == null || !value.matches("\\+7[0-9]{10}")) {
            throw new IllegalArgumentException("PHONE_SYNTHETIC_NUMBER");
        }
    }

    private static void validateLabel(String value, int units, int bytes) {
        if (value == null || value.trim().isEmpty() || value.length() > units) {
            throw new IllegalArgumentException("PHONE_LABEL");
        }

        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);

            if (c < 32 || c == 127) {
                throw new IllegalArgumentException("PHONE_LABEL_CONTROL");
            }

            if (Character.isHighSurrogate(c)) {
                if (++i >= value.length() || !Character.isLowSurrogate(value.charAt(i))) {
                    throw new IllegalArgumentException("PHONE_LABEL_UTF16");
                }
            } else if (Character.isLowSurrogate(c)) {
                throw new IllegalArgumentException("PHONE_LABEL_UTF16");
            }
        }

        if (value.getBytes(java.nio.charset.StandardCharsets.UTF_8).length > bytes) {
            throw new IllegalArgumentException("PHONE_LABEL_BYTES");
        }
    }

    public void configure(String name, List<Contact> values) {
        if (connected || state != TERMINATED) {
            throw new IllegalStateException("PHONE_CONNECTED");
        }

        validateLabel(name, 64, 64);

        if (values == null
                || values.size() != 2
                || values.get(0) == null
                || values.get(1) == null
                || values.get(0).number.equals(values.get(1).number)) {
            throw new IllegalArgumentException("PHONE_TWO_DISTINCT_CONTACTS");
        }

        contacts = Collections.unmodifiableList(new ArrayList<>(values));
        peerName = name;
        record("configure");
    }

    public void connect() {
        if (connected) {
            throw new IllegalStateException("PHONE_ALREADY_CONNECTED");
        }

        connected = true;
        record("connect");
    }

    public void disconnect() {
        requireConnected();
        connected = false;
        state = TERMINATED;
        number = "";
        record("disconnect");
    }

    public void incoming(String value) {
        begin(value, INCOMING, false);
    }

    /**
     * OEM contact lookup may strip the leading plus; only our exact configured alias is accepted.
     */
    public String resolveOemDialNumber(String value) {
        for (Contact contact : contacts) {
            if (contact.number.substring(1).equals(value)) {
                return contact.number;
            }
        }

        return value;
    }

    public void dial(String value) {
        begin(value, DIALING, true);
        outgoing.add(value);

        if (outgoing.size() > 32) {
            outgoing.remove(0);
        }
    }

    private void begin(String value, int next, boolean out) {
        validateNumber(value);
        requireConnected();

        if (state != TERMINATED) {
            throw new IllegalStateException("PHONE_CALL_ACTIVE");
        }

        number = value;
        state = next;
        outgoingCall = out;
        record(out ? "dial" : "incoming");
    }

    public void answer() {
        requireConnected();

        if (state != INCOMING && state != DIALING) {
            throw new IllegalStateException("PHONE_NOT_RINGING_OR_DIALING");
        }

        state = ACTIVE;
        record("answer");
    }

    public void hangup() {
        requireConnected();

        if (state == TERMINATED) {
            throw new IllegalStateException("PHONE_NO_CALL");
        }

        state = TERMINATED;
        record("hangup");
    }

    public synchronized void record(String event) {
        journal.add(sequence.incrementAndGet() + ":" + event);

        if (journal.size() > 64) {
            journal.remove(0);
        }
    }

    private void requireConnected() {
        if (!connected) {
            throw new IllegalStateException("PHONE_DISCONNECTED");
        }
    }

    /** Immutable remote snapshots are views; only the Phone authority executes transitions. */
    void mirror(
            String name,
            List<Contact> values,
            boolean connected,
            int state,
            String number,
            boolean outgoing,
            long revision) {
        validateLabel(name, 64, 64);

        if (values == null
                || values.size() != 2
                || values.get(0).number.equals(values.get(1).number)
                || revision < 0
                || !(state == ACTIVE
                        || state == DIALING
                        || state == INCOMING
                        || state == TERMINATED)
                || (!connected && state != TERMINATED)) {
            throw new IllegalArgumentException("PHONE_PEER_STATE");
        }

        if (!number.isEmpty()) {
            validateNumber(number);
        }

        if (state != TERMINATED && number.isEmpty()) {
            throw new IllegalArgumentException("PHONE_PEER_CALL");
        }

        this.contacts = Collections.unmodifiableList(new ArrayList<>(values));
        this.peerName = name;
        this.connected = connected;
        this.state = state;
        this.number = number;
        this.outgoingCall = outgoing;
        this.sequence.set(revision);
    }

    void unavailable() {
        connected = false;
        state = TERMINATED;
        number = "";
        outgoingCall = false;
    }

    public boolean connected() {
        return connected;
    }

    public boolean outgoingCall() {
        return outgoingCall;
    }

    public int state() {
        return state;
    }

    public String number() {
        return number;
    }

    public String name() {
        return peerName;
    }

    public long sequence() {
        return sequence.get();
    }

    public List<Contact> contacts() {
        return contacts;
    }

    public List<String> outgoing() {
        return new ArrayList<>(outgoing);
    }

    public synchronized List<String> journal() {
        return new ArrayList<>(journal);
    }
}
