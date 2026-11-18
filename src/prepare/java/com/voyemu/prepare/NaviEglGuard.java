package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.instruction.DexBackedInstruction;
import org.jf.dexlib2.iface.instruction.formats.ArrayPayload;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.List;
import java.util.zip.Adler32;

// The emulated GPU has no MSAA4 EGL configs. Keep the OEM renderer and color/depth selection.
final class NaviEglGuard {
    static final String NAME = "api30-navi-egl",
            PIN = NaviVersionGuard.PIN,
            PROFILE = NaviVersionGuard.PROFILE;
    static final List<Integer> CONFIG =
            List.of(0x3024, 4, 0x3023, 4, 0x3022, 4, 0x3040, 4, 0x3032, 1, 0x3031, 4, 0x3038);

    static void need(boolean v) {
        Failure.require(
                v,
                "NAVI_EGL_CONTRACT",
                "Exact Free A11 7.3.1 Navi EGL sample configuration required",
                3);
    }

    static int offset(byte[] source) {
        var dex = new DexBackedDexFile(Opcodes.forApi(30), source);
        int count = 0, result = -1;

        for (var c : dex.getClasses()) {
            if (c.getType().equals("Lcom/baidu/mapautosdk/component/map/h$c;")) {
                for (var m : c.getMethods()) {
                    if (m.getName().equals("<init>")
                            && m.getParameterTypes().toString().equals("[I, I, I, I, I, I]")) {
                        for (var i : m.getImplementation().getInstructions()) {
                            if (i instanceof ArrayPayload a
                                    && a.getElementWidth() == 4
                                    && a.getArrayElements().stream()
                                            .map(Number::intValue)
                                            .toList()
                                            .equals(CONFIG)) {
                                result =
                                        dex.getDataBuffer().getBaseOffset()
                                                + ((DexBackedInstruction) i).instructionStart
                                                + 8;
                                count++;
                            }
                        }
                    }
                }
            }
        }

        need(count == 1 && result >= 0);

        return result;
    }

    static void validate(String profile, int api, String abi, Path apk, byte[] source)
            throws Exception {
        need(
                PROFILE.equals(profile)
                        && api == 30
                        && abi.equals("arm64-v8a")
                        && FilesSupport.hash(apk).equals(PIN)
                        && source != null);
        offset(source);
    }

    static byte[] apply(String profile, int api, String abi, Path apk, byte[] source)
            throws Exception {
        validate(profile, api, abi, apk, source);
        int at = offset(source);
        byte[] out = source.clone();
        var b = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(at + 9 * 4, 0);
        b.putInt(at + 11 * 4, 0);
        var sha = MessageDigest.getInstance("SHA-1");
        sha.update(out, 32, out.length - 32);
        System.arraycopy(sha.digest(), 0, out, 12, 20);
        var adler = new Adler32();
        adler.update(out, 12, out.length - 12);
        b.putInt(8, (int) adler.getValue());
        ViewDumpPatch.checkedDex(out, NativeCore.hash(out));

        return out;
    }
}
