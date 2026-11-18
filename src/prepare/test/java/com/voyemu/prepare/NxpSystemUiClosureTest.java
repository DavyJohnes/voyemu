package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.ExceptionHandler;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.TryBlock;
import org.jf.dexlib2.iface.instruction.DualReferenceInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.MethodProtoReference;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.reference.Reference;
import org.jf.dexlib2.iface.reference.TypeReference;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21c;

import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * Verifies the actual packaged OEM type graph, including the isolated unused widget resource
 * family.
 */
public final class NxpSystemUiClosureTest {
    private static final String MENU = "Lcom/qinggan/qgwigdets/MenuPanel";
    private static final String RESOURCE = "Lcom/qinggan/qgwigdets/R$anim;";
    private static final Set<String> FAMILY =
            Set.of(
                    MENU + ";",
                    MENU + "$1;",
                    MENU + "$2;",
                    MENU + "$3;",
                    MENU + "$4;",
                    MENU + "$5;",
                    MENU + "$5$1;",
                    MENU + "$5$2;",
                    MENU + "$Jumper;",
                    MENU + "$Jumper$SwitchAnimationListener;",
                    MENU + "$MenuIconsOnClickListener;");
    private static final Map<String, Integer> RESOURCE_FIELDS =
            Map.of(
                    "dock_bottom_enter",
                    2,
                    "dock_bottom_exit",
                    2,
                    "dock_top_enter",
                    1,
                    "dock_top_exit",
                    1,
                    "dock_left_enter",
                    1,
                    "dock_left_exit",
                    1,
                    "dock_right_enter",
                    1,
                    "dock_right_exit",
                    1,
                    "zoom_exit",
                    1);

    private static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static String descriptor(String type) {
        if (type == null) {
            return "";
        }

        while (type.startsWith("[")) {
            type = type.substring(1);
        }

        return type;
    }

    private static String signature(Method method) {
        return method.getName()
                + "("
                + String.join("", method.getParameterTypes())
                + ")"
                + method.getReturnType();
    }

    private static boolean contains(byte[] bytes, byte[] part) {
        outer:
        for (int i = 0; i <= bytes.length - part.length; i++) {
            for (int j = 0; j < part.length; j++) {
                if (bytes[i + j] != part[j]) {
                    continue outer;
                }
            }

            return true;
        }

        return false;
    }

    private static Map<String, ClassDef> originalClasses(Map<String, byte[]> entries) {
        var result = new TreeMap<String, ClassDef>();

        for (var entry : entries.entrySet()) {
            if (entry.getKey().matches("classes(?:[2-9][0-9]*)?\\.dex")) {
                for (ClassDef type :
                        new DexBackedDexFile(Opcodes.forApi(28), entry.getValue()).getClasses()) {
                    need(
                            result.put(type.getType(), type) == null,
                            "original duplicate class " + type.getType());
                }
            }
        }

        return result;
    }

    private static final class Graph {
        final Map<String, ClassDef> classes;
        final Set<String> missing = new TreeSet<>();
        final Map<String, String> resourceSites = new TreeMap<>();
        final Map<String, Integer> resourceFields = new TreeMap<>();

        Graph(Map<String, ClassDef> classes) {
            this.classes = classes;
        }

        void type(String caller, String target) {
            target = descriptor(target);

            if (FAMILY.contains(target)) {
                need(
                        FAMILY.contains(caller),
                        "external reference to isolated MenuPanel family: "
                                + caller
                                + " -> "
                                + target);
            }

            if (target.startsWith("Lcom/qinggan/") && !classes.containsKey(target)) {
                missing.add(target);
            }
        }

        void reference(String caller, Method method, int offset, Opcode opcode, Reference ref) {
            if (ref instanceof FieldReference field) {
                if (field.getDefiningClass().equals(RESOURCE)) {
                    String methodName = signature(method);
                    need(
                            opcode == Opcode.SGET && field.getType().equals("I"),
                            "unexpected resource opcode/type");
                    need(
                            (caller.equals(MENU + ";")
                                            && methodName.equals("initAnimations()V")
                                            && !field.getName().equals("zoom_exit"))
                                    || (caller.equals(MENU + "$Jumper;")
                                            && methodName.equals(
                                                    "<init>(Lcom/qinggan/qgwigdets/MenuPanel;II)V")
                                            && field.getName().equals("zoom_exit")),
                            "unexpected resource caller or method");
                    need(RESOURCE_FIELDS.containsKey(field.getName()), "unexpected resource field");
                    String site = caller + "->" + methodName + "@" + offset;
                    need(
                            resourceSites.put(site, field.getName()) == null,
                            "duplicate resource site");
                    resourceFields.merge(field.getName(), 1, Integer::sum);

                    return;
                }

                type(caller, field.getDefiningClass());
                type(caller, field.getType());
            } else if (ref instanceof TypeReference value) {
                type(caller, value.getType());
            } else if (ref instanceof MethodReference value) {
                type(caller, value.getDefiningClass());
                type(caller, value.getReturnType());

                for (CharSequence parameter : value.getParameterTypes()) {
                    type(caller, parameter.toString());
                }
            } else if (ref instanceof MethodProtoReference value) {
                type(caller, value.getReturnType());

                for (CharSequence parameter : value.getParameterTypes()) {
                    type(caller, parameter.toString());
                }
            }
        }

        void scan() {
            need(
                    !classes.containsKey(RESOURCE),
                    "resource exception no longer needed; review its removal");
            var actualFamily = new TreeSet<String>();

            for (String name : classes.keySet()) {
                if (name.startsWith(MENU + "$") || name.equals(MENU + ";")) {
                    actualFamily.add(name);
                }
            }

            need(actualFamily.equals(FAMILY), "isolated MenuPanel family inventory changed");

            for (ClassDef cls : classes.values()) {
                String caller = cls.getType();
                type(caller, cls.getSuperclass());

                for (String target : cls.getInterfaces()) {
                    type(caller, target);
                }

                for (Field field : cls.getFields()) {
                    type(caller, field.getType());
                }

                for (Method method : cls.getMethods()) {
                    type(caller, method.getReturnType());

                    for (CharSequence parameter : method.getParameterTypes()) {
                        type(caller, parameter.toString());
                    }

                    var impl = method.getImplementation();

                    if (impl == null) {
                        continue;
                    }

                    int offset = 0;

                    for (Instruction instruction : impl.getInstructions()) {
                        if (instruction instanceof ReferenceInstruction ref) {
                            reference(
                                    caller,
                                    method,
                                    offset,
                                    instruction.getOpcode(),
                                    ref.getReference());
                        }

                        if (instruction instanceof DualReferenceInstruction ref) {
                            reference(
                                    caller,
                                    method,
                                    offset,
                                    instruction.getOpcode(),
                                    ref.getReference2());
                        }

                        offset += instruction.getCodeUnits();
                    }

                    for (TryBlock<? extends ExceptionHandler> block : impl.getTryBlocks()) {
                        for (ExceptionHandler handler : block.getExceptionHandlers()) {
                            type(caller, handler.getExceptionType());
                        }
                    }
                }
            }

            need(
                    resourceSites.size() == 11 && resourceFields.equals(RESOURCE_FIELDS),
                    "exact 11 resource sites / 9 fields changed");
        }
    }

    static void verify(Map<String, ClassDef> packaged, Map<String, byte[]> originalEntries) {
        need(!packaged.isEmpty(), "empty packaged graph");
        var original = new Graph(originalClasses(originalEntries));
        original.scan();
        var built = new Graph(packaged);
        built.scan();
        need(
                built.resourceSites.equals(original.resourceSites),
                "resource exception instruction locations changed");

        for (var entry : originalEntries.entrySet()) {
            if (entry.getKey().endsWith(".xml")) {
                for (var charset :
                        List.of(
                                StandardCharsets.UTF_8,
                                StandardCharsets.UTF_16LE,
                                StandardCharsets.UTF_16BE)) {
                    need(
                            !contains(entry.getValue(), "MenuPanel".getBytes(charset)),
                            "MenuPanel XML instantiation: " + entry.getKey());
                }
            }
        }

        need(built.missing.isEmpty(), "unresolved packaged com.qinggan types: " + built.missing);
    }

    private static void reject(Runnable action, String reason) {
        boolean rejected = false;

        try {
            action.run();
        } catch (AssertionError expected) {
            rejected = true;
        }

        need(rejected, "closure control accepted " + reason);
    }

    private static ClassDef mutatedMenu(ClassDef original, boolean opcode) {
        var direct = new ArrayList<Method>();
        boolean changed = false;

        for (Method method : original.getDirectMethods()) {
            if (!method.getName().equals("initAnimations")) {
                direct.add(method);
                continue;
            }

            var impl = method.getImplementation();
            var instructions = new ArrayList<Instruction>();

            if (!opcode) {
                instructions.add(new ImmutableInstruction10x(Opcode.NOP));
            }

            for (Instruction instruction : impl.getInstructions()) {
                if (opcode
                        && !changed
                        && instruction instanceof ReferenceInstruction ref
                        && ref.getReference() instanceof FieldReference field
                        && field.getDefiningClass().equals(RESOURCE)) {
                    instructions.add(
                            new ImmutableInstruction21c(
                                    Opcode.SPUT,
                                    ((OneRegisterInstruction) instruction).getRegisterA(),
                                    field));
                    changed = true;
                } else {
                    instructions.add(instruction);
                }
            }

            if (!opcode) {
                changed = true;
            }

            direct.add(
                    new ImmutableMethod(
                            method.getDefiningClass(),
                            method.getName(),
                            method.getParameters(),
                            method.getReturnType(),
                            method.getAccessFlags(),
                            method.getAnnotations(),
                            method.getHiddenApiRestrictions(),
                            new ImmutableMethodImplementation(
                                    impl.getRegisterCount(),
                                    instructions,
                                    impl.getTryBlocks(),
                                    impl.getDebugItems())));
        }

        need(changed, "control did not mutate MenuPanel");

        return new ImmutableClassDef(
                original.getType(),
                original.getAccessFlags(),
                original.getSuperclass(),
                original.getInterfaces(),
                original.getSourceFile(),
                original.getAnnotations(),
                original.getStaticFields(),
                original.getInstanceFields(),
                direct,
                original.getVirtualMethods());
    }

    static void controls(Map<String, ClassDef> packaged, Map<String, byte[]> originalEntries) {
        verify(packaged, originalEntries);
        var missing = new TreeMap<>(packaged);
        need(
                missing.remove("Lcom/qinggan/bluetooth/profile/IBTCallback;") != null,
                "control callback missing before mutation");
        reject(() -> verify(missing, originalEntries), "missing IBTCallback");

        for (boolean opcode : List.of(false, true)) {
            var changed = new TreeMap<>(packaged);
            changed.put(MENU + ";", mutatedMenu(packaged.get(MENU + ";"), opcode));
            reject(
                    () -> verify(changed, originalEntries),
                    opcode ? "resource opcode" : "resource instruction location");
        }

        var external = new TreeMap<>(packaged);
        external.put(
                "Ltest/ExternalMenuCaller;",
                new ImmutableClassDef(
                        "Ltest/ExternalMenuCaller;",
                        1,
                        MENU + ";",
                        List.of(),
                        null,
                        Set.of(),
                        List.of(),
                        List.of(),
                        List.of(),
                        List.of()));
        reject(() -> verify(external, originalEntries), "external MenuPanel caller");
        var xml = new TreeMap<>(originalEntries);
        xml.put(
                "res/layout/control.xml",
                "com.qinggan.qgwigdets.MenuPanel".getBytes(StandardCharsets.UTF_16LE));
        reject(() -> verify(packaged, xml), "MenuPanel XML instantiation");
    }

    public static void main(String[] args) throws Exception {
        need(args.length == 2, "root candidate-apk");
        Path root = Path.of(args[0]).toRealPath(),
                original =
                        root.resolve(
                                ".local/profiles/free-09-chinese-20221230-nxp-rc45/originals/systemui.apk");
        need(
                FilesSupport.hash(original)
                        .equals("a20c1ee2c682aad846b93010356d434ef7e43ffb314b21186a91f348ba4c497b"),
                "original SystemUI pin differs");
        var classes = new TreeMap<String, ClassDef>();
        var container =
                DexFileFactory.loadDexContainer(Path.of(args[1]).toFile(), Opcodes.forApi(28));

        for (String name : container.getDexEntryNames()) {
            for (ClassDef cls : container.getEntry(name).getDexFile().getClasses()) {
                need(classes.put(cls.getType(), cls) == null, "duplicate packaged class");
            }
        }

        controls(classes, NativeCore.entries(original));
        System.out.println(
                "PASS complete typed com.qinggan closure; exact isolated resource exception and 5"
                        + " negative controls");
    }
}
