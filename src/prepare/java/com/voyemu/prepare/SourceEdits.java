package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;

/** Line-coordinate edits preserve owner bytes via copy ranges rather than embedded text. */
final class SourceEdits {
    record Edit(int start, int delete, String insertion) {}

    static String apply(String source, List<Object> definitions, String expected) throws Exception {
        List<String> lines = lines(source);
        List<Edit> edits = new ArrayList<>();

        for (Object item : definitions) {
            Map<String, Object> definition = Json.object(item);
            int start = Math.toIntExact(Json.integer(definition, "startLine")) - 1;
            int delete = Math.toIntExact(Json.integer(definition, "deleteCount"));
            Failure.require(
                    start >= 0
                            && start <= lines.size()
                            && delete >= 0
                            && start + delete <= lines.size(),
                    "SOURCE_EDIT_RANGE",
                    "Edit leaves source line extent",
                    5);
            StringBuilder insertion = new StringBuilder();

            for (Object part : Json.list(definition.get("insert"))) {
                Map<String, Object> value = Json.object(part);
                Failure.require(
                        value.containsKey("text") != value.containsKey("copy"),
                        "SOURCE_EDIT_INVALID",
                        "Insert must contain text or copy",
                        5);

                if (value.containsKey("text")) {
                    insertion.append(Json.str(value, "text"));
                } else {
                    Map<String, Object> copy = Json.obj(value, "copy");
                    int from = Math.toIntExact(Json.integer(copy, "startLine")) - 1;
                    int count = Math.toIntExact(Json.integer(copy, "lineCount"));
                    Failure.require(
                            from >= 0 && count >= 0 && from + count <= lines.size(),
                            "SOURCE_EDIT_RANGE",
                            "Copy leaves source line extent",
                            5);

                    for (int i = from; i < from + count; i++) {
                        insertion.append(lines.get(i));
                    }
                }
            }

            edits.add(new Edit(start, delete, insertion.toString()));
        }

        edits.sort(Comparator.comparingInt(Edit::start));
        int previous = -1;

        for (Edit edit : edits) {
            Failure.require(
                    edit.start() > previous, "SOURCE_EDIT_OVERLAP", "Overlapping line edits", 5);
            previous = edit.start() + Math.max(1, edit.delete()) - 1;
        }

        StringBuilder result = new StringBuilder();
        int cursor = 0;

        for (Edit edit : edits) {
            for (; cursor < edit.start(); cursor++) {
                result.append(lines.get(cursor));
            }

            result.append(edit.insertion());
            cursor += edit.delete();
        }

        for (; cursor < lines.size(); cursor++) {
            result.append(lines.get(cursor));
        }

        String text = result.toString();
        Failure.require(
                expected == null
                        || NativeCore.hash(text.getBytes(StandardCharsets.UTF_8)).equals(expected),
                "SOURCE_EDIT_HASH_MISMATCH",
                "Edited source differs from its pinned result",
                5);

        return text;
    }

    static void file(Path file, List<Object> edits, String expected) throws Exception {
        String text = Files.readString(file);
        String result = apply(text, edits, expected);

        Files.writeString(file, result);
    }

    static void upstream(Path root, Path specification) throws Exception {
        Map<String, Object> manifest = Json.object(Json.parse(Files.readString(specification)));

        Failure.require(
                Json.integer(manifest, "version") == 1,
                "SOURCE_EDIT_INVALID",
                "Unsupported source edits version",
                5);

        for (Object item : Json.list(manifest.get("files"))) {
            Map<String, Object> definition = Json.object(item);
            Path file = Storage.confined(root, Json.str(definition, "path"));
            file(file, Json.list(definition.get("edits")), Json.str(definition, "resultSha256"));
        }
    }

    private static List<String> lines(String text) {
        List<String> result = new ArrayList<>();
        int start = 0;

        for (int end; (end = text.indexOf('\n', start)) >= 0; ) {
            result.add(text.substring(start, end + 1));
            start = end + 1;
        }

        if (start < text.length()) {
            result.add(text.substring(start));
        }

        return result;
    }
}
