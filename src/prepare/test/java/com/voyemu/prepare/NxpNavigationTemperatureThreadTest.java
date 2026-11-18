package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/** Applies the production recipe to the exact OEM class and audits every sibling callback. */
public final class NxpNavigationTemperatureThreadTest {
    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectories(stage);
        Path apk =
                args.length == 3
                        ? Path.of(args[2])
                        : root.resolve(
                                ".local/profiles/free-09-chinese-20221230-nxp-rc45/originals/launcher.apk");
        need(
                FilesSupport.hash(apk)
                        .equals("93f4bea56530bf2de8634b7898fd4275a3c8985ff0392fdc58607ef8bc5b2441"),
                "Exact NXP Launcher APK required");
        String owner = "Lcom/qinggan/app/launcher/navigation/NavigationBar;";
        var dex =
                new DexBackedDexFile(
                        Opcodes.forApi(28), NativeCore.entries(apk).get("classes.dex"));
        var classes =
                dex.getClasses().stream()
                        .filter(c ->
                                c.getType().equals(owner)
                                        || c.getType()
                                                .startsWith(
                                                        owner.substring(0, owner.length() - 1)
                                                                + "$"))
                        .toList();
        need(classes.size() == 9, "Audit main NavigationBar and all eight callback classes");
        var recipe =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/free-09-chinese-20221230-nxp-rc45/patches/nxp-launcher-display-routing.json"))));
        var edits = new java.util.ArrayList<DexEdits.MethodEdit>();

        for (Object value : Json.list(recipe.get("calls"))) {
            var call = Json.object(value);

            if ("editMethod".equals(call.get("operation"))) {
                var edit = DexEdits.MethodEdit.parse(Json.list(call.get("args")));

                if (edit.type().equals(owner)) {
                    edits.add(edit);
                }
            }
        }

        String constructor =
                owner
                        + "-><init>(Landroid/content/Context;Landroid/os/Handler;Ljava/lang/String;I)V";
        need(
                edits.stream().anyMatch(edit -> edit.method().equals(constructor)),
                "NavigationBar still stores launcher-loader Handler; missing MAIN dispatcher"
                        + " recipe");
        var baseline = new java.util.ArrayList<org.jf.dexlib2.iface.ClassDef>();
        var changed = new java.util.ArrayList<org.jf.dexlib2.iface.ClassDef>();

        for (var type : classes) {
            if (type.getType().equals(owner)) {
                baseline.add(
                        DexEdits.apply(
                                type,
                                edits.stream()
                                        .filter(edit -> !edit.method().equals(constructor))
                                        .toList()));
                changed.add(DexEdits.apply(type, edits));
            } else {
                baseline.add(type);
                changed.add(type);
            }
        }

        int untouched = auditMethods(changed, baseline, owner);
        System.out.println(
                "PASS actual NXP NavigationBar DEX: one MAIN-bound handler assignment, two"
                        + " preserved callback/Runnable paths, "
                        + untouched
                        + " unchanged methods across nine classes");
    }

    /** Returns the number of unchanged methods after auditing every changed-DEX method. */
    static int auditMethods(
            List<org.jf.dexlib2.iface.ClassDef> changed,
            List<org.jf.dexlib2.iface.ClassDef> baseline,
            String owner)
            throws Exception {
        int untouched = 0, constructors = 0;
        int[] uses = new int[3];

        for (var cls : changed) {
            for (var method : cls.getMethods()) {
                var old =
                        baseline.stream()
                                .filter(c -> c.getType().equals(cls.getType()))
                                .flatMap(c ->
                                        java.util.stream.StreamSupport.stream(
                                                c.getMethods().spliterator(), false))
                                .filter(m ->
                                        m.getName().equals(method.getName())
                                                && m.getParameterTypes()
                                                        .toString()
                                                        .equals(
                                                                method.getParameterTypes()
                                                                        .toString()))
                                .findFirst()
                                .orElseThrow();

                if (cls.getType().equals(owner) && method.getName().equals("<init>")) {
                    checkMainHandler(method, owner);
                    constructors++;
                } else {
                    need(
                            FrameworkDexMaterializer.semantic(old)
                                    .equals(FrameworkDexMaterializer.semantic(method)),
                            "Unrelated OEM method changed: " + cls.getType() + method.getName());
                    untouched++;
                }

                countHandlerUse(method, owner, uses);
            }
        }

        int posts = uses[0], updates = uses[1], handlerReads = uses[2];
        need(
                constructors == 1 && posts == 2 && updates == 2 && handlerReads == 1,
                "Only two temperature callbacks consume the private handler accessor");

        return untouched;
    }

    static void checkMainHandler(org.jf.dexlib2.iface.Method method, String owner) {
        int looper = -1, handler = -1;
        boolean pending = false, constructed = false, assigned = false;

        for (var instruction : method.getImplementation().getInstructions()) {
            if (pending) {
                need(
                        instruction
                                instanceof org.jf.dexlib2.iface.instruction.OneRegisterInstruction,
                        "MAIN Looper result register");
                looper =
                        ((org.jf.dexlib2.iface.instruction.OneRegisterInstruction) instruction)
                                .getRegisterA();
                pending = false;
            }

            if (instruction instanceof ReferenceInstruction r) {
                String ref = r.getReference().toString();

                if (ref.equals("Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;")) {
                    pending = true;
                }

                if (instruction.getOpcode().name.equals("new-instance")
                        && ref.equals("Landroid/os/Handler;")) {
                    handler =
                            ((org.jf.dexlib2.iface.instruction.OneRegisterInstruction) instruction)
                                    .getRegisterA();
                }

                if (ref.equals("Landroid/os/Handler;-><init>(Landroid/os/Looper;)V")) {
                    var regs =
                            (org.jf.dexlib2.iface.instruction.FiveRegisterInstruction) instruction;
                    constructed =
                            regs.getRegisterC() == handler
                                    && regs.getRegisterD() == looper
                                    && looper >= 0;
                }

                if (instruction.getOpcode().name.equals("iput-object")
                        && ref.equals(owner + "->mHandler:Landroid/os/Handler;")) {
                    assigned =
                            constructed
                                    && ((org.jf.dexlib2.iface.instruction.TwoRegisterInstruction)
                                                            instruction)
                                                    .getRegisterA()
                                            == handler;
                }
            }
        }

        need(assigned, "Stored Handler must be the instance constructed from MAIN Looper");
    }

    /** Adds Handler.post, updateTemperature and mHandler read counts to posts, updates, reads. */
    static void countHandlerUse(org.jf.dexlib2.iface.Method method, String owner, int[] uses) {
        if (method.getImplementation() != null) {
            for (var instruction : method.getImplementation().getInstructions()) {
                if (instruction instanceof ReferenceInstruction r) {
                    String ref = r.getReference().toString();

                    if (ref.equals("Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z")) {
                        uses[0]++;
                    }

                    if (ref.equals(
                            owner + "->updateTemperature(Lcom/qinggan/canbus/AirCondition;)V")) {
                        uses[1]++;
                    }

                    if (instruction.getOpcode().name.equals("iget-object")
                            && ref.equals(owner + "->mHandler:Landroid/os/Handler;")) {
                        uses[2]++;
                    }
                }
            }
        }
    }
}
