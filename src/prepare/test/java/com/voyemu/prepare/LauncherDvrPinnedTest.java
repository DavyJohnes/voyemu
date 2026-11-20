package com.voyemu.prepare;

import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class LauncherDvrPinnedTest {
    /** Step constants of a Launcher whose DVR reads have pinned instruction indices. */
    static Map<String, Object> constants(Path original) throws Exception {
        return Map.of(
                StepConstants.PINS,
                List.of(FilesSupport.hash(original)),
                "launcher_power_boundary",
                false,
                "aqy_boundary",
                false,
                "launcher_dvr_pinned",
                true);
    }

    static void check(boolean v, String m) {
        if (!v) {
            throw new AssertionError(m);
        }
    }

    static long refs(Method m) {
        long n = 0;

        for (var i : m.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction r
                    && r.getReference()
                            .toString()
                            .equals(LauncherDvrGuard.HELPER + "->select(I)I")) {
                n++;
            }
        }

        return n;
    }

    static void verifyService(Method m) {
        var list = new ArrayList<Instruction>();

        for (var i : m.getImplementation().getInstructions()) {
            list.add(i);
        }

        String[] names = {
            "com.qinggan.camera",
            "com.qinggan.camera.CameraService",
            "launcher_app",
            "dest_display_id",
            "in_type"
        };
        int[] idx = {236, 237, 239, 241, 243}, regs = {2, 3, 2, 1, 10};

        for (int k = 0; k < idx.length; k++) {
            var i = list.get(idx[k]);
            check(
                    i.getOpcode() == org.jf.dexlib2.Opcode.CONST_STRING
                            && ((OneRegisterInstruction) i).getRegisterA() == regs[k]
                            && ((ReferenceInstruction) i)
                                    .getReference()
                                    .toString()
                                    .equals(names[k]),
                    "OEM service component/extra changed");
        }

        int[][] inv = {
            {238, 0, 2, 3}, {240, 0, 2, 1}, {242, 0, 1, 10}, {244, 0, 10, 9}, {245, 8, 0, 0}
        };

        for (int[] row : inv) {
            var i = list.get(row[0]);
            var r = (FiveRegisterInstruction) i;
            check(
                    r.getRegisterC() == row[1]
                            && r.getRegisterD() == row[2]
                            && r.getRegisterE() == row[3],
                    "OEM service lost input registers");
        }

        check(
                ((ReferenceInstruction) list.get(245))
                        .getReference()
                        .toString()
                        .equals(
                                "Lcom/qinggan/launcher/base/utils/AppLauncher;->startService(Landroid/content/Context;Landroid/content/Intent;)V"),
                "OEM service invocation changed");
        int addr = 0;

        for (int i = 0; i < 246; i++) {
            addr += list.get(i).getCodeUnits();
        }

        var jump = list.get(246);
        check(jump.getOpcode() == org.jf.dexlib2.Opcode.GOTO_16, "OEM service exit missing");
        int target = addr + ((OffsetInstruction) jump).getCodeOffset(), walk = 0;
        boolean exits = false;

        for (var i : list) {
            if (walk == target) {
                exits = i.getOpcode() == org.jf.dexlib2.Opcode.RETURN_VOID;
            }

            walk += i.getCodeUnits();
        }

        check(exits, "OEM service route must return");
    }

    static String simulateRoute(
            Method m, boolean b56, boolean b97, boolean nullIntent, boolean camera) {
        var byAddress = new TreeMap<Integer, Instruction>();
        int address = 0, index = 0, start = -1;

        for (var ins : m.getImplementation().getInstructions()) {
            if (index++ == 227) {
                start = address;
            }

            byAddress.put(address, ins);
            address += ins.getCodeUnits();
        }

        int pc = start;
        boolean value = false;

        for (int step = 0; step < 40; step++) {
            var ins = byAddress.get(pc);
            check(ins != null, "invalid route target");
            String ref = ins instanceof ReferenceInstruction r ? r.getReference().toString() : "";

            if (ref.endsWith("->is56B()Z")) {
                value = b56;
            } else if (ref.endsWith("->is97C()Z")) {
                value = b97;
            } else if (ref.equals(LauncherDvrGuard.HELPER + "->useService(Ljava/lang/Object;)Z")) {
                value = nullIntent && camera;
            } else if (ins.getOpcode() == org.jf.dexlib2.Opcode.NEW_INSTANCE) {
                return "service";
            } else if (ref.equals(
                    "Landroid/content/Intent;->addFlags(I)Landroid/content/Intent;")) {
                return nullIntent ? "crash" : "activity";
            }

            if (ins instanceof OffsetInstruction o
                    && (ins.getOpcode() == org.jf.dexlib2.Opcode.GOTO
                            || ins.getOpcode() == org.jf.dexlib2.Opcode.GOTO_16
                            || (ins.getOpcode() == org.jf.dexlib2.Opcode.IF_NEZ && value)
                            || (ins.getOpcode() == org.jf.dexlib2.Opcode.IF_EQZ && !value))) {
                pc += o.getCodeOffset();
            } else {
                pc += ins.getCodeUnits();
            }
        }

        throw new AssertionError("route did not terminate");
    }

    public static void main(String[] a) throws Exception {
        Path original = Path.of(a[0]);
        byte[] raw = LauncherCanbusGuard.original(original);
        byte[] power =
                LauncherPowerGuard.apply(constants(original), 30, original, raw, "classes2.dex");
        byte[] aqy = AqyProviderGuard.apply(constants(original), 30, original, power);
        byte[] before =
                LauncherCanbusGuard.apply(constants(original), 30, "arm64-v8a", original, aqy);

        var old = LauncherDvrGuard.classes(before);
        byte[] after = applyAndCheckClasses(original, before, old);
        var out = LauncherDvrGuard.classes(after);

        checkOwnerMethods(old, out);
        var originalRoute = LauncherDvrGuard.Pinned.route(old);
        var patchedRoute = LauncherDvrGuard.Pinned.route(out);
        checkRoutes(originalRoute, patchedRoute);
        checkHelperBranch(patchedRoute);
        checkRouteOwner(old, out, originalRoute, patchedRoute);
        checkDenials(original, before);
        System.out.println(
                "PASS exact Launcher DVR-only availability and unrelated methods preserved");
    }

    static byte[] applyAndCheckClasses(Path original, byte[] before, Map<String, ClassDef> old)
            throws Exception {
        for (String name : LauncherDvrGuard.Pinned.METHODS) {
            check(
                    refs(LauncherDvrGuard.Pinned.method(old, name)) == 0,
                    "RED: OEM DVR entries exclude real virtual camera");
        }

        byte[] after =
                LauncherDvrGuard.apply(constants(original), 30, "arm64-v8a", original, before);
        var out = LauncherDvrGuard.classes(after);
        check(old.keySet().equals(out.keySet()), "class set drift");

        for (var e : old.entrySet()) {
            if (!e.getKey().equals(LauncherDvrGuard.OWNER)
                    && !e.getKey().equals("Lcom/qinggan/launcher/base/utils/AppLauncher;")) {
                check(
                        FrameworkDexMaterializer.semantic(e.getValue())
                                .equals(FrameworkDexMaterializer.semantic(out.get(e.getKey()))),
                        "unrelated class changed");
            }
        }

        return after;
    }

    static void checkOwnerMethods(Map<String, ClassDef> old, Map<String, ClassDef> out)
            throws Exception {
        for (var m : old.get(LauncherDvrGuard.OWNER).getMethods()) {
            Method replacement = null;

            for (var n : out.get(LauncherDvrGuard.OWNER).getMethods()) {
                if (n.getName().equals(m.getName())
                        && n.getParameterTypes().equals(m.getParameterTypes())
                        && n.getReturnType().equals(m.getReturnType())) {
                    replacement = n;
                }
            }

            check(replacement != null, "method missing");

            if (LauncherDvrGuard.Pinned.METHODS.contains(m.getName())) {
                check(refs(replacement) == 1, "one DVR-only selector required");
            } else {
                check(
                        FrameworkDexMaterializer.semantic(m)
                                .equals(FrameworkDexMaterializer.semantic(replacement)),
                        "unrelated method changed: " + m.getName());
            }
        }
    }

    static void checkRoutes(Method originalRoute, Method patchedRoute) {
        verifyService(originalRoute);
        verifyService(patchedRoute);
        check(
                simulateRoute(originalRoute, false, true, true, true).equals("crash"),
                "RED: null OEM intent must reproduce crash");
        check(
                simulateRoute(patchedRoute, false, true, true, true).equals("service"),
                "GREEN: available virtual camera uses OEM service");
        check(
                simulateRoute(patchedRoute, false, true, false, true).equals("activity"),
                "valid OEM intent must remain");
        check(
                simulateRoute(patchedRoute, false, true, true, false).equals("crash"),
                "unavailable camera must preserve OEM branch");
        check(
                simulateRoute(patchedRoute, false, false, true, true).equals("service"),
                "existing service branch must remain");
        check(
                simulateRoute(patchedRoute, true, false, true, true).equals("service"),
                "56B null path uses OEM service");
    }

    static void checkHelperBranch(Method patchedRoute) {
        int[] marks = helperMarks(patchedRoute);
        int service = marks[0],
                branch = marks[1],
                helper = marks[2],
                move = marks[3],
                oldTag = marks[4],
                addFlags = marks[5];
        int pos = 0;
        boolean serviceStart = false;

        for (var i : patchedRoute.getImplementation().getInstructions()) {
            if (pos == service) {
                serviceStart = i.getOpcode() == org.jf.dexlib2.Opcode.NEW_INSTANCE;
            }

            pos += i.getCodeUnits();
        }

        check(
                helper >= 0
                        && move > helper
                        && branch > move
                        && oldTag > branch
                        && addFlags > oldTag
                        && serviceStart,
                "null-intent helper must branch to OEM service before original addFlags");
    }

    /**
     * Returns service, branch, helper, move, oldTag and addFlags addresses of the patched route.
     */
    static int[] helperMarks(Method patchedRoute) {
        int address = 0,
                service = -1,
                branch = -1,
                helper = -1,
                move = -1,
                oldTag = -1,
                addFlags = -1;

        for (var i : patchedRoute.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction r) {
                String ref = r.getReference().toString();

                if (ref.equals(LauncherDvrGuard.HELPER + "->useService(Ljava/lang/Object;)Z")) {
                    helper = address;
                }

                if (helper >= 0
                        && oldTag < 0
                        && ref.equals(
                                "Lcom/qinggan/launcher/base/utils/AppLauncher;->TAG:Ljava/lang/String;")) {
                    oldTag = address;
                }

                if (ref.equals("Landroid/content/Intent;->addFlags(I)Landroid/content/Intent;")
                        && helper >= 0
                        && addFlags < 0) {
                    addFlags = address;
                }
            }

            if (helper >= 0 && move < 0 && i.getOpcode() == org.jf.dexlib2.Opcode.MOVE_RESULT) {
                move = address;
            }

            if (helper >= 0 && branch < 0 && i.getOpcode() == org.jf.dexlib2.Opcode.IF_NEZ) {
                branch = address;
                service = address + ((OffsetInstruction) i).getCodeOffset();
            }

            address += i.getCodeUnits();
        }

        return new int[] {service, branch, helper, move, oldTag, addFlags};
    }

    static void checkRouteOwner(
            Map<String, ClassDef> old,
            Map<String, ClassDef> out,
            Method originalRoute,
            Method patchedRoute)
            throws Exception {
        for (var m : old.get("Lcom/qinggan/launcher/base/utils/AppLauncher;").getMethods()) {
            Method found = null;

            for (var x : out.get(m.getDefiningClass()).getMethods()) {
                if (x.getName().equals(m.getName())
                        && x.getParameterTypes().equals(m.getParameterTypes())
                        && x.getReturnType().equals(m.getReturnType())) {
                    found = x;
                }
            }

            check(found != null, "route owner method missing");

            if (!m.getName().equals("startAppBySubType")) {
                check(
                        FrameworkDexMaterializer.semantic(m)
                                .equals(FrameworkDexMaterializer.semantic(found)),
                        "unrelated route owner changed");
            }
        }

        check(
                !FrameworkDexMaterializer.semantic(originalRoute)
                        .equals(FrameworkDexMaterializer.semantic(patchedRoute)),
                "route not patched");
    }

    static void checkDenials(Path original, byte[] before) throws Exception {
        boolean denied = false;

        try {
            LauncherDvrGuard.validate(
                    Map.of(
                            StepConstants.PINS,
                            List.of("0".repeat(64)),
                            "launcher_dvr_pinned",
                            true),
                    30,
                    "arm64-v8a",
                    original,
                    before);
        } catch (Failure f) {
            denied = true;
        }

        check(denied, "foreign original accepted");
    }
}
