import android.app.ActivityThread;
import android.app.Application;
import android.app.Binding;
import android.app.LoadedApk;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;

import com.voyemu.egl.ScopedEglHook;
import com.voyemu.navi.OwnContext;

public class ScopedEglHookTest {
    public interface E10 {
        int f();
    }

    public interface E11 extends E10 {}

    public static class D implements E10 {
        public int f() {
            return 9;
        }
    }

    static void ck(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static void configure(Context c) {
        c.p = "com.baidu.naviauto";
        c.info = new ApplicationInfo();
        c.info.uid = 10050;
        c.info.packageName = c.p;
        c.pm = new PackageManager();
        c.pm.packages = new String[] {c.p};
    }

    public static void main(String[] x) {
        D d = new D();

        ck(ScopedEglHook.get(null, E10.class, E11.class) == null);
        ck(OwnContext.get() == null);
        ActivityThread.p = "com.baidu.naviauto";
        ck(OwnContext.get() == null);
        ActivityThread.current = new ActivityThread();
        Context system = new Context(), own = new Context();
        configure(own);
        system.own = own;
        ActivityThread.current.system = system;
        Binding binding = new Binding();
        binding.info = new LoadedApk();
        binding.info.info = own.info;
        ActivityThread.current.mBoundApplication = binding;
        ck(OwnContext.get() == own);
        Object w = ScopedEglHook.get(d, E10.class, E11.class);
        ck(w != d && w == ScopedEglHook.get(d, E10.class, E11.class) && ((E10) w).f() == 9);
        system.own = system;
        ck(OwnContext.get() == null);
        system.own = own;
        own.info.uid = 10051;
        ck(OwnContext.get() == null);
        own.info.uid = 10050;
        own.info.packageName = "other";
        ck(OwnContext.get() == null);
        own.info.packageName = own.p;
        own.pm.packages = new String[] {own.p, "other"};
        ck(OwnContext.get() == null);
        own.pm.packages = null;
        ck(OwnContext.get() == null);
        own.pm.packages = new String[] {own.p};
        ActivityThread.current.mBoundApplication = null;
        ck(OwnContext.get() == null);
        ActivityThread.current.mBoundApplication = binding;
        android.os.Process.uid = 1000;
        ck(OwnContext.get() == null);
        android.os.Process.uid = 10050;
        ActivityThread.a = new Application();
        configure(ActivityThread.a);
        ck(OwnContext.get() == ActivityThread.a);
        ActivityThread.a.p = "other";
        ck(OwnContext.get() == null);
        ActivityThread.a = null;
        ActivityThread.p = "android";
        ck(OwnContext.get() == null);
        ActivityThread.p = "com.baidu.naviauto";
        ActivityThread.fail = true;
        ck(OwnContext.get() == null);
        System.out.println(
                "PASS early/ready context, uid/package/single UID isolation, no system context and"
                        + " fail closed");
    }
}
