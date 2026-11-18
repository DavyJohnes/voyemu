package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.reference.StringReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Builds component:canbus of one profile through the ordinary Artifacts path and checks the OEM
 * start order of the OEM CanBusService: the package is persistent and its Application starts the
 * one service that answers qg.canbus within the package when the persistent process starts.
 */
public final class CanbusPersistentStartTest {
    private static final String APPLICATION = "com.qinggan.canbus.service.CanApplication";

    private CanbusPersistentStartTest() {}

    static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    /** Service names whose intent filter declares the action, in a decoded aapt2 manifest tree. */
    static List<String> services(String manifest, String action) {
        List<String> result = new ArrayList<>();
        String[] lines = manifest.lines().toArray(String[]::new);

        for (int i = 0; i < lines.length; i++) {
            if (!lines[i].trim().startsWith("E: service (")) {
                continue;
            }

            int indent = lines[i].indexOf('E');
            StringBuilder body = new StringBuilder();

            for (int j = i + 1;
                    j < lines.length && lines[j].indexOf(lines[j].trim()) > indent;
                    j++) {
                body.append(lines[j]).append('\n');
            }

            Matcher name = Pattern.compile("android:name\\([^)]*\\)=\"([^\"]+)\"").matcher(body);

            if (name.find() && body.toString().contains("=\"" + action + "\"")) {
                result.add(name.group(1));
            }
        }

        return result;
    }

    static String application(String manifest) {
        int at = manifest.indexOf("E: application (");

        need(at >= 0, "Manifest application missing");
        int end = manifest.indexOf(" E: ", at + 20);

        return manifest.substring(at, end < 0 ? manifest.length() : end);
    }

    /** The checks reject the retained manifest of the data-installed CanBus started on boot. */
    static void manifestSelfTest(Path retained) throws Exception {
        String manifest = Files.readString(retained);
        String application = application(manifest);

        need(
                !application.contains("android:persistent(0x0101000d)=true")
                        && !application.contains("\"" + APPLICATION + "\""),
                "Retained boot-started manifest accepted as persistent");
        need(
                services(manifest, "qg.canbus")
                        .equals(List.of("com.qinggan.canbus.service.RegistrationService")),
                "Retained qg.canbus service not found");
        System.out.println("CanbusPersistentStartTest manifest self-test PASS");
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 2 && args[0].equals("--manifest-self-test")) {
            manifestSelfTest(Path.of(args[1]));

            return;
        }

        need(args.length == 3, "Arguments: root work profile");
        Path root = Path.of(args[0]), work = Path.of(args[1]);
        String id = args[2];
        Path apk = CanbusApiParity.build(root, work, id);
        var storage =
                new Storage(
                        root.resolve(".local"),
                        work.resolve("config"),
                        work.resolve("cache"),
                        work.resolve("state"),
                        work.resolve("run"),
                        work.resolve("logs"));
        String manifest =
                CanbusComponentRegression.dump(
                        CanbusApiParity.toolchains(root, id, storage).buildTool("aapt2"), apk);
        String application = application(manifest);
        need(
                application.contains("android:name(0x01010003)=\"" + APPLICATION + "\""),
                id + ": Application is not CanApplication");
        need(
                application.contains("android:persistent(0x0101000d)=true"),
                id + ": CanBus package is not persistent");
        List<String> answering = services(manifest, "qg.canbus");
        need(answering.size() == 1, id + ": qg.canbus services " + answering);
        var container = DexFileFactory.loadDexContainer(apk.toFile(), Opcodes.forApi(28));
        ClassDef type = null;

        for (String entry : container.getDexEntryNames()) {
            for (var c : container.getEntry(entry).getDexFile().getClasses()) {
                if (c.getType().equals("L" + APPLICATION.replace('.', '/') + ";")) {
                    type = c;
                }
            }
        }

        need(
                type != null && "Landroid/app/Application;".equals(type.getSuperclass()),
                id + ": CanApplication is not an Application");
        boolean action = false, started = false;

        for (var method : type.getMethods()) {
            if (!method.getName().equals("onCreate") || method.getImplementation() == null) {
                continue;
            }

            for (var instruction : method.getImplementation().getInstructions()) {
                if (instruction instanceof ReferenceInstruction reference) {
                    if (reference.getReference() instanceof StringReference text
                            && text.getString().equals("qg.canbus")) {
                        action = true;
                    }

                    if (reference.getReference() instanceof MethodReference call
                            && call.getName().equals("startService")) {
                        started = true;
                    }
                }
            }
        }

        need(action && started, id + ": CanApplication.onCreate does not start qg.canbus");
        Files.writeString(work.resolve("manifest.txt"), manifest);
        System.out.println(id + " persistent " + answering.get(0));
    }
}
