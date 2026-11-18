package com.voyemu.prepare;

final class Failure extends RuntimeException {
    final String code;
    final int exitCode;

    Failure(String code, String message, int exitCode) {
        super(message);
        this.code = code;
        this.exitCode = exitCode;
    }

    static void require(boolean condition, String code, String message, int exitCode) {
        if (!condition) {
            throw new Failure(code, message, exitCode);
        }
    }
}
