package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * The OEM rear screen app starts its home with setLaunchDisplayId(3). Its one start call goes to
 * the RearScreen helper instead, which starts the same intent on the display of the emulator
 * multidisplay provider with the rear unique ID, whenever the provider adds it: display numbers
 * shift when the provider restarts. Its one touch listener registration, which the OEM framework
 * serves with IWindowManager.registerIOnTouchListener for display 3 and the API30 base lacks, goes
 * to RearScreen as well, which passes the touches of the rear display to the same listener.
 */
final class ThirdScreenRouting {
    static final String NAME = "api30-thirdscreen-rear", DEX = "classes2.dex";
    static final String HELPER = "Lcom/voyemu/output/RearScreen;";

    /**
     * One routed call: the method with its access flags and register count, the code unit of the
     * call, its OEM opcode and argument registers, the OEM callee and the static helper callee.
     */
    record Site(
            String method,
            int access,
            int registers,
            int address,
            Opcode original,
            List<Integer> arguments,
            ImmutableMethodReference from,
            ImmutableMethodReference to) {
        String owner() {
            return method.substring(0, method.indexOf("->"));
        }
    }

    // invoke-virtual {v3, v1, v0} at 0x1c: this, the intent and the options.
    static final Site START =
            new Site(
                    "Lcom/qinggan/app/thirdscreen/ThirdScreenApplication;->startActivity()V",
                    0x2,
                    4,
                    0x1c,
                    Opcode.INVOKE_VIRTUAL,
                    List.of(3, 1, 0),
                    new ImmutableMethodReference(
                            "Lcom/qinggan/app/thirdscreen/ThirdScreenApplication;",
                            "startActivity",
                            List.of("Landroid/content/Intent;", "Landroid/os/Bundle;"),
                            "V"),
                    new ImmutableMethodReference(
                            HELPER,
                            "startActivity",
                            List.of(
                                    "Landroid/content/Context;",
                                    "Landroid/content/Intent;",
                                    "Landroid/os/Bundle;"),
                            "V"));
    // invoke-static {v1, v2} at 0x2c: the listener and display 3.
    static final Site TOUCH =
            new Site(
                    "Lcom/qinggan/app/vehicle/Lock3ScreenManager;-><init>()V",
                    0x10002,
                    5,
                    0x2c,
                    Opcode.INVOKE_STATIC,
                    List.of(1, 2),
                    new ImmutableMethodReference(
                            "Lcom/qinggan/os/ServiceManager;",
                            "registerIOnTouchListener",
                            List.of("Landroid/app/IOnTouchListener;", "I"),
                            "V"),
                    new ImmutableMethodReference(
                            HELPER,
                            "registerTouchListener",
                            List.of("Landroid/app/IOnTouchListener;", "I"),
                            "V"));
    static final List<Site> SITES = List.of(START, TOUCH);

    /** Profile data: the original APK. */
    private ThirdScreenRouting() {}

    private static void require(boolean accepted) {
        Failure.require(
                accepted,
                "THIRDSCREEN_ROUTING",
                "Exact original ThirdScreen rear display start required",
                3);
    }

    static List<Integer> arguments(FiveRegisterInstruction r) {
        return Arrays.stream(
                        new int[] {
                            r.getRegisterC(),
                            r.getRegisterD(),
                            r.getRegisterE(),
                            r.getRegisterF(),
                            r.getRegisterG()
                        })
                .limit(r.getRegisterCount())
                .boxed()
                .toList();
    }

    // Forward replaces the OEM call with the helper call; backward restores it.
    static Method rewrite(Site site, Method m, boolean forward) {
        var code = new MutableMethodImplementation(m.getImplementation());
        int address = 0, replaced = 0;
        int[] r = new int[5];

        for (int i = 0; i < site.arguments().size(); i++) {
            r[i] = site.arguments().get(i);
        }

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var old = code.getInstructions().get(i);

            if (address == site.address()) {
                require(
                        old.getOpcode() == (forward ? site.original() : Opcode.INVOKE_STATIC)
                                && old instanceof FiveRegisterInstruction call
                                && old instanceof ReferenceInstruction target
                                && target.getReference()
                                        .toString()
                                        .equals((forward ? site.from() : site.to()).toString())
                                && arguments(call).equals(site.arguments()));
                code.replaceInstruction(
                        i,
                        new BuilderInstruction35c(
                                forward ? Opcode.INVOKE_STATIC : site.original(),
                                site.arguments().size(),
                                r[0],
                                r[1],
                                r[2],
                                r[3],
                                r[4],
                                forward ? site.to() : site.from()));
                replaced++;
            }

            address += old.getCodeUnits();
        }

        require(replaced == 1);

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                code);
    }

    static void validateMethod(Site site, Method m) throws Exception {
        require(
                m.toString().equals(site.method())
                        && m.getAccessFlags() == site.access()
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == site.registers()
                        && m.getImplementation().getTryBlocks().isEmpty());
        rewrite(site, m, true);
    }

    static void validate(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        require(
                api == 30
                        && StepConstants.admits(constants, FilesSupport.hash(original))
                        && dex != null);

        for (Site site : SITES) {
            int found = 0;

            for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
                for (var m : c.getMethods()) {
                    if (m.toString().equals(site.method())) {
                        validateMethod(site, m);
                        found++;
                    }
                }
            }

            require(found == 1);
        }
    }

    private static ClassDef rewriteClass(ClassDef c, boolean forward) {
        var site = SITES.stream().filter(s -> s.owner().equals(c.getType())).findFirst();

        if (site.isEmpty()) {
            return c;
        }

        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();

        for (var m : c.getDirectMethods()) {
            direct.add(
                    m.toString().equals(site.get().method()) ? rewrite(site.get(), m, forward) : m);
        }

        for (var m : c.getVirtualMethods()) {
            virtual.add(m);
        }

        return new ImmutableClassDef(
                c.getType(),
                c.getAccessFlags(),
                c.getSuperclass(),
                c.getInterfaces(),
                c.getSourceFile(),
                c.getAnnotations(),
                c.getStaticFields(),
                c.getInstanceFields(),
                direct,
                virtual);
    }

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        validate(constants, api, original, dex);
        var before = new TreeMap<String, ClassDef>();
        var changed = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            require(before.put(c.getType(), c) == null);
            changed.put(c.getType(), rewriteClass(c, true));
        }

        byte[] out =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), changed.values()));
        var read = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), out).getClasses()) {
            require(read.put(c.getType(), c) == null);
        }

        require(read.keySet().equals(before.keySet()));

        // Only the owner classes change; each serialized owner restores to the original.
        for (Site site : SITES) {
            String owner = site.owner();
            require(
                    FrameworkDexMaterializer.semantic(changed.get(owner))
                            .equals(FrameworkDexMaterializer.semantic(read.get(owner))));
            require(
                    FrameworkDexMaterializer.semantic(before.get(owner))
                            .equals(
                                    FrameworkDexMaterializer.semantic(
                                            rewriteClass(read.get(owner), false))));
        }

        return out;
    }
}
