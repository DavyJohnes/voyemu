package com.voyemu.output;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

import com.qinggan.app.launcher.widget.AllAppBarView;

import java.lang.reflect.Method;
import java.util.HashMap;
import java.util.Map;

public final class WindowRouterTest {
    static int checks;

    static void check(boolean b, String s) {
        checks++;

        if (!b) {
            throw new AssertionError(s);
        }
    }

    static class Manager implements WindowManager {
        int adds, updates, removes;
        ViewGroup.LayoutParams last;

        public void addView(View v, ViewGroup.LayoutParams p) {
            adds++;
            last = p;
        }

        public void updateViewLayout(View v, ViewGroup.LayoutParams p) {
            updates++;
            last = p;
        }

        public void removeView(View v) {
            removes++;
        }

        public void removeViewImmediate(View v) {
            removes++;
        }
    }

    static WindowManager.LayoutParams params(int type, String title) {
        WindowManager.LayoutParams p = new WindowManager.LayoutParams();

        p.type = type;
        p.setTitle(title);
        p.width = 99;
        p.flags = 17;
        p.x = 7;

        return p;
    }

    static int route(View v, ViewGroup.LayoutParams p) throws Exception {
        Method m =
                WindowRouter.class.getDeclaredMethod(
                        "target", View.class, ViewGroup.LayoutParams.class);

        m.setAccessible(true);

        return (Integer) m.invoke(null, v, p);
    }

    static View view(Context c, Object tag) {
        View v = new AllAppBarView(c);

        v.setTag(tag);

        return v;
    }

    public static void main(String[] args) throws Exception {
        DisplayManager d = new DisplayManager();

        d.add(0, "Built-in");
        Map<Integer, WindowManager> ms = new HashMap<>();
        Manager main = new Manager(),
                second = new Manager(),
                third = new Manager(),
                original = new Manager();
        ms.put(0, main);
        ms.put(7, second);
        ms.put(9, third);
        Context c = new Context("com.qinggan.app.launcher", d, ms);
        WindowManager.LayoutParams p = params(2106, "AllApp");
        check(route(view(c, "Second"), p) == 1, "AllApp secondary must route to secondary slot");
        check(route(view(c, ""), p) == 0, "AllApp empty tag must route primary");

        for (Object tag : new Object[] {null, "second", "Main", "Second ", Integer.valueOf(1)}) {
            check(route(view(c, tag), p) == -1, "unknown tag fallback " + tag);
        }

        check(route(view(new Context("foreign", d, ms), "Second"), p) == -1, "owner guard");
        check(route(view(c, "Second"), params(2110, "AllApp")) == -1, "type guard");
        check(route(view(c, "Second"), params(2106, "allapp")) == -1, "title guard");
        View wrong = new View(c);
        wrong.setTag("Second");
        check(route(wrong, p) == -1, "exact class guard");
        check(route(view(c, "Second"), new ViewGroup.LayoutParams()) == -1, "non-window guard");
        check(
                WindowRouter.target("com.qinggan.systemui", 2118, "StatusBar2") == 1,
                "old statusbar route");
        check(
                WindowRouter.target("com.qinggan.systemui", 2026, "StatusBar1") == 0,
                "NXP DISPLAY_OVERLAY primary statusbar");
        check(
                WindowRouter.target("com.qinggan.systemui", 2026, "StatusBar2") == 1,
                "NXP DISPLAY_OVERLAY secondary statusbar");
        check(
                WindowRouter.target("com.android.systemui", 2026, "StatusBar2") == -1,
                "stock Android overlay must not route as OEM");
        check(
                WindowRouter.target("com.qinggan.systemui", 2026, "OtherOverlay") == -1,
                "unrelated OEM overlays unchanged");
        check(
                WindowRouter.target("com.qinggan.systemui", 2025, "StatusBar2") == -1,
                "unrecognized OEM type unchanged");
        check(
                WindowRouter.target("com.qinggan.app.launcher", 2106, "SecondScreenNavigationBar")
                        == 1,
                "old navigation route");
        View v = view(c, "Second");
        WindowRouter.addView(original, v, p);
        check(original.adds == 0 && second.adds == 0, "defer missing secondary");
        WindowManager.LayoutParams updated = params(2106, "AllApp");
        updated.width = 123;
        WindowRouter.updateViewLayout(original, v, updated);
        d.add(7, "Emulator2D virtual");
        check(second.adds == 1, "attach on display added");
        WindowManager.LayoutParams copied = (WindowManager.LayoutParams) second.last;
        check(
                copied.type == 2008 && copied.width == 123 && copied.flags == 17 && copied.x == 7,
                "adapt type preserve layout");
        check(updated.type == 2106, "do not mutate OEM params");
        WindowRouter.updateViewLayout(original, v, p);
        check(second.updates == 1, "attached update");
        d.remove(7);
        check(second.removes == 1, "remove old display");
        d.add(9, "Emulator2D recreated");
        check(third.adds == 1, "reattach recreated id");
        WindowRouter.removeViewImmediate(original, v);
        check(third.removes == 1 && original.removes == 0, "remove current routed display");
        d.add(10, "Emulator2D future");
        check(third.adds == 1, "removed entry not resurrected");
        View canceled = view(c, "Second");
        d.remove(9);
        d.live.remove(10);
        WindowRouter.addView(original, canceled, p);
        WindowRouter.removeView(original, canceled);
        d.add(7, "Emulator2D again");
        check(second.adds == 1, "remove deferred entry prevents late attachment");
        View primary = view(c, "");
        WindowRouter.addView(original, primary, p);
        check(main.adds == 1, "primary attaches main");
        WindowRouter.removeView(original, primary);
        check(main.removes == 1, "primary remove");
        View fallback = view(c, "unsupported");
        WindowRouter.addView(original, fallback, p);
        check(original.adds == 1 && original.last == p, "unknown routes unchanged");
        WindowRouter.updateViewLayout(original, fallback, p);
        WindowRouter.removeView(original, fallback);
        check(original.updates == 1 && original.removes == 1, "unknown update remove fallback");
        System.out.println("PASS " + checks + " assertions against actual WindowRouter source");
    }
}
