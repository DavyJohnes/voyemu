package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/** Shared own source reuse; OEM donor bytes remain in declared original artifacts. */
final class ComponentSources {
    interface Reader {
        byte[] read(String resource) throws Exception;
    }

    static void copy(Map<String, Object> files, Path destination, Reader reader) throws Exception {
        require(!files.isEmpty() && files.size() <= 256, "Bounded source inventory required");
        long total = 0;

        for (var entry : files.entrySet()) {
            String name = entry.getKey();
            require(
                    name.equals("AndroidManifest.xml")
                            || name.matches("src/[A-Za-z0-9_$/-]+\\.(?:java|c)"),
                    "Own Java or C source or Android manifest required");
            require(entry.getValue() instanceof String, "Source resource path required");
            String resource = (String) entry.getValue();
            require(
                    resource.matches(
                            "voyemu/profiles/[a-z0-9][a-z0-9._-]{0,127}/(?:components|patches)/[a-z0-9][a-z0-9._-]{0,127}/(?:src/[A-Za-z0-9_$/-]+\\.(?:java|c)|AndroidManifest\\.xml)"),
                    "Confined profile component resource required");
            Path target = Storage.confined(destination, name);
            require(!Files.exists(target), "Shared source would overwrite local source");
            byte[] bytes = reader.read(resource);
            total += bytes.length;
            require(
                    bytes.length > 0 && bytes.length <= 1024 * 1024 && total <= 8 * 1024 * 1024,
                    "Shared source byte limit exceeded");
            Storage.directory(target.getParent());
            Files.write(target, bytes, StandardOpenOption.CREATE_NEW);
        }
    }

    static void editDeclared(Map<String, Object> definition, Path destination) throws Exception {
        if (definition.containsKey("sourceEdits")) {
            edit(
                    Json.obj(definition, "sourceFiles"),
                    destination,
                    Storage.confined(destination, Json.str(definition, "sourceEdits")));
        }
    }

    /** Adapts declared own sources; semantic anchors replace output hash pins and line offsets. */
    static void edit(Map<String, Object> files, Path destination, Path specification)
            throws Exception {
        Map<String, Object> manifest = Json.object(Json.parse(Files.readString(specification)));

        require(
                manifest.keySet().equals(Set.of("version", "files"))
                        && Json.integer(manifest, "version") == 1,
                "Own source edit version required");
        Map<Path, String> results = new LinkedHashMap<>();
        require(!Json.list(manifest.get("files")).isEmpty(), "Own source edits required");

        for (Object item : Json.list(manifest.get("files"))) {
            Map<String, Object> definition = Json.object(item);
            require(
                    definition.keySet().equals(Set.of("path", "replacements")),
                    "Own source replacements required without result pins");
            String name = Json.str(definition, "path");
            require(files.containsKey(name), "Only declared own sources may be edited");
            Path target = Storage.confined(destination, name);
            require(!results.containsKey(target), "Duplicate own source edit");
            String text = Files.readString(target);
            require(
                    !Json.list(definition.get("replacements")).isEmpty(),
                    "Source replacements required");

            for (Object entry : Json.list(definition.get("replacements"))) {
                Map<String, Object> replacement = Json.object(entry);
                require(
                        replacement.keySet().equals(Set.of("before", "after")),
                        "Source anchor required");
                String before = Json.str(replacement, "before"),
                        after = Json.str(replacement, "after");
                int offset = text.indexOf(before);
                require(
                        !before.isEmpty()
                                && !before.equals(after)
                                && offset >= 0
                                && text.indexOf(before, offset + 1) < 0,
                        "Own source anchor missing or ambiguous");
                text = text.substring(0, offset) + after + text.substring(offset + before.length());
            }

            results.put(target, text);
        }

        for (var result : results.entrySet()) {
            Files.writeString(result.getKey(), result.getValue());
        }
    }

    private static void require(boolean accepted, String reason) {
        Failure.require(accepted, "COMPONENT_SOURCE", reason, 3);
    }
}
