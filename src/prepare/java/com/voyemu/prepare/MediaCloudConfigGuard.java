package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

final class MediaCloudConfigGuard {
    record Variant(String pin) {}

    static final Map<String, Variant> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    new Variant("c6f96ac40525aac9600731f7494cc3d23cb969c4a88ff2815526abe7da2e6d2e"),
                    "free-11-chinese-20260731-7.3.1",
                    new Variant(
                            "533a0b0dc5ccbe91a42847fac20c5735bd8d78a28bbb980a6bf365dd5ed0fba0"));

    static List<String> pins(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.of(variant.pin());
    }

    static final String NAME = "api30-media-cloud-availability";
    static final String OWNER = "Lcom/qinggan/media/helper/impl/AsgardMediaConfigImpl;",
            ENUM =
                    "Lcom/qinggan/media/helper/MediaEnum;->RADIO_YUNTING:Lcom/qinggan/media/helper/MediaEnum;";
    static final Set<String> BODY = Set.of("getControlMedias", "getSupportMedias");

    static void require(boolean b) {
        Failure.require(
                b,
                "MEDIA_CLOUD_CONTRACT",
                "Exact unavailable ARM32 CloudFM configuration contract required",
                3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && BODY.contains(m.getName())
                && m.getParameterTypes().isEmpty()
                && m.getReturnType().equals("Ljava/util/Set;");
    }

    static Method rewrite(String profile, Method m, boolean inverse) throws Exception {
        require(m.getImplementation() != null && m.getImplementation().getTryBlocks().isEmpty());

        var b = new MutableMethodImplementation(m.getImplementation());
        int address = 0, field = -1, call = -1;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            var instruction = b.getInstructions().get(i);

            if (address == 72) {
                require(
                        instruction.getOpcode() == Opcode.SGET_OBJECT
                                && instruction instanceof ReferenceInstruction r
                                && r.getReference().toString().equals(ENUM)
                                && instruction instanceof OneRegisterInstruction rr
                                && rr.getRegisterA() == 1);
                field = i;
            }

            if (address == 74) {
                call = i;
            }

            address += instruction.getCodeUnits();
        }

        require(field >= 0 && call == field + 1);
        var old = b.getInstructions().get(call);
        require(
                old.getOpcode() == Opcode.INVOKE_INTERFACE
                        && old instanceof FiveRegisterInstruction
                        && old instanceof ReferenceInstruction);
        var regs = (FiveRegisterInstruction) old;
        require(
                regs.getRegisterCount() == 2
                        && regs.getRegisterC() == 0
                        && regs.getRegisterD() == 1);
        require(
                ((ReferenceInstruction) old)
                        .getReference()
                        .toString()
                        .equals(
                                "Ljava/util/Set;->"
                                        + (inverse ? "remove" : "add")
                                        + "(Ljava/lang/Object;)Z"));
        b.replaceInstruction(
                call,
                new BuilderInstruction35c(
                        Opcode.INVOKE_INTERFACE,
                        2,
                        0,
                        1,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Ljava/util/Set;",
                                inverse ? "add" : "remove",
                                List.of("Ljava/lang/Object;"),
                                "Z")));

        return PassionServiceForegroundGuard.copy(m, b);
    }

    static Map<String, ClassDef> transform(
            String profile, Map<String, ClassDef> input, boolean inverse) throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int count = 0;

        for (var c : input.values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int gi = 0;

            for (var group : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var dest = gi++ == 0 ? direct : virtual;

                for (var m : group) {
                    Method n = m;

                    if (target(m)) {
                        n = rewrite(profile, m, inverse);

                        count++;
                    }

                    dest.add(n);
                }
            }

            out.put(
                    c.getType(),
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            direct,
                            virtual));
        }

        require(count == 2);

        return out;
    }

    static void validate(String profile, int api, String abi, Path original, byte[] dex)
            throws Exception {
        require(
                VARIANTS.containsKey(profile)
                        && api == 30
                        && abi.equals("arm64-v8a")
                        && VARIANTS.get(profile).pin().equals(FilesSupport.hash(original))
                        && dex != null);
        transform(profile, PassionServiceForegroundGuard.classes(dex), false);
    }

    static byte[] binary(byte[] source, boolean inverse) throws Exception {
        var dex = new org.jf.dexlib2.dexbacked.DexBackedDexFile(Opcodes.forApi(30), source);
        String wanted =
                "Ljava/util/Set;->" + (inverse ? "add" : "remove") + "(Ljava/lang/Object;)Z";
        int reference = -1;

        for (int i = 0; i < dex.getMethodSection().size(); i++) {
            if (dex.getMethodSection().get(i).toString().equals(wanted)) {
                require(reference < 0);
                reference = i;
            }
        }

        require(reference >= 0 && reference <= 65535);
        byte[] out = source.clone();
        int count = 0;

        for (var c : dex.getClasses()) {
            for (var m : c.getMethods()) {
                if (target(m)) {
                    int address = 0;

                    for (var instruction : m.getImplementation().getInstructions()) {
                        if (address == 74) {
                            require(
                                    instruction
                                                    instanceof
                                                    org.jf.dexlib2.dexbacked.instruction
                                                            .DexBackedInstruction
                                            && instruction
                                                    instanceof
                                                    ReferenceInstruction referenceInstruction
                                            && referenceInstruction
                                                    .getReference()
                                                    .toString()
                                                    .equals(
                                                            "Ljava/util/Set;->"
                                                                    + (inverse ? "remove" : "add")
                                                                    + "(Ljava/lang/Object;)Z"));
                            int at =
                                    dex.getDataBuffer().getBaseOffset()
                                            + ((org.jf.dexlib2.dexbacked.instruction
                                                                    .DexBackedInstruction)
                                                            instruction)
                                                    .instructionStart
                                            + 2;
                            out[at] = (byte) reference;
                            out[at + 1] = (byte) (reference >>> 8);
                            count++;
                        }

                        address += instruction.getCodeUnits();
                    }
                }
            }
        }

        require(count == 2);
        var sha = java.security.MessageDigest.getInstance("SHA-1");
        sha.update(out, 32, out.length - 32);
        System.arraycopy(sha.digest(), 0, out, 12, 20);
        var adler = new java.util.zip.Adler32();
        adler.update(out, 12, out.length - 12);
        java.nio.ByteBuffer.wrap(out)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(8, (int) adler.getValue());

        return out;
    }

    static byte[] apply(String profile, int api, String abi, Path original, byte[] dex)
            throws Exception {
        validate(profile, api, abi, original, dex);
        var before = PassionServiceForegroundGuard.classes(dex);
        var expected = transform(profile, before, false);
        byte[] output = binary(dex, false);
        var actual = PassionServiceForegroundGuard.classes(output);
        require(actual.keySet().equals(before.keySet()));

        for (var n : expected.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(expected.get(n))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(n))));
        }

        require(java.util.Arrays.equals(dex, binary(output, true)));
        ViewDumpPatch.checkedDex(output, NativeCore.hash(output));

        return output;
    }
}
