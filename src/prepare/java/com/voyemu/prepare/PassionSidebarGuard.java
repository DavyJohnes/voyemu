package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.RegisterRangeInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.instruction.VariableRegisterInstruction;
import org.jf.dexlib2.iface.instruction.WideLiteralInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21s;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/** Keep the two OEM sidebars outside AOSP's reserved bottom-navigation-bar geometry. */
final class PassionSidebarGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            NAME = "passion-api30-sidebars";
    static final String PIN = "4032f14c0fc49fd52514737309d2af67c9d4ff9340dd534874f46cbe2e748c00";
    static final String OWNER = "Lcom/voyah/os/carsystemui/navigation/NavigationBarManager;";
    static final String ROUTER = "Lcom/voyemu/output/PassionSidebarWindow;";

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_SIDEBAR_CONTRACT",
                "Exact owner-pinned Passion two-sidebar layout required",
                3);
    }

    static Method method(DexFile dex) {
        Method selected = null;

        for (var type : dex.getClasses()) {
            if (type.getType().equals(OWNER)) {
                for (Method candidate : type.getDirectMethods()) {
                    if (candidate.getName().equals("initLayoutParams")) {
                        require(
                                selected == null
                                        && candidate.getParameterTypes().isEmpty()
                                        && candidate.getReturnType().equals("V")
                                        && candidate.getAccessFlags() == 2);
                        selected = candidate;
                    }
                }
            }
        }

        require(selected != null && selected.getImplementation() != null);

        return selected;
    }

    static void validateMethod(Method method) {
        var body = method.getImplementation();

        require(body != null && body.getRegisterCount() == 11 && body.getTryBlocks().isEmpty());
        int address = 0, sites = 0, constructors = 0, literals = 0;

        for (var instruction : body.getInstructions()) {
            if (instruction instanceof WideLiteralInstruction value
                    && value.getWideLiteral() == 2019) {
                literals++;
            }

            if (address == 39 || address == 128) {
                requireLayoutType(instruction, address);
                sites++;
            }

            if (address == 46 || address == 135) {
                requireLayoutConstructor(instruction, address);
                constructors++;
            }

            address += instruction.getCodeUnits();
        }

        require(address == 176 && sites == 2 && constructors == 2 && literals == 2);
    }

    private static void requireLayoutType(Instruction instruction, int address) {
        require(
                instruction.getOpcode() == Opcode.CONST_16
                        && instruction instanceof NarrowLiteralInstruction narrowLiteralInstruction
                        && narrowLiteralInstruction.getNarrowLiteral() == 2019
                        && ((OneRegisterInstruction) instruction).getRegisterA()
                                == (address == 39 ? 6 : 7));
    }

    private static void requireLayoutConstructor(Instruction instruction, int address) {
        require(
                instruction.getOpcode() == Opcode.INVOKE_DIRECT_RANGE
                        && instruction instanceof ReferenceInstruction referenceInstruction
                        && referenceInstruction
                                .getReference()
                                .toString()
                                .equals("Landroid/view/WindowManager$LayoutParams;-><init>(IIIII)V")
                        && ((RegisterRangeInstruction) instruction).getStartRegister()
                                == (address == 46 ? 3 : 4)
                        && ((VariableRegisterInstruction) instruction).getRegisterCount() == 6);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(original).equals(PIN)
                        && dex != null);
        validateMethod(method(new DexBackedDexFile(Opcodes.forApi(30), dex)));
    }

    private static ImmutableDexFile rewrite(DexFile source, boolean inverse) {
        var classes = new TreeMap<String, ClassDef>();

        for (var type : source.getClasses()) {
            if (!type.getType().equals(OWNER)) {
                classes.put(type.getType(), type);
                continue;
            }

            var methods = new ArrayList<Method>();

            for (Method candidate : type.getDirectMethods()) {
                if (candidate.getName().equals("initLayoutParams")
                        && candidate.getParameterTypes().isEmpty()
                        && candidate.getReturnType().equals("V")) {
                    candidate = routedLayout(candidate, inverse);
                }

                methods.add(candidate);
            }

            classes.put(
                    type.getType(),
                    new ImmutableClassDef(
                            type.getType(),
                            type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            methods,
                            type.getVirtualMethods()));
        }

        return new ImmutableDexFile(Opcodes.forApi(30), classes.values());
    }

    private static Method routedLayout(Method candidate, boolean inverse) {
        var body = candidate.getImplementation();
        var instructions = new ArrayList<Instruction>();
        int address = 0, changed = 0;
        int configured = 0;

        for (var instruction : body.getInstructions()) {
            if (inverse
                    && instruction instanceof ReferenceInstruction ref
                    && ref.getReference()
                            .toString()
                            .equals(
                                    ROUTER
                                            + "->configure(Landroid/view/WindowManager$LayoutParams;)V")) {
                requireConfigureCall(instruction, address);
                configured++;
                continue;
            }

            int units = instruction.getCodeUnits();

            if (address == 39 || address == 128) {
                require(
                        instruction.getOpcode() == Opcode.CONST_16
                                && ((NarrowLiteralInstruction) instruction).getNarrowLiteral()
                                        == (inverse ? 2038 : 2019));
                instructions.add(
                        new ImmutableInstruction21s(
                                Opcode.CONST_16,
                                ((OneRegisterInstruction) instruction).getRegisterA(),
                                inverse ? 2019 : 2038));
                changed++;
            } else {
                instructions.add(instruction);
            }

            if (!inverse && (address == 86 || address == 173)) {
                requirePackageName(instruction, address);
                instructions.add(
                        new ImmutableInstruction35c(
                                Opcode.INVOKE_STATIC,
                                1,
                                0,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        ROUTER,
                                        "configure",
                                        List.of("Landroid/view/WindowManager$LayoutParams;"),
                                        "V")));
                configured++;
            }

            address += units;
        }

        require(changed == 2 && configured == 2 && address == 176);

        return new ImmutableMethod(
                candidate.getDefiningClass(),
                candidate.getName(),
                candidate.getParameters(),
                candidate.getReturnType(),
                candidate.getAccessFlags(),
                candidate.getAnnotations(),
                candidate.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        instructions,
                        body.getTryBlocks(),
                        body.getDebugItems()));
    }

    private static void requireConfigureCall(Instruction instruction, int address) {
        require(
                (address == 88 || address == 175)
                        && instruction.getOpcode() == Opcode.INVOKE_STATIC
                        && instruction instanceof FiveRegisterInstruction fiveRegisterInstruction
                        && fiveRegisterInstruction.getRegisterCount() == 1
                        && fiveRegisterInstruction.getRegisterC() == 0);
    }

    private static void requirePackageName(Instruction instruction, int address) {
        require(
                instruction.getOpcode() == Opcode.IPUT_OBJECT
                        && instruction instanceof TwoRegisterInstruction twoRegisterInstruction
                        && twoRegisterInstruction.getRegisterA() == (address == 86 ? 4 : 10)
                        && twoRegisterInstruction.getRegisterB() == 0
                        && instruction instanceof ReferenceInstruction referenceInstruction
                        && referenceInstruction
                                .getReference()
                                .toString()
                                .equals(
                                        "Landroid/view/WindowManager$LayoutParams;->packageName:Ljava/lang/String;"));
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var source = new DexBackedDexFile(Opcodes.forApi(30), dex);
        byte[] output = FrameworkDexMaterializer.serialize(rewrite(source, false));
        var reread = new DexBackedDexFile(Opcodes.forApi(30), output);
        var normalized = rewrite(reread, true);
        var byName = new TreeMap<String, ClassDef>();

        for (var type : normalized.getClasses()) {
            byName.put(type.getType(), type);
        }

        require(byName.size() == source.getClasses().size());

        for (var type : source.getClasses()) {
            require(
                    byName.containsKey(type.getType())
                            && FrameworkDexMaterializer.semantic(type)
                                    .equals(
                                            FrameworkDexMaterializer.semantic(
                                                    byName.get(type.getType()))));
        }

        return output;
    }
}
