package com.voyemu.navi;

import android.content.Context;

public final class OwnContext {
    public static Context value;
    public static int calls;

    public static Context get() {
        calls++;

        return value;
    }
}
