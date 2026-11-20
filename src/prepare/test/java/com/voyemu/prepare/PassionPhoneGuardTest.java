package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;

public final class PassionPhoneGuardTest {
    interface Checked {
        void run() throws Exception;
    }

    static void need(boolean value) {
        if (!value) {
            throw new AssertionError("Exact Passion phone routing");
        }
    }

    static void rejects(Checked value) throws Exception {
        try {
            value.run();
            throw new AssertionError("foreign phone accepted");
        } catch (Failure expected) {
            need(expected.code.equals("PASSION_PHONE_CONTRACT"));
        }
    }

    static int outgoingReferences(DexFile dex) {
        int count = 0;

        for (var c : dex.getClasses()) {
            for (var m : c.getMethods()) {
                if (m.getImplementation() != null) {
                    for (var item : m.getImplementation().getInstructions()) {
                        if (item instanceof ReferenceInstruction r
                                && r.getReference()
                                        .toString()
                                        .equals(PassionPhoneGuard.TELECOM_PLACE_CALL)) {
                            count++;
                        }
                    }
                }
            }
        }

        return count;
    }

    static int uriOperand(Method original, int at) {
        int pc = 0;

        for (var item : original.getImplementation().getInstructions()) {
            if (pc == at) {
                need(
                        item.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && ((ReferenceInstruction) item)
                                        .getReference()
                                        .toString()
                                        .equals(PassionPhoneGuard.TELECOM_PLACE_CALL));

                return ((FiveRegisterInstruction) item).getRegisterD();
            }

            pc += item.getCodeUnits();
        }

        throw new AssertionError("Missing original URI operand");
    }

    static void testDialerManifest(byte[] original) throws Exception {
        byte[] patched = PassionPhoneGuard.dialerManifest(original, false);

        need(patched.length == original.length + 220);
        var patchedXml = ByteBuffer.wrap(patched).order(ByteOrder.LITTLE_ENDIAN);
        need(patchedXml.getInt(4) == patched.length);
        need(patched[8867] == 0x12 && patchedXml.getInt(8868) == 0);
        byte[] restored = Arrays.copyOf(patched, original.length);
        System.arraycopy(patched, 8116, restored, 7896, original.length - 7896);
        ByteBuffer.wrap(restored)
                .order(ByteOrder.LITTLE_ENDIAN)
                .putInt(4, original.length)
                .putInt(8648, -1);
        need(Arrays.equals(original, restored));
        rejects(() -> PassionPhoneGuard.dialerManifest(patched, false));
        rejects(() -> PassionPhoneGuard.dialerManifest(null, false));

        // These libraries are added by passion-api30-bluetooth-role.json before the phone step.
        var libraries =
                List.of("android.car", "voyemu.platform.callbacks", "voyemu.window", "voyemu.mega");
        byte[] withLibraries = ManifestLibraries.add(original, libraries);
        byte[] patchedWithLibraries = PassionPhoneGuard.dialerManifest(withLibraries, true);
        need(Arrays.equals(patchedWithLibraries, ManifestLibraries.add(patched, libraries)));
        need(
                patchedWithLibraries[9051] == 0x12
                        && ByteBuffer.wrap(patchedWithLibraries)
                                        .order(ByteOrder.LITTLE_ENDIAN)
                                        .getInt(9052)
                                == 0);
        rejects(() -> PassionPhoneGuard.dialerManifest(original, true));
        rejects(() -> PassionPhoneGuard.dialerManifest(withLibraries, false));
        rejects(() -> PassionPhoneGuard.dialerManifest(patchedWithLibraries, true));
    }

    static void testPanelHook(Method patched, Method original, List<Instruction> code, int offset) {
        int index = 0;
        int pc = 0;

        while (pc < offset) {
            pc += code.get(index++).getCodeUnits();
        }

        need(pc == offset);
        Instruction invoke = code.get(index);
        need(
                invoke.getOpcode() == Opcode.INVOKE_STATIC
                        && ((ReferenceInstruction) invoke)
                                .getReference()
                                .toString()
                                .equals(
                                        PassionPhoneGuard.HELPER
                                                + "->showSyntheticPanel(Ljava/lang/Object;)Z"));
        need(
                ((FiveRegisterInstruction) invoke).getRegisterC()
                        == original.getImplementation().getRegisterCount()
                                - original.getParameters().size()
                                - 1);
        need(code.get(index + 1).getOpcode() == Opcode.MOVE_RESULT);
        need(
                code.get(index + 2).getOpcode() == Opcode.IF_EQZ
                        && ((OffsetInstruction) code.get(index + 2)).getCodeOffset() == 3);
        need(code.get(index + 3).getOpcode() == Opcode.RETURN_VOID);
        List<Opcode> expected = new ArrayList<>();
        original.getImplementation().getInstructions().forEach(i -> expected.add(i.getOpcode()));
        List<Opcode> actual = new ArrayList<>();

        for (int i = 0; i < code.size(); ++i) {
            if (i < index || i >= index + 4) {
                actual.add(code.get(i).getOpcode());
            }
        }

        need(expected.equals(actual));

        if (patched.getName().equals("onResume")) {
            need(code.get(0).getOpcode() == Opcode.INVOKE_SUPER);
        }
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        String profile = TestProfiles.SYNTHETIC;
        var entries = NativeCore.entries(apk);

        testDialerManifest(entries.get("AndroidManifest.xml"));
        byte[] raw = entries.get("classes2.dex");
        byte[] out = PassionPhoneGuard.apply(profile, 30, apk, raw);
        var original = new DexBackedDexFile(Opcodes.forApi(30), raw);
        var methods = new HashMap<String, String>();
        var classes = new HashMap<String, String>();
        var originalMethods = new HashMap<String, Method>();

        for (var c : original.getClasses()) {
            classes.put(c.getType(), FrameworkDexMaterializer.semantic(c));

            for (var m : c.getMethods()) {
                methods.put(m.toString(), FrameworkDexMaterializer.semantic(m));
                originalMethods.put(m.toString(), m);
            }
        }

        var dex = new DexBackedDexFile(Opcodes.forApi(30), out);
        int calls = 0, lifecycle = 0, gates = 0, outgoing = 0, panels = 0;
        need(outgoingReferences(original) == 7 && outgoingReferences(dex) == 0);

        for (var c : dex.getClasses()) {
            // Includes the OEM BluetoothStateManager and SDK manager: their real status methods are
            // untouched.
            if (!PassionPhoneGuard.CLASSES.contains(c.getType())) {
                need(classes.get(c.getType()).equals(FrameworkDexMaterializer.semantic(c)));
            }

            for (var m : c.getMethods()) {
                if (PassionPhoneGuard.target(m)) {
                    List<Instruction> code = new ArrayList<Instruction>();
                    m.getImplementation().getInstructions().forEach(code::add);

                    if (PassionPhoneGuard.PANEL_HOOKS.containsKey(m.toString())) {
                        testPanelHook(
                                m,
                                originalMethods.get(m.toString()),
                                code,
                                PassionPhoneGuard.PANEL_HOOKS.get(m.toString()));
                        panels++;
                    } else if (PassionPhoneGuard.GATES.containsKey(m.toString())
                            || PassionPhoneGuard.OUTGOING.containsKey(m.toString())) {
                        int[] counts = testGateMethod(m, originalMethods.get(m.toString()), code);
                        gates += counts[0];
                        outgoing += counts[1];
                    } else if (m.getName().equals("placeCall")) {
                        testPlaceCall(m, code);
                        calls++;
                    } else {
                        testLifecycleHook(m, code);
                        lifecycle++;
                    }
                } else {
                    need(methods.get(m.toString()).equals(FrameworkDexMaterializer.semantic(m)));
                }
            }
        }

        need(calls == 2 && lifecycle == 2 && gates == 31 && outgoing == 6 && panels == 3);
        rejects(() -> PassionPhoneGuard.apply(profile, 29, apk, raw));
        rejects(() -> PassionPhoneGuard.apply(profile, 30, apk, out));
        System.out.println(
                "PASS Passion phone bare DIAL manifest and inverse, 31 local UI gates, all seven"
                        + " direct outgoing paths closed with exact URI operands, unchanged global"
                        + " HFP/PBAP methods, lifecycle, DEX inverse and foreign-input negatives;"
                        + " runtime pending");
    }

    /** Returns the changed gate count and the redirected outgoing call count. */
    static int[] testGateMethod(Method m, Method original, List<Instruction> code) {
        int pc = 0, changed = 0, redirected = 0;
        var gateOffsets = PassionPhoneGuard.GATES.getOrDefault(m.toString(), List.of());

        for (var item : code) {
            if (gateOffsets.contains(pc)) {
                need(
                        item.getOpcode() == Opcode.INVOKE_STATIC
                                && ((FiveRegisterInstruction) item).getRegisterCount() == 0);
                String ref = ((ReferenceInstruction) item).getReference().toString();
                need(
                        ref.equals(PassionPhoneGuard.HELPER + "->syntheticAvailable()Z")
                                || ref.equals(
                                        PassionPhoneGuard.HELPER
                                                + "->syntheticContactSyncState()I"));
                changed++;
            } else if (Integer.valueOf(pc).equals(PassionPhoneGuard.OUTGOING.get(m.toString()))) {
                need(
                        item.getOpcode() == Opcode.INVOKE_STATIC
                                && ((FiveRegisterInstruction) item).getRegisterCount() == 1);
                need(
                        ((ReferenceInstruction) item)
                                .getReference()
                                .toString()
                                .equals(
                                        PassionPhoneGuard.HELPER
                                                + "->placeCall(Landroid/net/Uri;)V"));
                need(((FiveRegisterInstruction) item).getRegisterC() == uriOperand(original, pc));
                redirected++;
            }

            pc += item.getCodeUnits();
        }

        need(
                changed == gateOffsets.size()
                        && redirected
                                == (PassionPhoneGuard.OUTGOING.containsKey(m.toString()) ? 1 : 0));

        return new int[] {changed, redirected};
    }

    static void testPlaceCall(Method m, List<Instruction> code) {
        need(
                code.size() == 2
                        && code.get(0).getOpcode() == Opcode.INVOKE_STATIC
                        && code.get(1).getOpcode() == Opcode.RETURN_VOID);
        need(
                ((ReferenceInstruction) code.get(0))
                        .getReference()
                        .toString()
                        .equals(PassionPhoneGuard.HELPER + "->placeCall(Ljava/lang/String;)V"));
        need(
                ((FiveRegisterInstruction) code.get(0)).getRegisterCount() == 1
                        && ((FiveRegisterInstruction) code.get(0)).getRegisterC()
                                == m.getImplementation().getRegisterCount() - 1);
    }

    static void testLifecycleHook(Method m, List<Instruction> code) {
        Instruction entry =
                m.getName().equals("onCreate") ? code.get(code.size() - 2) : code.get(0);

        need(entry.getOpcode() == Opcode.INVOKE_STATIC);
        need(
                ((ReferenceInstruction) entry)
                        .getReference()
                        .toString()
                        .equals(
                                PassionPhoneGuard.HELPER
                                        + (m.getName().equals("onCreate")
                                                ? "->start(Landroid/content/Context;)V"
                                                : "->stop()V")));
    }
}
