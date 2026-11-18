package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.zip.ZipFile;

public final class MusicFullscreenGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static Method method(byte[] dex) {
        for (var c : MusicFullscreenGuard.classes(dex).values()) {
            for (var m : c.getMethods()) {
                if (MusicFullscreenGuard.target(m)) {
                    return m;
                }
            }
        }

        throw new AssertionError("exact owner");
    }

    static void verify(Method before, Method after) {
        var old = new ArrayList<Instruction>();

        before.getImplementation().getInstructions().forEach(old::add);
        var code = new ArrayList<Instruction>();
        after.getImplementation().getInstructions().forEach(code::add);
        check(code.size() == old.size() + 4, "exact public fullscreen API bridge missing");
        check(
                code.get(0).getOpcode() == Opcode.INVOKE_SUPER
                        && ((ReferenceInstruction) code.get(0))
                                .getReference()
                                .toString()
                                .equals(
                                        "Lcom/qinggan/base/BaseActivity;->onCreate(Landroid/os/Bundle;)V"),
                "real original superclass first");
        var get = (FiveRegisterInstruction) code.get(1);
        check(
                get.getRegisterCount() == 1
                        && get.getRegisterC() == 2
                        && ((ReferenceInstruction) get)
                                .getReference()
                                .toString()
                                .equals("Landroid/app/Activity;->getWindow()Landroid/view/Window;"),
                "only real Music Activity window");
        check(
                code.get(2).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ((OneRegisterInstruction) code.get(2)).getRegisterA() == 0,
                "dead v0 Window");
        check(
                code.get(3).getOpcode() == Opcode.CONST_16
                        && ((OneRegisterInstruction) code.get(3)).getRegisterA() == 1
                        && ((NarrowLiteralInstruction) code.get(3)).getNarrowLiteral() == 1024,
                "only FLAG_FULLSCREEN, no navigation/immersive/overlay disable");
        var add = (FiveRegisterInstruction) code.get(4);
        check(
                add.getRegisterCount() == 2
                        && add.getRegisterC() == 0
                        && add.getRegisterD() == 1
                        && ((ReferenceInstruction) add)
                                .getReference()
                                .toString()
                                .equals("Landroid/view/Window;->addFlags(I)V"),
                "preserve all existing windowflags via public addFlags");
        check(
                code.get(5).getOpcode() == Opcode.NEW_INSTANCE
                        && ((OneRegisterInstruction) code.get(5)).getRegisterA() == 3,
                "p1 overwritten by original initialization");
        check(
                after.getImplementation().getRegisterCount() == 4
                        && after.getImplementation().getTryBlocks().isEmpty(),
                "no new register/catch/state");
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked action) throws Exception {
        boolean refused = false;

        try {
            action.run();
        } catch (Failure expected) {
            refused = expected.code.equals("MUSIC_FULLSCREEN_CONTRACT");
        }

        check(refused, "exact fullscreen contract rejection");
    }

    static void rejectOriginalChain(Checked action) throws Exception {
        boolean refused = false;

        try {
            action.run();
        } catch (Failure expected) {
            refused = expected.code.equals("MUSIC_PAGE_CONTRACT");
        }

        check(
                refused,
                "plan phase accepts only exact original DEX, not a predecessor or changed input");
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]).toRealPath(),
                apk = root.resolve(".local/profiles/" + PROFILE + "/originals/MusicConcate.apk");
        byte[] original;

        try (var z = new ZipFile(apk.toFile())) {
            original = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        byte[] rawSnapshot = original.clone();
        MusicFullscreenGuard.validateOriginalChain(PROFILE, 30, apk, original);
        check(Arrays.equals(rawSnapshot, original), "plan validation preserves original raw DEX");
        byte[] predecessor = MusicPageGuard.apply(PROFILE, 30, apk, original);
        rejectOriginalChain(
                () -> MusicFullscreenGuard.validateOriginalChain(PROFILE, 30, apk, predecessor));
        byte[] after =
                a.length > 1 && a[1].equals("--original")
                        ? predecessor
                        : MusicFullscreenGuard.apply(PROFILE, 30, apk, predecessor);
        verify(method(predecessor), method(after));
        check(
                FrameworkDexMaterializer.semantic(
                                MusicPageGuard.classes(predecessor).get(MusicPageGuard.OWNER))
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        MusicPageGuard.classes(after).get(MusicPageGuard.OWNER))),
                "existing Kuwo absent-page fix preserved");
        reject(() -> MusicFullscreenGuard.apply("wrong", 30, apk, predecessor));
        reject(() -> MusicFullscreenGuard.apply(PROFILE, 28, apk, predecessor));
        reject(() -> MusicFullscreenGuard.apply(PROFILE, 30, apk, null));

        if (a.length > 1 && !a[1].equals("--original")) {
            try (var z = new ZipFile(a[1])) {
                check(
                        Arrays.equals(
                                after, z.getInputStream(z.getEntry("classes.dex")).readAllBytes()),
                        "actual normal producer exact fullscreen DEX");
            }
        }

        System.out.println(
                "PRIVATE Music fullscreen proposal PASS: exact owner/body/predecessor, public"
                        + " Window FLAG_FULLSCREEN, original super/flags/OEM init/media/pageguard"
                        + " preserved, all-class inverse; no guest or SDK");
    }
}
