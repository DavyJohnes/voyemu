package com.voyemu.prepare;

import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class CameraVirtualGuardTest {
    static void check(boolean b, String m) {
        if (!b) {
            throw new AssertionError(m);
        }
    }

    static Map<String, Method> methods(ClassDef c) {
        var out = new TreeMap<String, Method>();

        for (var m : c.getMethods()) {
            out.put(m.toString(), m);
        }

        return out;
    }

    static boolean calls(Method m, String name) {
        for (var i : m.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction r
                    && r.getReference()
                            .toString()
                            .contains("Lcom/voyemu/camera/BackCameraProbe;->" + name)) {
                return true;
            }
        }

        return false;
    }

    static Map<String, Object> admitting(Path apk) throws Exception {
        return Map.of(StepConstants.PINS, List.of(FilesSupport.hash(apk)));
    }

    // Arguments: PROFILE APK PREDECESSOR_APK [OTHER_PROFILE_APK]; the profile argument is unused,
    // the step constants admit the APK by its pin.
    public static void main(String[] arguments) throws Exception {
        String[] a = java.util.Arrays.copyOfRange(arguments, 1, arguments.length);
        Path apk = Path.of(a[0]), pred = Path.of(a[1]);
        Map<String, Object> constants = admitting(apk);
        byte[] raw;

        try (var z = new ZipFile(pred.toFile())) {
            raw = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        var before = CameraVirtualGuard.classes(raw);
        checkOriginalUnadapted(before);
        byte[] patched = CameraVirtualGuard.apply(constants, 30, "arm64-v8a", apk, raw);
        var after = CameraVirtualGuard.classes(patched);
        checkUnpinnedPreserved(before, after);
        checkHelperBoundaries(after);
        checkOriginalEntryStore(before);
        checkEntryAdaptation(after);
        checkRejectedScopes(constants, apk, raw);

        if (a.length > 2) {
            checkRejectedCrossVersion(constants, a, apk, raw);
            System.out.println("Cross-version Camera inputs rejected");
        }

        System.out.println(
                "PASS Camera exact pins, scoped guards, OEM methods preserved, backend hooks"
                        + " present; no guest acceptance");
    }

    static void checkOriginalUnadapted(Map<String, ClassDef> before) {
        for (var k : CameraVirtualGuard.TARGETS) {
            for (var m : before.get(k.substring(0, k.indexOf("->"))).getMethods()) {
                if (CameraVirtualGuard.key(m).equals(k)) {
                    check(
                            !calls(m, "isSupported")
                                    && !calls(m, "augment")
                                    && !calls(m, "selectId")
                                    && !calls(m, "preferredQuality")
                                    && !calls(m, "defaultType")
                                    && !calls(m, "dvrEntry"),
                            "RED OEM method already adapted");
                }
            }
        }
    }

    static void checkUnpinnedPreserved(Map<String, ClassDef> before, Map<String, ClassDef> after)
            throws Exception {
        check(before.keySet().equals(after.keySet()), "class coverage");

        for (var e : before.entrySet()) {
            if (!Set.of(
                            CameraVirtualGuard.SUPPORT,
                            CameraVirtualGuard.ACTIVITY,
                            CameraVirtualGuard.FILTER)
                    .contains(e.getKey())) {
                check(
                        FrameworkDexMaterializer.semantic(e.getValue())
                                .equals(FrameworkDexMaterializer.semantic(after.get(e.getKey()))),
                        "other class drift");
                continue;
            }

            var a1 = methods(e.getValue());
            var a2 = methods(after.get(e.getKey()));
            check(a1.keySet().equals(a2.keySet()), "method set drift");

            for (var v : a1.entrySet()) {
                if (!CameraVirtualGuard.TARGETS.contains(CameraVirtualGuard.key(v.getValue()))) {
                    check(
                            FrameworkDexMaterializer.semantic(v.getValue())
                                    .equals(FrameworkDexMaterializer.semantic(a2.get(v.getKey()))),
                            "other owner method drift");
                }
            }
        }
    }

    static void checkHelperBoundaries(Map<String, ClassDef> after) {
        Map<String, String> wanted =
                Map.of(
                        CameraVirtualGuard.SUPPORT + "->isSupportDvr",
                        "isSupported",
                        CameraVirtualGuard.ACTIVITY + "->isDVRCameraError",
                        "isSupported",
                        CameraVirtualGuard.ACTIVITY + "->initIntent",
                        "defaultType",
                        CameraVirtualGuard.ACTIVITY + "->openCamera$lambda-14",
                        "selectId",
                        CameraVirtualGuard.FILTER + "->filter",
                        "augment");

        for (var c : after.values()) {
            for (var m : c.getMethods()) {
                if (wanted.containsKey(CameraVirtualGuard.key(m))) {
                    check(
                            calls(m, wanted.get(CameraVirtualGuard.key(m))),
                            "GREEN helper boundary missing");
                }
            }
        }
    }

    static void checkOriginalEntryStore(Map<String, ClassDef> before) {
        Method originalInit = null;

        for (var m : before.get(CameraVirtualGuard.ACTIVITY).getMethods()) {
            if (m.getName().equals("initIntent")) {
                originalInit = m;
            }
        }

        check(originalInit != null, "original init missing");
        var rawOps = new ArrayList<Instruction>();
        var rawAddr = new ArrayList<Integer>();
        int pc = 0;

        for (var i : originalInit.getImplementation().getInstructions()) {
            rawAddr.add(pc);
            rawOps.add(i);
            pc += i.getCodeUnits();
        }

        int store = -1;

        for (int i = 0; i + 3 < rawOps.size(); i++) {
            if (rawOps.get(i) instanceof ReferenceInstruction r
                    && r.getReference().toString().equals("mInType")
                    && rawOps.get(i + 1) instanceof ReferenceInstruction c
                    && c.getReference()
                            .toString()
                            .equals("Landroid/content/Intent;->getIntExtra(Ljava/lang/String;I)I")
                    && rawOps.get(i + 3) instanceof ReferenceInstruction f
                    && f.getReference()
                            .toString()
                            .equals(
                                    "Lcom/qinggan/camera/activity/RecordVideoActivity;->mInType:I")) {
                store = rawAddr.get(i + 3);
            }
        }

        check(store >= 0, "original DVR entry store missing");

        for (int i = 0; i < rawOps.size(); i++) {
            if (rawOps.get(i) instanceof OffsetInstruction o) {
                check(
                        rawAddr.get(i) + o.getCodeOffset() != store,
                        "incoming direct branch targets original store");
            }
        }

        for (var block : originalInit.getImplementation().getTryBlocks()) {
            for (var handler : block.getExceptionHandlers()) {
                check(
                        handler.getHandlerCodeAddress() != store,
                        "exception handler targets original store");
            }
        }
    }

    static void checkEntryAdaptation(Map<String, ClassDef> after) {
        Method init = null;

        for (var m : after.get(CameraVirtualGuard.ACTIVITY).getMethods()) {
            if (m.getName().equals("initIntent")) {
                init = m;
            }
        }

        check(init != null && calls(init, "dvrEntry"), "GREEN DVR entry adaptation missing");
        var ops = new ArrayList<Instruction>();

        for (var i : init.getImplementation().getInstructions()) {
            ops.add(i);
        }

        boolean entry = false;

        for (int i = 0; i + 5 < ops.size(); i++) {
            if (ops.get(i) instanceof ReferenceInstruction r
                    && r.getReference().toString().equals("mInType")) {
                if (ops.get(i + 1) instanceof ReferenceInstruction call
                        && call.getReference()
                                .toString()
                                .equals(
                                        "Landroid/content/Intent;->getIntExtra(Ljava/lang/String;I)I")
                        && ops.get(i + 3) instanceof ReferenceInstruction hook
                        && hook.getReference()
                                .toString()
                                .equals("Lcom/voyemu/camera/BackCameraProbe;->dvrEntry(I)I")) {
                    check(
                            ops.get(i + 2).getOpcode() == org.jf.dexlib2.Opcode.MOVE_RESULT
                                    && ((OneRegisterInstruction) ops.get(i + 2)).getRegisterA() == 6
                                    && ops.get(i + 4).getOpcode()
                                            == org.jf.dexlib2.Opcode.MOVE_RESULT
                                    && ((OneRegisterInstruction) ops.get(i + 4)).getRegisterA() == 6
                                    && ops.get(i + 5) instanceof ReferenceInstruction field
                                    && field.getReference()
                                            .toString()
                                            .equals(
                                                    "Lcom/qinggan/camera/activity/RecordVideoActivity;->mInType:I"),
                            "entry value not written back to OEM field");
                    check(!entry, "duplicate DVR entry hook");
                    entry = true;
                }
            }
        }

        check(entry, "exact getIntExtra → dvrEntry → mInType path missing");
    }

    static void checkRejectedScopes(Map<String, Object> constants, Path apk, byte[] raw)
            throws Exception {
        for (int i = 0; i < 4; i++) {
            boolean denied = false;

            try {
                CameraVirtualGuard.validate(
                        i == 0 ? Map.of(StepConstants.PINS, List.of()) : constants,
                        i == 1 ? 28 : 30,
                        i == 2 ? "x86" : "arm64-v8a",
                        apk,
                        i == 3 ? null : raw);
            } catch (Failure expected) {
                denied = true;
            }

            check(denied, "scope accepted");
        }
    }

    static void checkRejectedCrossVersion(
            Map<String, Object> constants, String[] a, Path apk, byte[] raw) throws Exception {
        Path other = Path.of(a[2]);
        Map<String, Object> otherConstants = admitting(other);

        for (int i = 0; i < 2; i++) {
            boolean denied = false;

            try {
                CameraVirtualGuard.validate(
                        i == 1 ? otherConstants : constants,
                        30,
                        "arm64-v8a",
                        i == 0 ? other : apk,
                        raw);
            } catch (Failure expected) {
                denied = true;
            }

            check(denied, "cross-version APK or step accepted");
        }
    }
}
