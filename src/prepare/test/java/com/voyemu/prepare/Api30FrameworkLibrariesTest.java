package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.w3c.dom.Element;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

import javax.xml.parsers.DocumentBuilderFactory;

/**
 * API30 shared libraries have separate class loaders; exact owner references need explicit edges.
 */
public final class Api30FrameworkLibrariesTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 2) {
            throw new IllegalArgumentException("ROOT [PROFILE_METADATA_DIRECTORY]");
        }

        Path root = Path.of(args[0]).toRealPath();
        String id = "free-11-chinese-20260731-7.3.1";
        Path metadata =
                args.length == 2 ? Path.of(args[1]).toRealPath() : root.resolve("profiles/" + id);
        var profile = Json.object(Json.parse(Files.readString(metadata.resolve("profile.json"))));
        check(Json.str(profile, "id").equals(id), "exact Free API30 owner profile");
        var original = Json.obj(profile, "original");
        String[] names = {"QGAPI", "QGMOD", "QGVehicle"};
        Map<String, Set<String>> definitions = new TreeMap<>(), references = new TreeMap<>();

        for (String name : names) {
            var input = Json.obj(original, name);
            Path file =
                    root.resolve(".local/profiles/" + id + "/originals")
                            .resolve(Json.str(input, "file"));
            check(
                    FilesSupport.hash(file).equals(Json.str(input, "sha256")),
                    "exact owner JAR pin: " + name);
            var classes = new TreeSet<String>();
            var refs = new TreeSet<String>();
            var container = DexFileFactory.loadDexContainer(file.toFile(), Opcodes.forApi(30));

            for (String entry : container.getDexEntryNames()) {
                var dex = container.getEntry(entry).getDexFile();

                for (var type : dex.getClasses()) {
                    classes.add(type.getType());
                }

                for (var type : dex.getTypeReferences()) {
                    refs.add(type.getType());
                }
            }

            definitions.put(name, classes);
            references.put(name, refs);
        }

        check(
                definitions.get("QGAPI").contains("Lcom/qinggan/os/ServiceManager;"),
                "ServiceManager exists; no invented donor required");
        check(
                definitions.get("QGVehicle").contains("Lcom/qinggan/carsignal/CarSignalManager;"),
                "actual failing caller owner");
        Map<String, Set<String>> required = new TreeMap<>();

        for (String name : names) {
            var edges = new TreeSet<String>();

            for (String other : names) {
                if (!name.equals(other)) {
                    var used = new TreeSet<>(references.get(name));
                    used.removeAll(definitions.get(name));
                    used.retainAll(definitions.get(other));

                    if (!used.isEmpty()) {
                        edges.add(other);
                    }
                }
            }

            required.put(name, edges);
        }

        check(
                required.equals(
                        Map.of(
                                "QGAPI",
                                Set.of(),
                                "QGMOD",
                                Set.of("QGAPI"),
                                "QGVehicle",
                                Set.of("QGAPI"))),
                "actual owner reference graph changed");
        var document =
                DocumentBuilderFactory.newInstance()
                        .newDocumentBuilder()
                        .parse(metadata.resolve("files/voyemu-qinggan-framework.xml").toFile());
        var libraries = document.getDocumentElement().getElementsByTagName("library");
        Map<String, Set<String>> declared = new TreeMap<>();
        var expected = new TreeMap<>(required);
        expected.put("VoyemuFreeWheel", Set.of());
        expected.put("android.car", Set.of());

        for (int i = 0; i < libraries.getLength(); ++i) {
            Element library = (Element) libraries.item(i);
            String name = library.getAttribute("name"),
                    dependency = library.getAttribute("dependency");
            check(
                    expected.containsKey(name) && !declared.containsKey(name),
                    "unique exact library: " + name);
            check(
                    library.getAttribute("file")
                            .equals(
                                    name.equals("VoyemuFreeWheel")
                                            ? "/system/framework/voyemu-free-wheel.jar"
                                            : "/system/framework/" + name + ".jar"),
                    "library path unchanged");
            Set<String> edges =
                    dependency.isEmpty()
                            ? Set.of()
                            : new TreeSet<>(java.util.List.of(dependency.split(":")));
            declared.put(name, edges);
        }

        check(
                declared.equals(expected),
                "Exact OEM dependencies and independent car and wheel libraries required");
        System.out.println(
                Json.write(
                        Map.of(
                                "profile",
                                id,
                                "owner_graph",
                                required,
                                "xml_dependency_graph_verified",
                                true,
                                "guest_started",
                                false)));
    }
}
