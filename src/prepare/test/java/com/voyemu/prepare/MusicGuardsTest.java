package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Arrays;

public final class MusicGuardsTest {
    static final String OTHER = "free-11-chinese-20260731-7.3.1";

    static final String PROFILE = "free-11-chinese-20250710-6.11.1";

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
        Path a611 =
                root.resolve(
                        ".local/profiles/free-11-chinese-20250710-6.11.1/originals/MusicConcate.apk");
        Path a731 =
                root.resolve(
                        ".local/profiles/free-11-chinese-20260731-7.3.1/originals/MusicConcate.apk");
        byte[] d611 = NativeCore.entries(a611).get("classes.dex"),
                d731 = NativeCore.entries(a731).get("classes.dex");
        byte[] page = MusicPageGuard.apply(PROFILE, 30, a611, d611);

        MusicFullscreenGuard.validateOriginalChain(PROFILE, 30, a611, d611);
        byte[] full = MusicFullscreenGuard.apply(PROFILE, 30, a611, page);

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
        rejects(() -> MusicPageGuard.apply(PROFILE, 30, a731, d731));
        rejects(() -> MusicPageGuard.apply(OTHER, 30, a611, d611));
        rejects(() -> MusicPageGuard.apply(PROFILE, 30, a611, page));
        rejects(() -> MusicFullscreenGuard.apply(PROFILE, 30, a731, d731));
    }
}
