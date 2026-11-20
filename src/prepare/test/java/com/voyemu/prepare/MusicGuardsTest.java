package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;

public final class MusicGuardsTest {

    interface Check {
        void run() throws Exception;
    }

    static void rejects(Check c) throws Exception {
        try {
            c.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("Music guard accepted an incompatible input");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Map<String, Object>
                constants =
                        TestSteps.select(
                                root, "music_fullscreen_boundary", true, MusicFullscreenGuard.NAME),
                other =
                        TestSteps.select(
                                root,
                                "music_fullscreen_boundary",
                                false,
                                MusicFullscreenGuard.NAME);
        Path a611 = TestSteps.original(root, constants, "MusicConcate.apk"),
                a731 = TestSteps.original(root, other, "MusicConcate.apk");
        byte[] d611 = NativeCore.entries(a611).get("classes.dex"),
                d731 = NativeCore.entries(a731).get("classes.dex");
        byte[] page = MusicPageGuard.apply(constants, 30, a611, d611);

        MusicFullscreenGuard.validateOriginalChain(constants, 30, a611, d611);
        byte[] full = MusicFullscreenGuard.apply(constants, 30, a611, page);

        if (Arrays.equals(d611, page) || Arrays.equals(page, full)) {
            throw new AssertionError("guards must change their intended DEX");
        }

        var support = MusicPageGuardTest.method(page, MusicPageGuard.OWNER, "isSupportKwMusic");

        if (!MusicPageGuardTest.execute(support, true, false, 0)
                .equals(new MusicPageGuardTest.Execution(0, 1))) {
            throw new AssertionError("missing Kuwo must disable optional page");
        }

        if (!MusicPageGuardTest.execute(support, false, true, 0)
                .equals(new MusicPageGuardTest.Execution(0, 0))) {
            throw new AssertionError("disabled page must not probe");
        }

        if (!MusicPageGuardTest.execute(support, true, true, 0)
                .equals(new MusicPageGuardTest.Execution(1, 1))) {
            throw new AssertionError("available page must remain enabled");
        }

        MusicFullscreenGuardTest.verify(
                MusicFullscreenGuardTest.method(page), MusicFullscreenGuardTest.method(full));
        rejects(() -> MusicPageGuard.apply(constants, 30, a731, d731));
        rejects(() -> MusicPageGuard.apply(other, 30, a611, d611));
        rejects(() -> MusicPageGuard.apply(constants, 30, a611, page));
        rejects(() -> MusicFullscreenGuard.apply(constants, 30, a731, d731));
    }
}
