package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.instruction.DexBackedInstruction;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.zip.Adler32;

// Android 11 rejects the OEM's optional custom Settings.System version key.
final class NaviVersionGuard {
    static final String NAME = "api30-navi-version",
            PIN = "dceddff70db38bacb50ebffa46d36da76f70e5596c6184bca51c48c3ed562a05";
    static final String OWNER = "Lcom/baidu/navisdk/integration/FlavorCustomIntegrationManager;";

    static void require(boolean value) {
        Failure.require(
                value,
                "NAVI_VERSION_CONTRACT",
                "Exact Free A11 7.3.1 optional Navi version metadata contract required",
                3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("setMapVersionToSystem")
                && m.getParameterTypes().size() == 1
                && m.getParameterTypes().get(0).toString().equals("Landroid/content/Context;")
                && m.getReturnType().equals("V");
    }

    static void validate(String profile, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        require(
                api == 30
                        && abi.equals("arm64-v8a")
                        && PIN.equals(FilesSupport.hash(apk))
                        && dex != null);
        binary(dex, false);
    }

    static byte[] binary(byte[] source, boolean inverse) throws Exception {
        var dex = new DexBackedDexFile(Opcodes.forApi(30), source);
        byte[] out = source.clone();
        int count = 0;

        for (var c : dex.getClasses()) {
            for (var m : c.getMethods()) {
                if (target(m)) {
                    int at = channelBranch(dex, m);
                    require(
                            out[at] == (byte) (inverse ? 0x12 : 0x0a)
                                    && out[at + 1] == (byte) (inverse ? 0x10 : 0x00));
                    out[at] = (byte) (inverse ? 0x0a : 0x12);
                    out[at + 1] = (byte) (inverse ? 0x00 : 0x10);
                    count++;
                }
            }
        }

        require(count == 1);
        var sha = MessageDigest.getInstance("SHA-1");
        sha.update(out, 32, out.length - 32);
        System.arraycopy(sha.digest(), 0, out, 12, 20);
        var adler = new Adler32();
        adler.update(out, 12, out.length - 12);
        ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN).putInt(8, (int) adler.getValue());

        return out;
    }

    // Returns the raw offset of the channel branch after checking the version write it guards.
    static int channelBranch(DexBackedDexFile dex, Method m) {
        int address = 0, at = -1;
        boolean channel = false, branch = false, write = false, key = false;

        for (var ins : m.getImplementation().getInstructions()) {
            if (address == 0) {
                channel =
                        ins instanceof ReferenceInstruction r
                                && r.getReference()
                                        .toString()
                                        .equals(
                                                "Lcom/baidu/naviauto/util/ChannelUtil;->isH97CPadChannel()Z");
            }

            if (address == 3) {
                require(ins instanceof DexBackedInstruction);
                at =
                        dex.getDataBuffer().getBaseOffset()
                                + ((DexBackedInstruction) ins).instructionStart;
            }

            if (address == 4) {
                branch = ins.getOpcode() == org.jf.dexlib2.Opcode.IF_EQZ;
            }

            if (ins instanceof ReferenceInstruction r) {
                String ref = r.getReference().toString();

                if (r.getReference() instanceof org.jf.dexlib2.iface.reference.StringReference sr
                        && sr.getString().equals("soft_version")) {
                    key = true;
                }

                if (ref.equals(
                        "Landroid/provider/Settings$System;->putString(Landroid/content/ContentResolver;Ljava/lang/String;Ljava/lang/String;)Z")) {
                    write = true;
                }
            }

            address += ins.getCodeUnits();
        }

        require(channel && branch && key && write && address == 65 && at >= 0);

        return at;
    }

    static byte[] apply(String profile, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        validate(profile, api, abi, apk, dex);
        byte[] out = binary(dex, false);
        require(Arrays.equals(dex, binary(out, true)));
        ViewDumpPatch.checkedDex(out, NativeCore.hash(out));

        return out;
    }
}
