package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * Builds component:canbus of one profile through the ordinary Artifacts path and compares the
 * public methods of its CAN bus component classes with the OEM CanBusService API of that firmware:
 * every OEM method is declared by the same class with the same descriptor and the same
 * abstractness, and the classes declare nothing else but the Voyemu lifecycle methods.
 */
public final class CanbusApiParity {
    private static final String PACKAGE = "Lcom/qinggan/canbus/service/";
    private static final Map<String, String> CLASSES =
            Map.of(
                    "BaseCanBusComponent",
                    PACKAGE + "BaseCanBusComponent;",
                    "DongfengCanBusComponentImpl",
                    PACKAGE + "protocol/dongfeng/DongfengCanBusComponentImpl;");
    private static final Map<String, Set<String>> LIFECYCLE =
            Map.of(
                    "BaseCanBusComponent",
                    Set.of(
                            "start()V",
                            "stop()V",
                            "setAccStatus(I)V",
                            "state()" + PACKAGE + "CanBusState;"),
                    "DongfengCanBusComponentImpl",
                    Set.of("start()V", "stop()V", "setAccStatus(I)V"));

    private CanbusApiParity() {}

    static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    static Toolchains toolchains(Path root, String id, Storage storage) throws Exception {
        var signing =
                Json.obj(
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                root.resolve(
                                                        ".local/profiles/"
                                                                + id
                                                                + "/native-inputs.json")))),
                        "signing");
        Path key = Path.of(Json.str(Json.obj(signing, "key"), "file")),
                cert = Path.of(Json.str(Json.obj(signing, "cert"), "file"));

        NativeCore.pinned(key, Json.str(Json.obj(signing, "key"), "sha256"));
        NativeCore.pinned(cert, Json.str(Json.obj(signing, "cert"), "sha256"));

        return new Toolchains(
                storage,
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json")))),
                root.resolve(".local/toolchains/native-sdk"),
                key,
                cert,
                root.resolve(".local/toolchains/java-sdk/platforms/android-34/android.jar"));
    }

    static Path build(Path root, Path work, String id) throws Exception {
        Path canonical = root.resolve("profiles/" + id);
        var data = Json.object(Json.parse(Files.readString(canonical.resolve("profile.json"))));
        var profile = new ProfileRegistry.Profile(id, "canbus-api-parity", data, "", root);
        var storage =
                new Storage(
                        root.resolve(".local"),
                        work.resolve("config"),
                        work.resolve("cache"),
                        work.resolve("state"),
                        work.resolve("run"),
                        work.resolve("logs"));
        var builder =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of()),
                        storage,
                        toolchains(root, id, storage),
                        canonical,
                        work.resolve("artifacts"),
                        logical -> {
                            throw new AssertionError("Unexpected guest input " + logical);
                        },
                        System.err::println);

        return builder.resolve("component:canbus");
    }

    /** Public methods of one DEX class as "abstract|concrete name(descriptor)". */
    static Set<String> api(ClassDef type) {
        Set<String> result = new TreeSet<>();

        for (Method method : type.getMethods()) {
            int flags = method.getAccessFlags();

            if (!AccessFlags.PUBLIC.isSet(flags) || AccessFlags.SYNTHETIC.isSet(flags)) {
                continue;
            }

            result.add(
                    (AccessFlags.ABSTRACT.isSet(flags) ? "abstract " : "concrete ")
                            + method.getName()
                            + "("
                            + String.join("", method.getParameterTypes())
                            + ")"
                            + method.getReturnType());
        }

        return result;
    }

    public static void main(String[] args) throws Exception {
        need(args.length == 4, "Arguments: root work profile expected-api");
        Path root = Path.of(args[0]), work = Path.of(args[1]);
        String id = args[2];
        Map<String, Set<String>> expected = new TreeMap<>();

        for (String line : Files.readAllLines(Path.of(args[3]))) {
            String[] parts = line.split(" ", 2);
            expected.computeIfAbsent(parts[0], key -> new TreeSet<>()).add(parts[1]);
        }

        need(expected.keySet().equals(CLASSES.keySet()), "OEM API of both classes required");
        Path apk = build(root, work, id);
        Map<String, ClassDef> dex = new TreeMap<>();
        var container = DexFileFactory.loadDexContainer(apk.toFile(), Opcodes.forApi(28));
        List<String> listing = new ArrayList<>();

        for (String entry : container.getDexEntryNames()) {
            for (ClassDef type : container.getEntry(entry).getDexFile().getClasses()) {
                dex.put(type.getType(), type);

                for (String method : api(type)) {
                    listing.add(type.getType() + " " + method);
                }
            }
        }

        listing.sort(null);
        Files.write(work.resolve("dex-api.txt"), listing);
        int compared = 0;

        for (var entry : CLASSES.entrySet()) {
            ClassDef type = dex.get(entry.getValue());
            need(type != null, id + ": " + entry.getKey() + " absent from the APK");
            need(
                    AccessFlags.PUBLIC.isSet(type.getAccessFlags())
                            && !AccessFlags.ABSTRACT.isSet(type.getAccessFlags())
                            && !AccessFlags.INTERFACE.isSet(type.getAccessFlags()),
                    id + ": " + entry.getKey() + " must be a public concrete class as in OEM");
            Set<String> actual = api(type);
            Set<String> oem = expected.get(entry.getKey());
            Set<String> missing = new TreeSet<>(oem);
            missing.removeAll(actual);
            need(missing.isEmpty(), id + ": " + entry.getKey() + " lacks OEM methods " + missing);
            Set<String> extra = new TreeSet<>(actual);
            extra.removeAll(oem);

            for (String method : LIFECYCLE.get(entry.getKey())) {
                extra.remove("concrete " + method);
            }

            need(
                    extra.isEmpty(),
                    id + ": " + entry.getKey() + " declares non-OEM methods " + extra);
            compared += oem.size();
        }

        System.out.println(
                id
                        + ": "
                        + compared
                        + " OEM component methods match; "
                        + listing.size()
                        + " public DEX methods listed in "
                        + work.resolve("dex-api.txt"));
    }
}
