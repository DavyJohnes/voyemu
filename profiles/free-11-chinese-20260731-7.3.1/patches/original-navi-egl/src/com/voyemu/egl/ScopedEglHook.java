package com.voyemu.egl;

import android.content.Context;

import com.voyemu.navi.OwnContext;

public final class ScopedEglHook {
    public static Object get(Object original) {
        try {
            return get(
                    original,
                    Class.forName("javax.microedition.khronos.egl.EGL10"),
                    Class.forName("javax.microedition.khronos.egl.EGL11"));
        } catch (ClassNotFoundException | LinkageError | SecurityException e) {
            return original;
        }
    }

    public static Object get(Object original, Class<?> egl10, Class<?> egl11) {
        if (original == null) {
            return null;
        }

        Context own = OwnContext.get();

        if (own == null) {
            return original;
        }

        return ScopedEgl.wrap(own.getPackageName(), original, egl10, egl11);
    }
}
