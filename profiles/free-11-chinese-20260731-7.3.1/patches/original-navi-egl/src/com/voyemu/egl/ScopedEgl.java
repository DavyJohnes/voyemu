package com.voyemu.egl;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.Map;

public final class ScopedEgl {
    private static final IdentityHashMap<Object, Map<Class<?>, Object>> CACHE =
            new IdentityHashMap<>();
    private static final int END = 0x3038, BUFFERS = 0x3032, SAMPLES = 0x3031;

    public static synchronized Object wrap(
            String pkg, Object original, Class<?> egl10, Class<?> egl11) {
        if (!"com.baidu.naviauto".equals(pkg) || original == null || !egl10.isInstance(original)) {
            return original;
        }

        Class<?> surface = egl11.isInstance(original) ? egl11 : egl10;
        Map<Class<?>, Object> per = CACHE.get(original);

        if (per == null) {
            per = new HashMap<Class<?>, Object>();
            CACHE.put(original, per);
        }

        Object existing = per.get(surface);

        if (existing != null) {
            return existing;
        }

        Object wrapped =
                Proxy.newProxyInstance(
                        surface.getClassLoader(),
                        new Class<?>[] {surface},
                        new InvocationHandler() {
                            // Compares identity on purpose: the same Binder, token or instance,
                            // never an equal one.
                            @SuppressWarnings("ReferenceEquality")
                            @Override
                            public Object invoke(Object proxy, Method m, Object[] args)
                                    throws Throwable {
                                if (m.getDeclaringClass() == Object.class) {
                                    switch (m.getName()) {
                                        case "equals":
                                            return proxy == args[0];
                                        case "hashCode":
                                            return System.identityHashCode(proxy);
                                        case "toString":
                                            return "ScopedEgl(" + original + ")";
                                        default:
                                            throw new AssertionError();
                                    }
                                }

                                Object first = call(m, original, args);

                                if (!m.getName().equals("eglChooseConfig")
                                        || args == null
                                        || args.length != 5
                                        || !Boolean.TRUE.equals(first)
                                        || !(args[1] instanceof int[])
                                        || !(args[4] instanceof int[])) {
                                    return first;
                                }

                                int[] attrs = (int[]) args[1], count = (int[]) args[4];

                                if (count.length == 0 || count[0] != 0) {
                                    return first;
                                }

                                int[] copy = relaxedAttributes(attrs);

                                if (copy == null) {
                                    return first;
                                }

                                Object[] retry = args.clone();
                                retry[1] = copy;

                                return call(m, original, retry);
                            }
                        });
        per.put(surface, wrapped);

        return wrapped;
    }

    private static int[] relaxedAttributes(int[] attrs) {
        int bi = -1, si = -1;
        boolean end = false;

        for (int i = 0; i < attrs.length; ) {
            int k = attrs[i];

            if (k == END) {
                end = true;
                break;
            }

            if (i + 1 >= attrs.length) {
                return null;
            }

            if (k == BUFFERS) {
                if (bi >= 0) {
                    return null;
                }

                bi = i + 1;
            }

            if (k == SAMPLES) {
                if (si >= 0) {
                    return null;
                }

                si = i + 1;
            }

            i += 2;
        }

        if (!end || bi < 0 || si < 0 || attrs[bi] != 1 || attrs[si] != 4) {
            return null;
        }

        int[] copy = attrs.clone();
        copy[bi] = 0;
        copy[si] = 0;

        return copy;
    }

    private static Object call(Method m, Object target, Object[] args) throws Throwable {
        try {
            return m.invoke(target, args);
        } catch (InvocationTargetException e) {
            throw e.getCause();
        }
    }
}
