import com.voyemu.egl.ScopedEgl;

import java.util.Arrays;

public class ScopedEglTest {
    public interface E10 {
        boolean eglChooseConfig(
                Object display, int[] attrs, Object[] configs, int size, int[] count);

        int other(int x);
    }

    public interface E11 extends E10 {
        int extra();
    }

    public static class Fake implements E11 {
        int calls;
        boolean fail, throwsError, retryFail, retryThrow;
        int firstCount;
        int[] last, firstAttrs;
        Object token = new Object(), firstToken = new Object();

        public boolean eglChooseConfig(Object d, int[] a, Object[] c, int s, int[] n) {
            calls++;

            if (throwsError || (retryThrow && calls == 2)) {
                throw new IllegalStateException("sentinel");
            }

            last = a == null ? null : a.clone();

            if (calls == 1) {
                firstAttrs = last;
            }

            if (fail || (retryFail && calls == 2)) {
                return false;
            }

            storeCount(n);
            storeConfig(c);

            return true;
        }

        private void storeCount(int[] count) {
            if (count != null && count.length > 0) {
                count[0] = (calls % 2 == 1 ? firstCount : 1);
            }
        }

        private void storeConfig(Object[] configs) {
            if (configs != null && configs.length > 0) {
                configs[0] = calls % 2 == 1 ? firstToken : token;
            }
        }

        public int other(int x) {
            return x + 1;
        }

        public int extra() {
            return 7;
        }
    }

    public static class TenOnly implements E10 {
        public boolean eglChooseConfig(Object d, int[] a, Object[] c, int s, int[] n) {
            if (n != null && n.length > 0) {
                n[0] = 1;
            }

            return true;
        }

        public int other(int x) {
            return x + 1;
        }
    }

    static void ck(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    static int[] attrs() {
        return new int[] {0x3024, 4, 0x3032, 1, 0x3031, 4, 0x3038};
    }

    public static void main(String[] z) {
        TenOnly ten = new TenOnly();
        Object tenWrapped = ScopedEgl.wrap("com.baidu.naviauto", ten, E10.class, E11.class);

        ck(
                tenWrapped != ten
                        && tenWrapped instanceof E10
                        && !(tenWrapped instanceof E11)
                        && ((E10) tenWrapped).other(3) == 4);
        Fake f = new Fake();
        ck(ScopedEgl.wrap("other", f, E10.class, E11.class) == f);
        E11 p = (E11) ScopedEgl.wrap("com.baidu.naviauto", f, E10.class, E11.class);
        ck(
                ScopedEgl.wrap("com.baidu.naviauto", f, E10.class, E11.class) == p
                        && ScopedEgl.wrap("other", f, E10.class, E11.class) == f
                        && ScopedEgl.wrap("com.baidu.naviauto", null, E10.class, E11.class)
                                == null);
        ck(
                p instanceof E10
                        && p != f
                        && p.extra() == 7
                        && p.other(3) == 4
                        && p.equals(p)
                        && !p.equals(f));
        int[] a = attrs(), before = a.clone(), n = {-1};
        verifyQueryAndFetch(f, p, a, before, n);
        verifyRetryFailures(f, p, a, before, n);
        verifyFallbackInputs(f, p, a, n);
        System.out.println(
                "PASS scoped delegation, genuine retry, query/fetch, no mutation, negative and"
                        + " exception cases");
    }

    private static void verifyQueryAndFetch(Fake f, E11 p, int[] a, int[] before, int[] n) {
        ck(
                p.eglChooseConfig(null, a, null, 0, n)
                        && f.calls == 2
                        && n[0] == 1
                        && Arrays.equals(a, before)
                        && Arrays.equals(f.firstAttrs, before)
                        && f.last[3] == 0
                        && f.last[5] == 0);
        f.calls = 0;
        Object[] cfg = new Object[1];
        n[0] = -1;
        ck(p.eglChooseConfig(null, a, cfg, 1, n) && f.calls == 2 && cfg[0] == f.token && n[0] == 1);
        f.calls = 0;
        f.firstCount = 2;
        ck(p.eglChooseConfig(null, a, null, 0, n) && f.calls == 1);
        f.calls = 0;
        f.firstCount = 0;
        int[] other = {0x3032, 1, 0x3031, 2, 0x3038};
        ck(p.eglChooseConfig(null, other, null, 0, n) && f.calls == 1);
    }

    private static void verifyRetryFailures(Fake f, E11 p, int[] a, int[] before, int[] n) {
        f.calls = 0;
        f.fail = true;
        ck(!p.eglChooseConfig(null, a, null, 0, n) && f.calls == 1);
        f.fail = false;
        f.calls = 0;
        f.retryFail = true;
        ck(!p.eglChooseConfig(null, a, null, 0, n) && f.calls == 2 && Arrays.equals(a, before));
        f.retryFail = false;
        f.calls = 0;
        f.retryThrow = true;

        try {
            p.eglChooseConfig(null, a, null, 0, n);
            throw new AssertionError();
        } catch (IllegalStateException e) {
            ck(e.getMessage().equals("sentinel") && f.calls == 2 && Arrays.equals(a, before));
        }

        f.retryThrow = false;
    }

    private static void verifyFallbackInputs(Fake f, E11 p, int[] a, int[] n) {
        f.calls = 0;
        ck(p.eglChooseConfig(null, new int[] {0x3032}, null, 0, n) && f.calls == 1);
        f.calls = 0;
        ck(
                p.eglChooseConfig(
                                null,
                                new int[] {0x3032, 1, 0x3032, 1, 0x3031, 4, 0x3038},
                                null,
                                0,
                                n)
                        && f.calls == 1);
        f.calls = 0;
        int[] duplicateSamples = {0x3032, 1, 0x3031, 4, 0x3031, 4, 0x3038};
        int[] duplicateBefore = duplicateSamples.clone();
        ck(
                p.eglChooseConfig(null, duplicateSamples, null, 0, n)
                        && f.calls == 1
                        && Arrays.equals(duplicateSamples, duplicateBefore));
        f.calls = 0;
        int[] terminated = {0x3032, 1, 0x3031, 4, 0x3038, 0x3031, 99};
        int[] terminatedBefore = terminated.clone();
        ck(
                p.eglChooseConfig(null, terminated, null, 0, n)
                        && f.calls == 2
                        && Arrays.equals(terminated, terminatedBefore)
                        && f.last[6] == 99);
        f.calls = 0;
        ck(p.eglChooseConfig(null, new int[] {0x3032, 1, 0x3031, 4}, null, 0, n) && f.calls == 1);
        f.calls = 0;
        ck(p.eglChooseConfig(null, null, null, 0, n) && f.calls == 1);
        f.calls = 0;
        ck(p.eglChooseConfig(null, a, null, 0, null) && f.calls == 1);
        f.throwsError = true;

        try {
            p.eglChooseConfig(null, a, null, 0, n);
            throw new AssertionError();
        } catch (IllegalStateException e) {
            ck(e.getMessage().equals("sentinel"));
        }
    }
}
