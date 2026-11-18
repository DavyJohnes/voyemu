package com.voyemu.passion.phone;

import java.util.Arrays;
import java.util.concurrent.CountDownLatch;

public final class PassionSyntheticPhoneTest {
    private static int checks;

    private static void need(boolean value) {
        checks++;

        if (!value) {
            throw new AssertionError("check " + checks);
        }
    }

    private static void refuse(Runnable value) {
        try {
            value.run();
            throw new AssertionError("accepted");
        } catch (IllegalArgumentException | IllegalStateException expected) {
            checks++;
        }
    }

    public static void main(String[] args) throws Exception {
        PassionSyntheticPhone p = new PassionSyntheticPhone();

        need(p.snapshot().contacts.size() == 2);
        refuse(() -> p.begin("+70000000001", false));
        refuse(() -> new PassionSyntheticPhone.Contact("\n", "+70000000001"));
        refuse(() -> new PassionSyntheticPhone.Contact("x", "112"));
        refuse(() ->
                p.configure(
                        "x",
                        Arrays.asList(p.snapshot().contacts.get(0), p.snapshot().contacts.get(0))));
        p.connect();
        refuse(() -> p.configure("x", p.snapshot().contacts));

        for (String invalid :
                new String[] {
                    "112",
                    "911",
                    "+79991234567",
                    "+70000000003",
                    "tel:+70000000001",
                    "+70000000001;123",
                    null
                }) {
            refuse(() -> p.begin(invalid, true));
        }

        long id = p.begin("+70000000001", false);
        need(p.snapshot().state == PassionSyntheticPhone.State.IDLE && p.snapshot().pending);
        refuse(() -> p.begin("+70000000002", false));
        need(!p.deliver(id + 1, "+70000000001", false));
        need(!p.deliver(id, "+70000000002", false));
        need(!p.deliver(id, "+70000000001", true));
        need(!p.awaitDelivery(id, 1));
        need(p.cancelPending(id, "delivery_failed"));
        need(!p.deliver(id, "+70000000001", false));
        need(
                p.snapshot().state == PassionSyntheticPhone.State.IDLE
                        && p.snapshot().number.isEmpty());
        id = p.begin("+70000000002", true);
        need(
                p.snapshot().pending
                        && p.snapshot().outgoing
                        && p.snapshot().state == PassionSyntheticPhone.State.IDLE);
        need(p.cancelPending(id, "hangup"));
        need(!p.deliver(id, "+70000000002", true));
        need(
                p.snapshot().state == PassionSyntheticPhone.State.IDLE
                        && p.snapshot().outgoingCalls.isEmpty());
        id = p.begin("+70000000001", false);
        need(p.deliver(id, "+70000000001", false));
        need(p.awaitDelivery(id, 1));
        need(!p.cancelPending(id, "late_timeout"));
        need(p.snapshot().state == PassionSyntheticPhone.State.INCOMING && !p.snapshot().outgoing);
        need(p.answer(id, 100));
        need(p.snapshot().activeStarted == 100);
        need(!p.answer(id, 101));
        need(p.end(id, "ended"));
        need(!p.end(id, "ended"));
        need(!p.deliver(id, "+70000000001", false));
        need(!p.answer(id, 102));
        long next = p.begin("+70000000002", true);
        need(next > id && !p.end(id, "stale"));
        need(p.deliver(next, "+70000000002", true));
        need(p.snapshot().outgoing && p.snapshot().number.equals("+70000000002"));
        need(p.snapshot().outgoingCalls.equals(Arrays.asList("+70000000002")));
        need(p.answer(next, 200));
        p.disconnect();
        need(
                p.snapshot().state == PassionSyntheticPhone.State.DISCONNECTED
                        && p.snapshot().number.isEmpty());
        need(!p.answer(next, 201) && !p.deliver(next, "+70000000002", true));
        p.connect();
        next = p.begin("+70000000001", false);
        p.disconnect();
        need(!p.deliver(next, "+70000000001", false));
        p.connect();
        final long pending = p.begin("+70000000001", false);
        CountDownLatch ready = new CountDownLatch(1);
        final boolean[] delivered = {false};
        Thread waiting =
                new Thread(() -> {
                    try {
                        ready.countDown();
                        delivered[0] = p.awaitDelivery(pending, 1000);
                    } catch (InterruptedException error) {
                        throw new AssertionError(error);
                    }
                });
        waiting.start();
        ready.await();
        need(p.deliver(pending, "+70000000001", false));
        waiting.join(1500);
        need(!waiting.isAlive() && delivered[0]);
        p.end(pending, "ended");

        for (int i = 0; i < 70; i++) {
            long call = p.begin("+70000000002", true);
            need(p.deliver(call, "+70000000002", true));
            p.end(call, "ended");
        }

        need(p.snapshot().events.size() == 64 && p.snapshot().outgoingCalls.size() == 64);
        System.out.println(
                "PASS "
                        + checks
                        + " synthetic phone checks: exact numbers, callback delivery, timeout"
                        + " cleanup, stale IDs, direction, termination and reconnect");
    }
}
