package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;

public final class SourceEditsTest {
    public static void main(String[] args) throws Exception {
        String owner = "first\nsecond\nlast";
        String result = "first\nlastown\nlast";
        var edit =
                Map.of(
                        "startLine",
                        2,
                        "deleteCount",
                        1,
                        "insert",
                        List.of(
                                Map.of("copy", Map.of("startLine", 3, "lineCount", 1)),
                                Map.of("text", "own\n")));

        StorageJsonTest.check(
                SourceEdits.apply(
                                owner,
                                List.of(edit),
                                NativeCore.hash(result.getBytes(StandardCharsets.UTF_8)))
                        .equals(result));

        try {
            SourceEdits.apply(owner, List.of(edit), "0".repeat(64));
            throw new AssertionError();
        } catch (Failure expected) {
            if (!expected.code.equals("SOURCE_EDIT_HASH_MISMATCH")) {
                throw expected;
            }
        }

        try {
            SourceEdits.apply(owner, List.of(edit, edit), null);
            throw new AssertionError();
        } catch (Failure expected) {
            if (!expected.code.equals("SOURCE_EDIT_OVERLAP")) {
                throw expected;
            }
        }

        try {
            SourceEdits.apply(
                    owner,
                    List.of(Map.of("startLine", 99, "deleteCount", 0, "insert", List.of())),
                    null);
            throw new AssertionError();
        } catch (Failure expected) {
            if (!expected.code.equals("SOURCE_EDIT_RANGE")) {
                throw expected;
            }
        }

        MultiDisplayStartupTest.run();
        System.out.println("Pinned line edits, owner copy ranges and overlap guards passed");
    }
}
