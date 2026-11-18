package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class WirelessReadinessTest {
    static final String BAR = "Lcom/qinggan/mainlauncher/hiboard/HiboardBar;",
            LISTENER = BAR.substring(0, BAR.length() - 1) + "$5;";

    static void check(boolean b, String reason) {
        if (!b) {
            throw new AssertionError(reason);
        }
    }

    static ClassDef type(DexBackedDexFile d, String name) {
        return d.getClasses().stream()
                .filter(x -> x.getType().equals(name))
                .findFirst()
                .orElseThrow();
    }

    static Method method(ClassDef c, String name) {
        return java.util.stream.StreamSupport.stream(c.getMethods().spliterator(), false)
                .filter(x -> x.getName().equals(name))
                .findFirst()
                .orElseThrow();
    }

    static final class State {
        boolean ready, released, initialized;
        int calls;
    }

    static void run(Method m, State state) {
        var ins = new ArrayList<Instruction>();

        m.getImplementation().getInstructions().forEach(ins::add);
        var offsets = new ArrayList<Integer>();
        int total = 0;

        for (var i : ins) {
            offsets.add(total);
            total += i.getCodeUnits();
        }

        int pc = 0, v = 0, steps = 0;

        while (pc < ins.size() && steps++ < 30) {
            Instruction i = ins.get(pc);

            switch (i.getOpcode()) {
                case IGET_BOOLEAN -> {
                    String name =
                            ((FieldReference) ((ReferenceInstruction) i).getReference()).getName();
                    v =
                            name.equals("voyemuReleased")
                                    ? (state.released ? 1 : 0)
                                    : name.equals("mCanBusManagerReady")
                                            ? (state.ready ? 1 : 0)
                                            : name.equals("voyemuWirelessInitialized")
                                                    ? (state.initialized ? 1 : 0)
                                                    : -1;
                    check(v >= 0, "unexpected field");
                }
                case IF_NEZ, IF_EQZ -> {
                    boolean jump = i.getOpcode() == Opcode.IF_NEZ ? v != 0 : v == 0;

                    if (jump) {
                        int target = offsets.get(pc) + ((OffsetInstruction) i).getCodeOffset();
                        pc = offsets.indexOf(target);
                        check(pc >= 0, "branch target");
                        continue;
                    }
                }
                case CONST_4 -> v = ((NarrowLiteralInstruction) i).getNarrowLiteral();
                case IPUT_BOOLEAN -> {
                    check(
                            ((FieldReference) ((ReferenceInstruction) i).getReference())
                                            .getName()
                                            .equals("voyemuWirelessInitialized")
                                    && v == 1,
                            "init flag");
                    state.initialized = true;
                }
                case INVOKE_DIRECT -> {
                    check(
                            ((ReferenceInstruction) i)
                                    .getReference()
                                    .toString()
                                    .contains("->setupWirelessCharging()V"),
                            "original setup call");
                    state.calls++;
                }
                case RETURN_VOID -> {
                    return;
                }
                default -> throw new AssertionError("unexpected opcode " + i.getOpcode());
            }

            pc++;
        }

        throw new AssertionError("invalid control flow");
    }

    static boolean hasInvoke(Method m, String wanted) {
        for (Instruction i : m.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction r
                    && r.getReference().toString().contains(wanted)) {
                return true;
            }
        }

        return false;
    }

    static void startupWireless(Method method, boolean connected) {
        var ins = new ArrayList<Instruction>();

        method.getImplementation().getInstructions().forEach(ins::add);
        var offsets = new ArrayList<Integer>();
        int total = 0;

        for (var i : ins) {
            offsets.add(total);
            total += i.getCodeUnits();
        }

        int pc = 0, v = 0, pending = 0, steps = 0;

        while (pc < ins.size() && steps++ < 30) {
            Instruction i = ins.get(pc);

            switch (i.getOpcode()) {
                case IGET_OBJECT -> v = 1;
                case IF_NEZ, IF_EQZ -> {
                    boolean jump = i.getOpcode() == Opcode.IF_NEZ ? v != 0 : v == 0;

                    if (jump) {
                        pc =
                                offsets.indexOf(
                                        offsets.get(pc) + ((OffsetInstruction) i).getCodeOffset());
                        check(pc >= 0, "old setup branch");
                        continue;
                    }
                }
                case INVOKE_STATIC -> {
                    String ref = ((ReferenceInstruction) i).getReference().toString();

                    if (ref.contains("->isSupportWirelessCharge()Z")) {
                        pending = 1;
                    } else if (ref.contains("->isWirelessChargeOn()Z")) {
                        if (!connected) {
                            throw new IllegalStateException("OEM CanBusManager not connected");
                        }

                        return;
                    } else {
                        throw new AssertionError("unexpected startup call " + ref);
                    }
                }
                case MOVE_RESULT -> v = pending;
                case CONST_4 -> v = ((NarrowLiteralInstruction) i).getNarrowLiteral();
                case INVOKE_VIRTUAL -> {}
                case RETURN_VOID -> {
                    return;
                }
                default -> throw new AssertionError("unexpected startup opcode " + i.getOpcode());
            }

            pc++;
        }

        throw new AssertionError("old setup did not reach CAN read");
    }

    static void verify(Path before, Path after, Path originalApk) throws Exception {
        var old = new DexBackedDexFile(Opcodes.forApi(28), Files.readAllBytes(before));
        var now = new DexBackedDexFile(Opcodes.forApi(28), Files.readAllBytes(after));
        var previous = type(old, BAR);
        var current = type(now, BAR);

        if (hasInvoke(method(current, "setupViews"), "->setupWirelessCharging()V")) {
            startupWireless(method(current, "setupWirelessCharging"), false);
        }

        check(current.getInterfaces().contains("Ljava/lang/Runnable;"), "owner is not runnable");

        for (String name : List.of("mCanBusManagerReady", "voyemuReleased")) {
            var field =
                    java.util.stream.StreamSupport.stream(current.getFields().spliterator(), false)
                            .filter(x -> x.getName().equals(name))
                            .findFirst()
                            .orElseThrow();
            check(
                    (field.getAccessFlags() & AccessFlags.VOLATILE.getValue()) != 0,
                    "cross-thread flag must be volatile");
        }

        checkOemDispatcher(originalApk);
        check(
                FrameworkDexMaterializer.semantic(method(previous, "setupWirelessCharging"))
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        method(current, "setupWirelessCharging"))),
                "original setup changed");
        checkRunnerStates(current);
        var oldSetup = method(previous, "setupViews");
        var newSetup = method(current, "setupViews");
        check(
                hasInvoke(oldSetup, "->setupWirelessCharging()V")
                        && !hasInvoke(newSetup, "->setupWirelessCharging()V"),
                "startup call was not deferred");
        var callback = method(type(now, LISTENER), "onConnectStatusChange");
        checkReadyBranch(callback);
        var release = new ArrayList<Instruction>();
        method(current, "release").getImplementation().getInstructions().forEach(release::add);
        check(
                release.get(0).getOpcode() == Opcode.CONST_4
                        && ((NarrowLiteralInstruction) release.get(0)).getNarrowLiteral() == 1
                        && release.get(1).getOpcode() == Opcode.IPUT_BOOLEAN
                        && ((FieldReference) ((ReferenceInstruction) release.get(1)).getReference())
                                .getName()
                                .equals("voyemuReleased"),
                "release must fence before original early return");
        check(
                hasInvoke(
                                method(current, "release"),
                                "->removeOnInitListener(Lcom/qinggan/common/OnInitListener;)V")
                        && hasInvoke(
                                callback,
                                "->addCanBusListener(Lcom/qinggan/canbus/CanBusListener;)V"),
                "OEM listener lifecycle changed");
        System.out.println(
                "PASS original wireless method, deferred UI execution, once, release and"
                        + " reconnect");
    }

    static void checkOemDispatcher(Path originalApk) throws Exception {
        byte[] apkDex;

        try (var zip = new java.util.zip.ZipFile(originalApk.toFile())) {
            apkDex = zip.getInputStream(zip.getEntry("classes2.dex")).readAllBytes();
        }

        var apk = new DexBackedDexFile(Opcodes.forApi(28), apkDex);
        var worker = type(apk, "Lcom/qinggan/launcher/base/thread/WorkerThread;");
        check(
                hasInvoke(
                                method(worker, "<init>"),
                                "Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;")
                        && hasInvoke(
                                method(worker, "postOnUiThread"),
                                "Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z"),
                "OEM dispatcher must post to main handler");
        check(
                hasInvoke(
                                method(
                                        type(apk, "Lcom/qinggan/app/launcher/LauncherModel;"),
                                        "release"),
                                "->runOnUiThread(Ljava/lang/Runnable;)V")
                        && hasInvoke(
                                method(
                                        type(apk, "Lcom/qinggan/app/launcher/LauncherModel$13;"),
                                        "run"),
                                "->release()V"),
                "OEM release must run on UI handler");
    }

    static void checkRunnerStates(ClassDef current) throws Exception {
        Method runner = method(current, "run");
        var s = new State();

        run(runner, s);
        check(s.calls == 0 && !s.initialized, "not ready must defer");
        s.ready = true;
        run(runner, s);
        check(s.calls == 1 && s.initialized, "ready invokes original");
        run(runner, s);
        check(s.calls == 1, "duplicate ready");
        s = new State();
        s.ready = true;
        s.released = true;
        run(runner, s);
        check(s.calls == 0, "queued after release");
        s = new State();
        s.ready = false;
        run(runner, s);
        s.ready = true;
        run(runner, s);
        check(s.calls == 1, "reconnect after queued not-ready");
    }

    static void checkReadyBranch(Method callback) {
        var cb = new ArrayList<Instruction>();

        callback.getImplementation().getInstructions().forEach(cb::add);
        int post = -1, theme = -1, branch = -1, address = 0, branchTarget = -1;
        var offsets = new ArrayList<Integer>();

        for (int index = 0; index < cb.size(); index++) {
            Instruction i = cb.get(index);
            offsets.add(address);

            if (i.getOpcode() == Opcode.IF_NEZ && branch < 0) {
                branch = index;
                branchTarget = address + ((OffsetInstruction) i).getCodeOffset();
            }

            if (i instanceof ReferenceInstruction r) {
                String ref = r.getReference().toString();

                if (ref.contains("->postOnUiThread(Ljava/lang/Runnable;)V") && post < 0) {
                    post = index;
                }

                if (ref.contains("->isClusterManagerSync()Z")) {
                    theme = index;
                }
            }

            address += i.getCodeUnits();
        }

        check(
                branch >= 0
                        && cb.get(branch + 1).getOpcode() == Opcode.RETURN_VOID
                        && offsets.indexOf(branchTarget) > branch
                        && offsets.indexOf(branchTarget) <= post
                        && post < theme,
                "only ready branch posts before original early return");
    }

    public static void main(String[] a) throws Exception {
        verify(Path.of(a[0]), Path.of(a[1]), Path.of(a[2]));
    }
}
