package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.zip.ZipFile;

public final class Api30SystemUiRoutingTest {
    /** The pin of the original SystemUI whose call-site addresses this test checks. */
    static final String PIN = "b676a3a426ba3b59fabb9d98355ff0e0ac7b07d7ad6545ac44d9cbf85f1700c1";

    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static void verify(byte[] source, byte[] patched) {
        var before = SystemUiRouting.methods(source);
        var after = SystemUiRouting.methods(patched);
        int replacements = 0;

        for (var m : before) {
            var changed =
                    after.stream()
                            .filter(x -> x.getName().equals(m.getName()))
                            .findFirst()
                            .orElseThrow();
            var a = new Api30NetworkNullGuardTest.Body(m);
            var b = new Api30NetworkNullGuardTest.Body(changed);
            check(
                    a.instructions().size() == b.instructions().size(),
                    "No added instructions or branches");
            Map<Integer, Integer> identity = new HashMap<>();

            for (int i = 0; i < a.instructions().size(); i++) {
                identity.put(i, i);
            }

            for (int i = 0; i < a.instructions().size(); i++) {
                int offset = a.addresses().get(i);
                var x = a.instructions().get(i);
                var y = b.instructions().get(i);

                if (offset == 0xf || offset == 0x1e) {
                    check(
                            y.getOpcode() == Opcode.INVOKE_STATIC
                                    && ((ReferenceInstruction) y)
                                            .getReference()
                                            .toString()
                                            .startsWith(SystemUiRouting.ROUTER + "->"),
                            "Exact four status-bar lifecycle calls routed");
                    var xr = (FiveRegisterInstruction) x;
                    var yr = (FiveRegisterInstruction) y;
                    check(
                            xr.getRegisterCount() == yr.getRegisterCount()
                                    && xr.getRegisterC() == yr.getRegisterC()
                                    && xr.getRegisterD() == yr.getRegisterD()
                                    && xr.getRegisterE() == yr.getRegisterE(),
                            "Caller manager/view/params registers retained");
                    check(x.getCodeUnits() == y.getCodeUnits(), "No branch-address changes");
                    replacements++;
                } else {
                    check(
                            Api30NetworkNullGuardTest.signature(a, i, identity)
                                    .equals(Api30NetworkNullGuardTest.signature(b, i, identity)),
                            "All OEM fields/flags/state/branches retained");
                }
            }
        }

        check(replacements == 4, "Only two add plus two remove calls");
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked c) throws Exception {
        try {
            c.run();
            throw new AssertionError("Wrong pinned boundary accepted");
        } catch (Failure e) {
            check(e.code.equals("SYSTEMUI_ROUTING"), "Exact contract error");
        }
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]);
        String id = TestProfiles.pinning(root, PIN);
        Path original = root.resolve(".local/profiles/" + id + "/originals/SystemUI.apk");
        Map<String, Object> constants = TestProfiles.constants(root, SystemUiRouting.NAME, PIN);
        byte[] dex;

        try (var z = new ZipFile(original.toFile())) {
            dex = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        if (a.length == 2 && a[1].equals("--original")) {
            verify(dex, dex);

            return;
        }

        var output = SystemUiRouting.apply(constants, 30, original, dex);
        verify(dex, output);
        reject(() -> SystemUiRouting.apply(constants, 28, original, dex));
        reject(() -> SystemUiRouting.apply(constants, 30, original, null));
        reject(() -> SystemUiRouting.apply(constants, 30, original, output));
        reject(() ->
                SystemUiRouting.apply(
                        constants,
                        30,
                        root.resolve(".local/profiles/" + id + "/originals/SystemService.apk"),
                        dex));

        if (a.length == 2) {
            try (var z = new ZipFile(a[1])) {
                verify(dex, z.getInputStream(z.getEntry("classes.dex")).readAllBytes());
            }
        }

        System.out.println(
                "four exact SystemUI lifecycle invokes routed; all OEM branches/registers retained;"
                        + " six negatives rejected");
    }
}
