package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.instruction.DexBackedInstruction;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.zip.Adler32;

// The profile is H97C (NVRAM 43=134). Navi's separate UID cannot read system-private NVRAM.
// Select the same OEM H97C provider as the OEM's explicit H97C-pad branch; retain all controllers.
final class NaviVehicleGuard {
    static final String NAME = "api30-navi-vehicle", PIN = NaviVersionGuard.PIN;

    static void need(boolean value) {
        Failure.require(
                value,
                "NAVI_VEHICLE_CONTRACT",
                "Exact Free A11 7.3.1 H97C Navi factory boundary required",
                3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals("Lcom/baidu/naviauto/NaviAutoApplication;")
                && m.getName().equals("attachBaseContext")
                && m.getParameterTypes().toString().equals("[Landroid/content/Context;]")
                && m.getReturnType().equals("V");
    }

    static int offset(byte[] source) {
        var dex = new DexBackedDexFile(Opcodes.forApi(30), source);
        int result = -1, count = 0;

        for (var c : dex.getClasses()) {
            for (var m : c.getMethods()) {
                if (target(m)) {
                    var list = new ArrayList<Instruction>();
                    m.getImplementation().getInstructions().forEach(list::add);

                    for (int i = 0; i + 2 < list.size(); i++) {
                        var x = list.get(i);

                        if (x instanceof ReferenceInstruction ref
                                && ref.getReference()
                                        .toString()
                                        .equals(
                                                "Lcom/qinggan/vehicle/VehicleConfigHelper;->getVehicleTypeID()I")) {
                            need(
                                    x.getOpcode() == Opcode.INVOKE_STATIC
                                            && ((FiveRegisterInstruction) x).getRegisterCount() == 0
                                            && list.get(i + 1).getOpcode() == Opcode.MOVE_RESULT
                                            && ((OneRegisterInstruction) list.get(i + 1))
                                                            .getRegisterA()
                                                    == 0
                                            && list.get(i + 2) instanceof ReferenceInstruction call
                                            && call.getReference()
                                                    .toString()
                                                    .equals(
                                                            "Lcom/baidu/naviauto/config/VoyahCarFactory;->createCar(I)V"));
                            result =
                                    dex.getDataBuffer().getBaseOffset()
                                            + ((DexBackedInstruction) x).instructionStart;
                            count++;
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
                api == 30
                        && abi.equals("arm64-v8a")
                        && FilesSupport.hash(apk).equals(PIN)
                        && source != null);
        offset(source);
    }

    static byte[] apply(String profile, int api, String abi, Path apk, byte[] source)
            throws Exception {
        validate(profile, api, abi, apk, source);
        int at = offset(source);
        byte[] out = source.clone(), replacement = {0x13, 0, (byte) 134, 0, 0, 0, 0, 0};
        System.arraycopy(replacement, 0, out, at, 8);
        var sha = MessageDigest.getInstance("SHA-1");
        sha.update(out, 32, out.length - 32);
        System.arraycopy(sha.digest(), 0, out, 12, 20);
        var adler = new Adler32();
        adler.update(out, 12, out.length - 12);
        ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN).putInt(8, (int) adler.getValue());
        ViewDumpPatch.checkedDex(out, NativeCore.hash(out));

        return out;
    }
}
