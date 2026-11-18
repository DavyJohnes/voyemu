package com.voyemu.api;

/**
 * Bounded framework-only diagnostics; exception messages and request/source data never leave the
 * guest.
 */
final class FailureMetadata {
    private static boolean framework(String name) {
        return name != null
                && name.length() <= 160
                && name.matches("[A-Za-z_$][A-Za-z0-9_.$]*")
                && (name.startsWith("android.")
                        || name.startsWith("com.android.")
                        || name.startsWith("java.")
                        || name.startsWith("com.voyemu.api."));
    }

    private static String code(Throwable error) {
        String message = error.getMessage();

        if (error instanceof IllegalStateException && message != null) {
            if (message.equals("UiAutomation already connected!")) {
                return "automation_already_connected";
            }

            if (message.startsWith("UiAutomation not connected")) {
                return "automation_not_connected";
            }

            if (message.startsWith("UiAutomationService ")
                    && message.endsWith("already registered!")) {
                return "automation_service_already_registered";
            }

            if (message.startsWith("Error while connecting UiAutomation")) {
                return "automation_connect_failed";
            }
        }

        return "unclassified";
    }

    static java.util.Map<String, Object> fields(Throwable error) {
        java.util.Map<String, Object> result = new java.util.LinkedHashMap<>();
        java.util.List<java.util.Map<String, Object>> chain = new java.util.ArrayList<>();
        java.util.Set<Throwable> seen =
                java.util.Collections.newSetFromMap(
                        new java.util.IdentityHashMap<Throwable, Boolean>());

        for (int depth = 0;
                error != null && depth < 4 && seen.add(error);
                ++depth, error = error.getCause()) {
            java.util.Map<String, Object> cause = new java.util.LinkedHashMap<>();
            String type = error.getClass().getName();
            cause.put("type", framework(type) ? type : "redacted");
            cause.put("code", code(error));
            java.util.List<java.util.Map<String, Object>> frames = new java.util.ArrayList<>();

            for (StackTraceElement frame : error.getStackTrace()) {
                if (frames.size() == 4) {
                    break;
                }

                String method = frame.getMethodName();

                if (!framework(frame.getClassName())
                        || method == null
                        || method.length() > 80
                        || !method.matches("[A-Za-z_$<>][A-Za-z0-9_$<>]*")) {
                    continue;
                }

                java.util.Map<String, Object> safe = new java.util.LinkedHashMap<>();
                safe.put("class", frame.getClassName());
                safe.put("method", method);
                safe.put("line", Math.max(-1, frame.getLineNumber()));
                frames.add(safe);
            }

            cause.put("frames", frames);
            chain.add(cause);
        }

        result.put("causes", chain);

        return result;
    }
}
