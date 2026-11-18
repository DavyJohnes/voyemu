package com.voyemu.input;

import com.voyemu.oem.KeyManagerStub;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class KeyManagerHostProbe {
    private static int checks;

    private static void check(boolean condition) {
        checks++;

        if (!condition) {
            throw new AssertionError("check " + checks);
        }
    }

    private static void rejects(Runnable run) {
        boolean failed = false;

        try {
            run.run();
        } catch (IllegalArgumentException expected) {
            failed = true;
        }

        check(failed);
    }

    private static WheelInputSession.Command command(
            WheelInputSession s, long n, String phase, String owner) {
        return s.prepare(n, "photo", phase, owner);
    }

    private static void phases() {
        String session = "test_session_0001";
        String token = "0123456789abcdef0123456789abcdef";
        WheelInputSession s = new WheelInputSession(session, token, 0);

        check(WheelInputSession.schemaValue(Integer.valueOf(1)) == 1);
        check(WheelInputSession.schemaValue(Long.valueOf(1)) == 1);
        check(WheelInputSession.schemaValue(Double.valueOf(1)) == -1);
        check(WheelInputSession.schemaValue("1") == -1);
        check(WheelInputSession.schemaValue(null) == -1);
        check(WheelInputSession.schemaValue(Long.valueOf(0x100000001L)) == -1);
        check(!WheelInputSession.bootstrapMatches(1, session, token, null, null));
        check(WheelInputSession.bootstrapMatches(1, session, token, session, token));
        check(!WheelInputSession.bootstrapMatches(1, "new_session_0002", token, session, token));
        rejects(() -> WheelInputSession.bootstrapMatches(2, session, token, session, token));
        rejects(() -> WheelInputSession.bootstrapMatches(1, "bad session", token, null, null));
        rejects(() -> WheelInputSession.bootstrapMatches(1, session, "bad-token", null, null));
        rejects(() ->
                WheelInputSession.bootstrapMatches(
                        1, session, token, session, "fedcba9876543210fedcba9876543210"));
        check(WheelInputSession.controlCallerAllowed(0, true));
        check(WheelInputSession.controlCallerAllowed(2000, true));
        check(!WheelInputSession.controlCallerAllowed(0, false));
        check(!WheelInputSession.controlCallerAllowed(2000, false));
        check(!WheelInputSession.controlCallerAllowed(1000, true));
        check(!WheelInputSession.controlCallerAllowed(10000, true));
        check(!WheelInputSession.controlCallerAllowed(-1, true));
        check(s.authenticates(1, session, token));
        check(!s.authenticates(2, session, token));
        check(!s.authenticates(1, "other_session", token));
        check(!s.authenticates(1, session, "0123456789abcdef0123456789abcdee"));
        check(!s.authenticates(1, session, null));
        rejects(() -> command(s, 1, "up", "pointer:1"));
        rejects(() -> s.prepare(1, "acc", "down", "pointer:1"));
        rejects(() -> command(s, 1, "down", "bad owner"));
        WheelInputSession.Command down = command(s, 1, "down", "pointer:1");
        check(Arrays.equals(down.actions, new int[] {4096}));
        check(s.sequence() == 0 && s.held().isEmpty());
        s.commit(down);
        check(s.sequence() == 1 && s.held().get("photo").equals("pointer:1"));
        s.held().clear();
        check(s.held().size() == 1);
        rejects(() -> command(s, 1, "down", "pointer:2"));
        rejects(() -> command(s, 2, "down", "pointer:1"));
        rejects(() -> command(s, 2, "up", "pointer:2"));
        rejects(() -> command(s, 2, "hold", "pointer:1"));
        rejects(() -> command(s, 1000002, "up", "pointer:1"));
        WheelInputSession.Command up = command(s, 2, "up", "pointer:1");
        check(Arrays.equals(up.actions, new int[] {8192, 65536}));
        s.commit(up);
        check(s.held().isEmpty());
        s.commit(command(s, 3, "down", "pointer:2"));
        WheelInputSession.Command cancel = command(s, 4, "cancel", "pointer:2");
        check(Arrays.equals(cancel.actions, new int[] {65536}));
        s.commit(cancel);
        rejects(() -> s.commit(cancel));
        check(WheelInputSession.keyFor("menu") == 3074 && WheelInputSession.keyFor("ok") == 6);
        check(
                WheelInputSession.keyFor("photo") == 173
                        && WheelInputSession.keyFor("custom") == 3090);
        check(
                WheelInputSession.keyFor("night_vision") == 0
                        && WheelInputSession.keyFor("ica") == 0);
        WheelInputSession restored = new WheelInputSession(session, token, 4);
        rejects(() -> command(restored, 4, "down", "pointer:3"));
        java.util.Map<String, String> holds = new java.util.LinkedHashMap<>();
        holds.put("photo", "pointer:2");
        restored.restoreHeld(holds);
        holds.clear();
        check(restored.held().size() == 1);
        restored.clearHeld();
        check(restored.held().isEmpty());
    }

    private static void volumeHold() {
        long[] now = {0};
        WheelInputSession s =
                new WheelInputSession(
                        "volume-hold", "0123456789abcdef0123456789abcdef", 0, () -> now[0]);

        s.commit(s.prepare(1, "volume_down", "down", "pointer:1"));
        now[0] = 999;
        check(!s.startLong("volume_down", "pointer:1"));
        WheelInputSession.Command shortUp = s.prepare(2, "volume_down", "up", "pointer:1");
        check(Arrays.equals(shortUp.actions, new int[] {8192, 65536}));
        s.commit(shortUp);
        s.commit(s.prepare(3, "volume_down", "down", "pointer:1"));
        now[0] = 1999;
        check(!s.startLong("volume_down", "pointer:2"));
        check(s.startLong("volume_down", "pointer:1"));
        check(!s.startLong("volume_down", "pointer:1"));
        WheelInputSession.Command longUp = s.prepare(4, "volume_down", "up", "pointer:1");
        check(Arrays.equals(longUp.actions, new int[] {524288, 65536}));
        s.commit(longUp);
        s.commit(s.prepare(5, "volume_down", "down", "pointer:1"));
        now[0] = 3999;
        WheelInputSession.Command cancel = s.prepare(6, "volume_down", "cancel", "pointer:1");
        check(Arrays.equals(cancel.actions, new int[] {65536}));
        s.commit(cancel);
        check(!s.startLong("volume_down", "pointer:1"));
        s.commit(s.prepare(7, "volume_down", "down", "pointer:1"));
        s.clearHeld();
        check(!s.startLong("volume_down", "pointer:1"));
    }

    private static void policy() {
        KeyEventRouter router = new KeyEventRouter();
        List<String> got = new ArrayList<>();
        KeyEventRouter.Callback normal =
                (code, action, pkg, type) -> {
                    got.add(pkg + ":" + type + ":" + code);

                    return false;
                };
        int[] filter = {173};

        check(router.register("camera", 0, 1, filter, normal));
        filter[0] = 6;
        check(router.dispatch(173, 8192).callbacks == 1 && got.get(0).equals("camera:0:173"));
        check(router.dispatch(3074, 8192).callbacks == 0);
        got.clear();
        KeyEventRouter.Callback consume =
                (code, action, pkg, type) -> {
                    got.add(pkg);

                    return true;
                };
        router.register("low", 1, 10, null, consume);
        router.register("high", 1, 90, new int[] {173}, consume);
        KeyEventRouter.Delivery intercepted = router.dispatch(173, 8192);
        check(
                intercepted.callbacks == 1
                        && intercepted.consumed
                        && got.equals(Arrays.asList("high")));
        check(intercepted.callbackReceipts.size() == 1);
        check(intercepted.callbackReceipts.get(0).get("registration").equals("high"));
        check(intercepted.callbackReceipts.get(0).get("type").equals(1));
        check(intercepted.callbackReceipts.get(0).get("action").equals(8192));
        check(intercepted.callbackReceipts.get(0).get("consumed").equals(true));
        router.unregister("high", 1, consume);
        router.unregister("low", 1, consume);
        KeyEventRouter.Callback dead =
                (code, action, pkg, type) -> {
                    throw new Exception("dead");
                };
        router.register("camera", 0, 1, null, dead);
        router.unregister("camera", 0, normal);
        check(router.dispatch(173, 8192).callbacks == 0);
        check(router.dispatch(173, 8192).callbacks == 0);
        router.register("camera", 0, 1, null, normal);
        router.restriction(true, new int[] {173});
        check(router.dispatch(3074, 4096).restricted);
        check(router.dispatch(3074, 65536).callbacks == 1);
        check(!router.dispatch(173, 8192).restricted);
        router.restriction(false, null);
        got.clear();
        router.register("media-other", 0, 1, null, normal);
        router.register("com.qinggan.media", 0, 1, null, normal);
        check(
                router.dispatch(6, 8192).callbacks == 1
                        && got.equals(Arrays.asList("com.qinggan.media:0:6")));
        got.clear();
        router.mediaOwner("media-other");
        check(
                router.dispatch(3, 8192).callbacks == 1
                        && got.equals(Arrays.asList("media-other:0:3")));
        got.clear();
        router.register("volume-interceptor", 1, 100, null, consume);
        check(router.dispatch(2, 8192).callbacks == 0);
        router.register("com.qinggan.app.factorytest", 1, 90, null, consume);
        check(router.dispatch(2, 8192).callbacks == 1);
        check(router.mapping(3090) == 65 && router.mapping(173) == 173);
        router.mapping(3090, 3117);
        check(router.mapping(3090) == 3117);
        KeyEventRouter crowded = new KeyEventRouter();

        for (int i = 0; i < 10; i++) {
            crowded.register("wildcard-" + i, 0, 1, null, (code, action, pkg, type) -> false);
        }

        crowded.register(
                "com.qinggan.media", 0, 1, new int[] {65}, (code, action, pkg, type) -> false);
        KeyEventRouter.Delivery src = crowded.dispatch(3090, WheelInputSession.SHORT);
        check(src.callbacks == 11 && src.callbackReceipts.size() == 8 && src.srcNotified);
        check(!crowded.dispatch(3090, WheelInputSession.UP).srcNotified);
    }

    private static void longPhases() {
        long[] now = {10000};
        WheelInputSession session =
                new WheelInputSession(
                        "long_session_0001", "0123456789abcdef0123456789abcdef", 0, () -> now[0]);
        long sequence = 0;

        for (String button : Arrays.asList("custom", "photo", "phone")) {
            session.commit(session.prepare(++sequence, button, "down", "owner"));
            now[0] += 999;
            check(!session.startLong(button, "owner"));
            now[0]++;
            check(!session.startLong(button, "other"));
            check(session.startLong(button, "owner"));
            check(!session.startLong(button, "owner"));
            WheelInputSession.Command up = session.prepare(++sequence, button, "up", "owner");
            check(Arrays.equals(up.actions, new int[] {524288, 65536}));
            session.commit(up);
            check(!session.startLong(button, "owner") && session.held().isEmpty());
            now[0]++;
        }

        session.commit(session.prepare(++sequence, "photo", "down", "owner"));
        now[0] += 999;
        WheelInputSession.Command shortUp = session.prepare(++sequence, "photo", "up", "owner");
        check(Arrays.equals(shortUp.actions, new int[] {8192, 65536}));
        session.commit(shortUp);
        session.commit(session.prepare(++sequence, "phone", "down", "owner"));
        now[0] += 1000;
        // A delayed Handler must still classify the physical release correctly, without SHORT.
        WheelInputSession.Command lateUp = session.prepare(++sequence, "phone", "up", "owner");
        check(Arrays.equals(lateUp.actions, new int[] {32768, 524288, 65536}));
        session.commit(lateUp);
        session.commit(session.prepare(++sequence, "custom", "down", "owner"));
        now[0] += 1000;
        WheelInputSession.Command cancel = session.prepare(++sequence, "custom", "cancel", "owner");
        check(Arrays.equals(cancel.actions, new int[] {65536}));
        session.commit(cancel);
        check(!session.startLong("custom", "owner"));
        session.commit(session.prepare(++sequence, "volume_up", "down", "owner"));
        now[0] += 2000;
        check(!session.startLong("volume_up", "owner"));
        WheelInputSession.Command volumeUp =
                session.prepare(++sequence, "volume_up", "up", "owner");
        check(Arrays.equals(volumeUp.actions, new int[] {8192, 65536}));
        session.commit(volumeUp);
    }

    private static final class Client extends com.qinggan.system.IKeyManagerCallback.Stub {
        int events;
        int raw;
        int type;
        int action;
        int code;
        boolean consumed;
        boolean fail;

        public boolean onKeyEvent(com.qinggan.system.QGKeyEvent event) {
            raw++;

            return false;
        }

        public boolean onEvent(com.qinggan.system.QGKeyEvent event, String pkg, int type)
                throws android.os.RemoteException {
            if (fail) {
                throw new android.os.RemoteException();
            }

            events++;
            this.type = type;
            action = event.getAction();
            code = event.getKeyCode();

            return consumed;
        }
    }

    private static void binder() {
        KeyManagerStub manager = new KeyManagerStub();
        android.os.Binder token = new android.os.Binder();
        Client first = new Client();

        check(
                manager.registerKeyManagerListener(token, first, new int[] {173}, "camera", 0, 50)
                        == 0);
        check(token.links.size() == 1 && first.links.size() == 1);
        manager.dispatchQG(173, 8192);
        check(first.events == 1 && first.raw == 0 && first.type == 0 && first.code == 173);
        Client second = new Client();
        android.os.Binder newToken = new android.os.Binder();
        check(
                manager.registerKeyManagerListener(
                                newToken, second, new int[] {173}, "camera", 0, 50)
                        == 0);
        check(token.links.isEmpty() && first.links.isEmpty());
        manager.unregisterKeyManagerListener(first, "camera");
        manager.dispatchQG(173, 8192);
        check(second.events == 1 && first.events == 1);
        newToken.die();
        check(second.links.isEmpty());
        check(manager.dispatchQG(173, 8192).callbacks == 0);
        android.os.Binder deadToken = new android.os.Binder();
        deadToken.die();
        check(manager.registerKeyManagerListener(deadToken, new Client(), null, "dead", 0, 50) < 0);
        Client failed = new Client();
        failed.fail = true;
        android.os.Binder failToken = new android.os.Binder();
        manager.registerKeyManagerListener(failToken, failed, null, "fails", 0, 50);
        check(
                manager.dispatchQG(173, 8192).callbacks == 0
                        && failToken.links.isEmpty()
                        && failed.links.isEmpty());
        Client regular = new Client();
        Client intercept = new Client();
        intercept.consumed = true;
        manager.registerKeyManagerListener(new android.os.Binder(), regular, null, "camera", 0, 50);
        manager.registerInterceptListener(
                new android.os.Binder(), intercept, null, "capture", 0, 90);
        check(
                manager.dispatchQG(173, 8192).callbacks == 1
                        && intercept.type == 1
                        && regular.events == 0);
        manager.unregisterInterceptListener(intercept, "capture");
        manager.inputQGKeyEvent(new com.qinggan.system.QGKeyEvent(173, 4096), "caller");
        check(regular.events == 1 && regular.action == 4096 && regular.raw == 0);
        manager.inputKeyEvent(173, 1, "caller");
        check(regular.events == 4 && regular.action == 65536);
        manager.inputKeyEvent(173, 2, "caller");
        check(regular.events == 4);
        manager.unregisterKeyManagerListener(regular, "camera");
        check(regular.links.isEmpty());
    }

    private static final class TerminalContext extends android.content.Context {
        int starts, broadcasts;
        boolean failStart;
        android.content.Intent last;
        final List<Integer> mediaCodes = new ArrayList<>();
        final android.media.AudioManager audio =
                new android.media.AudioManager() {
                    @Override
                    public void dispatchMediaKeyEvent(android.view.KeyEvent event) {
                        mediaCodes.add(event.getKeyCode());
                    }
                };

        @Override
        public Object getSystemService(String name) {
            return audio;
        }

        @Override
        public void startActivity(android.content.Intent intent) {
            if (failStart) {
                throw new IllegalStateException("activity unavailable");
            }

            starts++;
            last = intent;
        }

        @Override
        public void sendBroadcast(android.content.Intent intent) {
            broadcasts++;
            last = intent;
        }
    }

    private static void terminalModes(KeyManagerStub manager, TerminalContext context) {
        android.os.SystemProperties.VALUES.put("com.qinggan.restmode.state", "on");
        KeyEventRouter.Delivery blocked = manager.dispatchQG(3090, 8192);
        check(
                blocked.terminals == 0
                        && blocked.terminalFailure.equals("src-mode-blocked")
                        && context.starts == 2);
        android.os.SystemProperties.VALUES.clear();
        android.os.SystemProperties.VALUES.put("com.qinggan.campmode.state", "on");
        check(
                manager.dispatchQG(3090, 8192).terminalFailure.equals("src-mode-blocked")
                        && context.starts == 2);
        android.os.SystemProperties.VALUES.clear();
        context.failStart = true;
        KeyEventRouter.Delivery unavailable = manager.dispatchQG(3090, 8192);
        check(unavailable.terminalFailure.equals("method-exception"));
        check(KeyManagerStub.deliveryStatus(unavailable).equals("terminal-unavailable"));
        context.failStart = false;
        manager.mappingKeyEvent(3090, 3090, "test");
        check(manager.dispatchQG(3090, 32768).terminals == 1 && context.broadcasts == 1);
        check(context.last.getAction().equals("com.qinggan.app.setting.STEERING_WHEEL_SETTING"));

        for (int code : new int[] {173, 128}) {
            Client listener = new Client();
            manager.registerKeyManagerListener(
                    new android.os.Binder(), listener, new int[] {code}, "oem-app", 0, 50);
            KeyEventRouter.Delivery result = manager.dispatchQG(code, 8192);
            check(result.callbacks == 1 && !result.consumed && result.terminals == 0);
            check(KeyManagerStub.deliveryStatus(result).equals("callbacks"));
            check(context.starts == 2 && context.broadcasts == 1);
            manager.unregisterKeyManagerListener(listener, "oem-app");
        }

        Client media = new Client();
        manager.registerKeyManagerListener(
                new android.os.Binder(), media, new int[] {3, 4, 6}, "com.qinggan.media", 0, 50);

        for (int code : new int[] {3, 4, 6}) {
            check(manager.dispatchQG(code, 8192).terminals == 0);
        }

        check(media.events == 3 && context.mediaCodes.isEmpty());
        manager.unregisterKeyManagerListener(media, "com.qinggan.media");

        for (int code : new int[] {3, 4, 6}) {
            check(manager.dispatchQG(code, 8192).terminals == 1);
        }

        check(context.mediaCodes.equals(Arrays.asList(87, 87, 88, 88, 85, 85)));
    }

    private static void terminals() {
        TerminalContext context = new TerminalContext();
        KeyManagerStub manager = new KeyManagerStub();

        manager.attach(context);
        check(manager.dispatchQG(3090, 4096).terminals == 0);
        KeyEventRouter.Delivery src = manager.dispatchQG(3090, 8192);
        check(src.terminals == 1 && src.consumed && context.starts == 1);
        check(context.last.getComponent().getPackageName().equals("com.qinggan.media"));
        check(
                context.last
                        .getComponent()
                        .getClassName()
                        .equals("com.qingang.asgard.media.general.src.SrcMediaActivity"));
        check(context.last.getFlags() == android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
        check(manager.dispatchQG(3090, 65536).terminals == 0 && context.starts == 1);
        // Default TranslatorSink mapping also applies to LONG; do not invent an unassigned-star
        // action.
        check(manager.dispatchQG(3090, 32768).terminals == 0 && context.broadcasts == 0);
        // Unrelated wildcard notifications do not implement the OEM SRC action.
        Client wildcard = new Client();
        manager.registerKeyManagerListener(
                new android.os.Binder(), wildcard, null, "com.qinggan.app.launcher", 0, 50);
        KeyEventRouter.Delivery fallback = manager.dispatchQG(3090, 8192);
        check(
                fallback.callbacks == 1
                        && fallback.terminals == 1
                        && fallback.consumed
                        && context.starts == 2);
        manager.unregisterKeyManagerListener(wildcard, "com.qinggan.app.launcher");
        // The real media registration may perform SRC while its NORMAL wrapper returns false.
        Client srcConsumer = new Client();
        manager.registerKeyManagerListener(
                new android.os.Binder(), srcConsumer, new int[] {65}, "com.qinggan.media", 0, 50);
        KeyEventRouter.Delivery notified = manager.dispatchQG(3090, 8192);
        check(
                notified.callbacks == 1
                        && !notified.consumed
                        && notified.terminals == 0
                        && context.starts == 2);
        check(notified.srcNotified && !fallback.srcNotified);
        manager.unregisterKeyManagerListener(srcConsumer, "com.qinggan.media");
        Client consumedSrc = new Client();
        consumedSrc.consumed = true;
        manager.registerInterceptListener(
                new android.os.Binder(), consumedSrc, new int[] {65}, "intercept-src", 0, 90);
        check(manager.dispatchQG(3090, 8192).terminals == 0 && context.starts == 2);
        manager.unregisterInterceptListener(consumedSrc, "intercept-src");
        terminalModes(manager, context);
    }

    public static void main(String[] args) {
        phases();
        longPhases();
        volumeHold();
        policy();
        binder();
        terminals();
        System.out.println("PASS wheel guards/routing/lifecycle checks=" + checks);
    }
}
