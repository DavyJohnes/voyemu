package android.util;

import java.util.ArrayList;
import java.util.List;

/**
 * Records real production diagnostic calls; never truncates before checking the producer's bound.
 */
public final class Log {
    private static final List<String> RECORDS = new ArrayList<String>();

    private static synchronized int record(String tag, String message, Throwable error) {
        if (!"VoyemuPhonePeer".equals(tag)) {
            throw new AssertionError("unexpected diagnostic tag");
        }

        if (message == null || message.length() > 512 || error != null) {
            throw new AssertionError("unbounded diagnostic content");
        }

        if (RECORDS.size() >= 4096) {
            throw new AssertionError("diagnostic safety limit exceeded");
        }

        RECORDS.add(message);

        return 0;
    }

    public static int d(String tag, String message) {
        return record(tag, message, null);
    }

    public static int i(String tag, String message) {
        return record(tag, message, null);
    }

    public static int w(String tag, String message) {
        return record(tag, message, null);
    }

    public static int e(String tag, String message) {
        return record(tag, message, null);
    }

    public static int w(String tag, String message, Throwable error) {
        return record(tag, message, error);
    }

    public static int e(String tag, String message, Throwable error) {
        return record(tag, message, error);
    }

    public static synchronized int count() {
        return RECORDS.size();
    }

    public static synchronized void assertSafe(int maximum) {
        if (RECORDS.isEmpty() || RECORDS.size() > maximum) {
            throw new AssertionError("diagnostic record count " + RECORDS.size());
        }

        for (String record : RECORDS) {
            if (record.contains("+7")
                    || record.contains("70000000001")
                    || record.contains("70000000002")
                    || record.contains("Иван")
                    || record.contains("Анна")
                    || record.contains("Тестовый")
                    || record.contains("Voyemu Phone")
                    || record.contains("owner-token")
                    || record.contains("session-token")
                    || record.contains("test-token")
                    || record.matches(".*[a-f0-9]{24,}.*")) {
                throw new AssertionError("sensitive diagnostic content");
            }
        }
    }
}
