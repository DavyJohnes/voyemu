package com.voyemu.prepare;

import com.qinggan.bluetooth.internal.ivt8.SyntheticPhonePeer;

import java.util.Arrays;

public final class SyntheticPhonePeerTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    private static void rejects(Runnable action) {
        try {
            action.run();
            throw new AssertionError("accepted invalid transition");
        } catch (IllegalArgumentException | IllegalStateException expected) {
            // The call must reject this input.
        }
    }

    public static void main(String[] args) {
        SyntheticPhonePeer p = new SyntheticPhonePeer();

        check(p.contacts().size() == 2 && p.contacts().get(0).name.equals("Иван Иванов"));
        rejects(() -> p.dial("+70000000001"));
        check(p.sequence() == 0 && p.outgoing().isEmpty());
        rejects(() -> p.configure("Test", Arrays.asList(p.contacts().get(0))));
        check(p.sequence() == 0);
        rejects(() -> new SyntheticPhonePeer.Contact("Иван", "70000000001"));
        rejects(() -> new SyntheticPhonePeer.Contact("Иван", "+7999"));
        rejects(() -> p.configure("Test", Arrays.asList(p.contacts().get(0), p.contacts().get(0))));
        rejects(() -> new SyntheticPhonePeer.Contact("Имя\tФамилия", "+70000000001"));
        rejects(() -> new SyntheticPhonePeer.Contact("\uD800", "+70000000001"));
        rejects(() ->
                new SyntheticPhonePeer.Contact(
                        new String(new char[65]).replace('\0', 'Ж'), "+70000000001"));
        check(p.resolveOemDialNumber("70000000001").equals("+70000000001"));
        check(p.resolveOemDialNumber("+70000000001").equals("+70000000001"));
        check(p.resolveOemDialNumber("70000000003").equals("70000000003"));
        check(p.resolveOemDialNumber("+70000000003").equals("+70000000003"));
        check(p.resolveOemDialNumber(null) == null);
        p.connect();
        long sequence = p.sequence();
        rejects(() -> p.dial("70000000001"));
        rejects(() -> p.incoming("70000000001"));
        check(p.sequence() == sequence && p.outgoing().isEmpty());
        rejects(p::connect);
        rejects(p::answer);
        rejects(p::hangup);
        rejects(() -> p.configure("Test", p.contacts()));
        check(p.sequence() == sequence);
        p.incoming("+70000000002");
        check(p.state() == 130 && !p.outgoingCall());
        sequence = p.sequence();
        rejects(() -> p.dial("+70000000001"));
        check(p.sequence() == sequence && p.outgoing().isEmpty());
        p.answer();
        check(p.state() == 128);
        p.hangup();
        check(p.state() == 131);
        p.dial("+70000000001");
        check(p.number().equals("+70000000001") && p.outgoing().get(0).equals("+70000000001"));
        p.answer();
        check(p.state() == 128 && p.outgoingCall());
        p.disconnect();
        check(!p.connected() && p.state() == 131 && p.number().isEmpty());
        p.connect();
        check(p.contacts().get(0).name.equals("Иван Иванов") && p.outgoing().size() == 1);

        for (int i = 0; i < 100; i++) {
            p.dial("+70000000001");
            p.hangup();
        }

        check(p.outgoing().size() == 32 && p.journal().size() == 64);
        p.disconnect();
        p.configure(
                "Телефон",
                Arrays.asList(
                        new SyntheticPhonePeer.Contact("Имя Фамилия", "+70000000003"),
                        new SyntheticPhonePeer.Contact("Анна Петрова", "+70000000004")));
        check(p.name().equals("Телефон") && p.contacts().get(0).name.equals("Имя Фамилия"));
        long afterConfigure = p.sequence();
        check(p.resolveOemDialNumber("70000000003").equals("+70000000003"));
        check(p.resolveOemDialNumber("70000000004").equals("+70000000004"));
        check(p.resolveOemDialNumber("70000000001").equals("70000000001"));
        check(p.sequence() == afterConfigure && p.contacts().get(0).number.equals("+70000000003"));

        p.outgoing().clear();
        check(p.outgoing().size() == 32);
        System.out.println("SyntheticPhonePeerTest PASS");
    }
}
