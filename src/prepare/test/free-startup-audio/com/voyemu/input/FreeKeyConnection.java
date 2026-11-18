package com.voyemu.input;

final class FreeKeyConnection {
    static boolean failEnsure, failSubmit;

    FreeKeyConnection(android.content.Context context) {}

    void ensure() {
        if (failEnsure) {
            throw new IllegalStateException("connection");
        }
    }

    void submit(FreeKeyInputSession.Event event) {
        if (failSubmit) {
            throw new IllegalStateException("submission");
        }
    }
}
