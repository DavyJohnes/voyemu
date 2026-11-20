package com.voyemu.prepare;

import java.util.HashMap;
import java.util.Map;

public final class ProfilePropertiesTest {
    static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    static void rejected(Runnable action, String message) {
        try {
            action.run();
        } catch (Failure expected) {
            check(expected.code.equals("PROFILE_INVALID"), message);

            return;
        }

        throw new AssertionError(message);
    }

    static ProfileRegistry.Profile profile(Map<String, Object> data) {
        return new ProfileRegistry.Profile("synthetic-profile", "test", data, "");
    }

    public static void main(String[] args) {
        Map<String, Object> declared = new HashMap<>();

        declared.put("schema", 1L);
        declared.put("title", "Synthetic 1.0");
        declared.put("platform", "qinggan11");
        declared.put("board", "8155");
        var synthetic = profile(declared);

        check(
                synthetic.platform().equals("qinggan11")
                        && synthetic.board().equals("8155")
                        && synthetic.title().equals("Synthetic 1.0"),
                "declared platform, board and title are read");

        for (String platform : new String[] {"qinggan09", "qinggan11", "passion"}) {
            for (String board : new String[] {"8155", "nxp"}) {
                Map<String, Object> data = new HashMap<>(declared);
                data.put("platform", platform);
                data.put("board", board);
                check(
                        profile(data).platform().equals(platform)
                                && profile(data).board().equals(board),
                        "every platform and board value is accepted");
            }
        }

        for (Object value : new Object[] {"qinggan10", "QINGGAN11", "", 11L}) {
            Map<String, Object> platform = new HashMap<>(declared);
            platform.put("platform", value);
            rejected(() -> profile(platform), "unknown platform rejected on load");
            Map<String, Object> board = new HashMap<>(declared);
            board.put("board", value);
            rejected(() -> profile(board), "unknown board rejected on load");
        }

        Map<String, Object> withoutBoard = new HashMap<>(declared);
        withoutBoard.remove("board");
        rejected(() -> profile(withoutBoard), "platform without board rejected on load");
        Map<String, Object> emptyTitle = new HashMap<>(declared);
        emptyTitle.put("title", "");
        rejected(() -> profile(emptyTitle), "empty title rejected on load");
        var undeclared = profile(Map.of("schema", 1L));
        rejected(undeclared::platform, "undeclared platform rejected on access");
        rejected(undeclared::board, "undeclared board rejected on access");
        rejected(undeclared::title, "undeclared title rejected on access");
        System.out.println("Profile platform, board and title validated");
    }
}
