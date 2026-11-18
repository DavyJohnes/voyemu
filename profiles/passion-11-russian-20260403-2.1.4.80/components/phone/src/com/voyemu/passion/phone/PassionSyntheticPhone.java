package com.voyemu.passion.phone;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/** A finite local peer. Only the two configured contacts may become calls. */
public final class PassionSyntheticPhone {
    public enum State {
        DISCONNECTED,
        IDLE,
        INCOMING,
        DIALING,
        ACTIVE
    }

    public static final class Contact {
        public final String name, number;

        public Contact(String name, String number) {
            requireLabel(name, 80);

            if (number == null || !number.matches("\\+7[0-9]{10}")) {
                throw new IllegalArgumentException("PHONE_NUMBER");
            }

            this.name = name;
            this.number = number;
        }
    }

    public static final class Snapshot {
        public final long revision, callId, activeStarted;
        public final State state;
        public final String number, peerName;
        public final boolean outgoing, pending;
        public final List<Contact> contacts;
        public final List<String> outgoingCalls, events;

        Snapshot(PassionSyntheticPhone p) {
            revision = p.revision;
            callId = p.callId;
            activeStarted = p.activeStarted;
            state = p.state;
            number = p.number;
            peerName = p.peerName;
            outgoing = p.outgoing;
            pending = p.pending;
            contacts = Collections.unmodifiableList(new ArrayList<Contact>(p.contacts));
            outgoingCalls = Collections.unmodifiableList(new ArrayList<String>(p.outgoingCalls));
            events = Collections.unmodifiableList(new ArrayList<String>(p.events));
        }
    }

    private State state = State.DISCONNECTED;
    private long revision, callId, activeStarted;
    private String number = "", peerName = "Voyemu Phone";
    private boolean outgoing, pending;
    private List<Contact> contacts =
            Arrays.asList(
                    new Contact("Иван Иванов", "+70000000001"),
                    new Contact("Анна Петрова", "+70000000002"));
    private final List<String> outgoingCalls = new ArrayList<String>(),
            events = new ArrayList<String>();

    public synchronized Snapshot snapshot() {
        return new Snapshot(this);
    }

    public synchronized void configure(String name, List<Contact> values) {
        require(state == State.DISCONNECTED && !pending, "PHONE_CONNECTED");
        requireLabel(name, 64);

        if (values == null
                || values.size() != 2
                || values.get(0) == null
                || values.get(1) == null
                || values.get(0).number.equals(values.get(1).number)) {
            throw new IllegalArgumentException("PHONE_CONTACTS");
        }

        contacts = new ArrayList<Contact>(values);
        peerName = name;
        record("configure");
    }

    public synchronized void connect() {
        require(state == State.DISCONNECTED && !pending, "PHONE_CONNECTED");
        state = State.IDLE;
        record("connect");
    }

    public synchronized void disconnect() {
        state = State.DISCONNECTED;
        pending = false;
        clearCall();
        record("disconnect");
        notifyAll();
    }

    public synchronized long begin(String value, boolean direction) {
        require(state == State.IDLE && !pending, "PHONE_CALL_ACTIVE");
        require(contact(value) != null, "PHONE_NONLOCAL_NUMBER");
        require(callId < Long.MAX_VALUE, "PHONE_CALL_ID");
        callId++;
        number = value;
        outgoing = direction;
        pending = true;
        record(direction ? "outgoing_requested" : "incoming_requested");

        return callId;
    }

    public synchronized boolean deliver(long id, String value, boolean direction) {
        if (!pending
                || id != callId
                || state != State.IDLE
                || outgoing != direction
                || !number.equals(value)) {
            return false;
        }

        pending = false;
        state = direction ? State.DIALING : State.INCOMING;

        if (direction) {
            outgoingCalls.add(value);

            if (outgoingCalls.size() > 64) {
                outgoingCalls.remove(0);
            }
        }

        record(direction ? "dialing" : "incoming");
        notifyAll();

        return true;
    }

    public synchronized boolean answer(long id, long elapsed) {
        if (id != callId || (state != State.INCOMING && state != State.DIALING)) {
            return false;
        }

        state = State.ACTIVE;
        activeStarted = elapsed;
        record("active");
        notifyAll();

        return true;
    }

    public synchronized boolean end(long id, String event) {
        if (id != callId
                || (!pending
                        && state != State.INCOMING
                        && state != State.DIALING
                        && state != State.ACTIVE)) {
            return false;
        }

        pending = false;
        state = State.IDLE;
        clearCall();
        record(event);
        notifyAll();

        return true;
    }

    public synchronized boolean cancelPending(long id, String event) {
        return pending && id == callId && end(id, event);
    }

    public synchronized boolean awaitDelivery(long id, long milliseconds)
            throws InterruptedException {
        long until = System.nanoTime() + milliseconds * 1000000L;

        while (id == callId && pending) {
            long remaining = until - System.nanoTime();

            if (remaining <= 0) {
                break;
            }

            wait(Math.max(1, remaining / 1000000L));
        }

        return id == callId
                && !pending
                && (state == State.INCOMING || state == State.DIALING || state == State.ACTIVE);
    }

    public synchronized Contact contact(String value) {
        for (Contact c : contacts) {
            if (c.number.equals(value)) {
                return c;
            }
        }

        return null;
    }

    private void clearCall() {
        number = "";
        outgoing = false;
        activeStarted = 0;
    }

    private void record(String event) {
        revision++;
        events.add(revision + ":" + event);

        if (events.size() > 64) {
            events.remove(0);
        }
    }

    public static void requireLabel(String value, int max) {
        if (value == null || value.trim().isEmpty() || value.length() > max) {
            throw new IllegalArgumentException("PHONE_LABEL");
        }

        for (int i = 0; i < value.length(); i++) {
            if (value.charAt(i) < 32 || value.charAt(i) == 127) {
                throw new IllegalArgumentException("PHONE_LABEL");
            }
        }
    }

    private static void require(boolean value, String reason) {
        if (!value) {
            throw new IllegalStateException(reason);
        }
    }
}
