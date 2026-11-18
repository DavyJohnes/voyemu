package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/** The classes of every DEX entry of one APK or JAR; only changed entries are rewritten. */
final class DexClasses {
    private final int api;
    private final Map<String, Map<String, ClassDef>> entries = new LinkedHashMap<>();
    private final Set<String> changed = new TreeSet<>();

    DexClasses(Map<String, byte[]> archive, List<String> dexNames, int api) {
        this.api = api;
        Failure.require(
                !dexNames.isEmpty(), "DEX_INPUT_MISSING", "Code artifact contains no DEX", 5);
        Set<String> all = new TreeSet<>();

        for (String name : dexNames) {
            Map<String, ClassDef> classes = new LinkedHashMap<>();

            for (ClassDef type :
                    new DexBackedDexFile(Opcodes.forApi(api), archive.get(name)).getClasses()) {
                Failure.require(
                        classes.put(type.getType(), type) == null && all.add(type.getType()),
                        "DEX_CLASS_COLLISION",
                        "Original APK has duplicate classes",
                        5);
            }

            entries.put(name, classes);
        }
    }

    /** The class inventory of one DEX entry. */
    Set<String> classes(String dexName) {
        return new TreeSet<>(entries.get(dexName).keySet());
    }

    /** The DEX entry that defines a class, or null. */
    String entry(String type) {
        for (var entry : entries.entrySet()) {
            if (entry.getValue().containsKey(type)) {
                return entry.getKey();
            }
        }

        return null;
    }

    ClassDef get(String type) {
        String entry = entry(type);

        Failure.require(entry != null, "DEX_EDIT_ANCHOR", "Edited class absent: " + type, 5);

        return entries.get(entry).get(type);
    }

    /** Replaces an existing class definition in its DEX entry. */
    void replace(ClassDef type) {
        String entry = entry(type.getType());

        Failure.require(
                entry != null, "DEX_EDIT_ANCHOR", "Edited class absent: " + type.getType(), 5);
        entries.get(entry).put(type.getType(), type);
        changed.add(entry);
    }

    /** Adds a class that no DEX entry defines yet. */
    void add(String dexName, ClassDef type) {
        Failure.require(
                entry(type.getType()) == null && entries.containsKey(dexName),
                "DEX_CLASS_COLLISION",
                "Added class already exists",
                5);
        entries.get(dexName).put(type.getType(), type);
        changed.add(dexName);
    }

    /** Applies method edits to their classes. */
    void edit(List<DexEdits.MethodEdit> edits) {
        Map<String, List<DexEdits.MethodEdit>> byType = new LinkedHashMap<>();

        for (DexEdits.MethodEdit edit : edits) {
            byType.computeIfAbsent(edit.type(), key -> new java.util.ArrayList<>()).add(edit);
        }

        for (var entry : byType.entrySet()) {
            replace(DexEdits.apply(get(entry.getKey()), entry.getValue()));
        }
    }

    /** Serializes every changed DEX entry into the archive entries. */
    void write(Map<String, byte[]> archive) throws Exception {
        for (String name : changed) {
            Map<String, ClassDef> classes = entries.get(name);
            byte[] bytes =
                    FrameworkDexMaterializer.serialize(
                            new ImmutableDexFile(Opcodes.forApi(api), classes.values()));
            Set<String> actual = new TreeSet<>();

            for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(api), bytes).getClasses()) {
                actual.add(type.getType());
            }

            Failure.require(
                    actual.equals(
                            classes.keySet().stream()
                                    .collect(
                                            java.util.stream.Collectors.toCollection(
                                                    TreeSet::new))),
                    "DEX_CLASS_INVENTORY_CHANGED",
                    "Rebuilt DEX lost or added unrequested classes",
                    5);
            archive.put(name, bytes);
        }
    }
}
