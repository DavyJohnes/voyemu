package com.voyemu.api;

public final class FailureMetadataTest {
    public static void main(String[] args) {
        IllegalStateException cause =
                new IllegalStateException("UiAutomationService secret-token already registered!");

        cause.setStackTrace(
                new StackTraceElement[] {
                    new StackTraceElement("untrusted.application.UserData", "secret", "private", 1),
                    new StackTraceElement(
                            "android.app.UiAutomation", "connect", "UiAutomation.java", 241)
                });
        Throwable wrapped =
                new java.lang.reflect.InvocationTargetException(
                        cause, "do not export private message");
        String value = fields(wrapped);

        if (!value.contains("automation_service_already_registered")
                || !value.contains("UiAutomation")
                || !value.contains("connect")
                || value.contains("secret")
                || value.contains("private")) {
            throw new AssertionError(value);
        }

        Throwable chain = cause;

        for (int i = 0; i < 20; ++i) {
            chain = new RuntimeException("private", chain);
        }

        java.util.List<?> causes = (java.util.List<?>) FailureMetadata.fields(chain).get("causes");

        if (causes.size() != 4) {
            throw new AssertionError("unbounded causes");
        }

        Throwable cycle =
                new RuntimeException("private") {
                    @Override
                    public synchronized Throwable getCause() {
                        return this;
                    }
                };

        if (((java.util.List<?>) FailureMetadata.fields(cycle).get("causes")).size() != 1) {
            throw new AssertionError("cycle");
        }

        System.out.println(
                "bounded framework cause diagnostics exclude messages and private application"
                        + " frames");
    }

    private static String fields(Throwable error) {
        return FailureMetadata.fields(error).toString();
    }
}
