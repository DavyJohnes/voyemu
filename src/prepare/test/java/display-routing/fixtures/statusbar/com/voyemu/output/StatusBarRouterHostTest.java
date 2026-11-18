package com.voyemu.output;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.View;
import android.view.WindowManager;

public class StatusBarRouterHostTest {
    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static WindowManager.LayoutParams params(String title, int type) {
        var p = new WindowManager.LayoutParams();

        p.type = type;
        p.setTitle(title);

        return p;
    }

    public static void main(String[] a) {
        Context c = new Context("com.qinggan.systemui", 0);
        var dm = Context.DISPLAYS;

        dm.add(new Display(0, "Built-in Screen"));
        var original = (WindowManager) c.getSystemService(Context.WINDOW_SERVICE);
        View primary = new View(c), secondary = new View(c);
        var p1 = params("StatusBar1", 2118);
        var p2 = params("StatusBar2", 2118);
        StatusBarRouter.addView(original, primary, p1);
        StatusBarRouter.addView(original, secondary, p2);
        check(
                primary.parent == original && secondary.parent == null,
                "late secondary must not fall back onto primary");

        Display screen = new Display(7, "Emulator 2D Display");
        dm.add(screen);
        var wm7 = Context.MANAGERS.get(7);
        check(
                secondary.parent == wm7 && wm7.attached.get(secondary) == p2 && p2.flags == 0x1234,
                "real secondary ID and all original layout/input flags retained");
        StatusBarRouter.addView(original, secondary, p2);
        check(wm7.adds == 1 && wm7.updates == 1, "duplicate add updates original attachment");

        Display duplicate = new Display(8, "Emulator2D");
        dm.add(duplicate);
        check(secondary.parent == null, "ambiguous secondary is rejected");
        dm.remove(8);
        check(secondary.parent == wm7, "unique secondary reattached");

        screen.name = "Emulator2D spoof";
        dm.changed(7);
        check(secondary.parent == null, "suffix spoof rejected");
        screen.name = "Emulator2D";
        dm.changed(7);
        check(secondary.parent == wm7, "legacy exact name preserved");
        screen.width = 1280;
        dm.changed(7);
        check(secondary.parent == null, "wrong geometry rejected");
        screen.width = 1920;
        screen.density = 240;
        dm.changed(7);
        check(secondary.parent == null, "wrong density rejected");
        screen.density = 160;
        dm.changed(7);
        check(secondary.parent == wm7, "geometry restore reattached");

        wm7.platformDisplayRemoval();
        dm.remove(7);
        check(secondary.parent == null, "already removed platform window handled");
        dm.add(new Display(9, "Emulator 2D Display"));
        var wm9 = Context.MANAGERS.get(9);
        check(secondary.parent == wm9, "display recreation uses new actual ID");

        Looper.main = false;
        StatusBarRouter.removeView(original, secondary);
        check(secondary.parent == wm9, "worker removal stays on main Looper");
        Handler.drain();
        check(
                secondary.parent == null && wm9.removes == 1,
                "remove uses attached secondary manager");
        dm.changed(9);
        check(secondary.parent == null, "removed entry cannot resurrect");
        StatusBarRouter.removeView(original, primary);
        check(primary.parent == null, "primary lifecycle retained");

        for (String owner : new String[] {"other", "com.qinggan.systemui"}) {
            for (String title :
                    new String[] {"StatusBar2 suffix", "NavigationBar1", "StatusBar2"}) {
                for (int type : new int[] {2118, 2038}) {
                    if (owner.equals("com.qinggan.systemui")
                            && title.equals("StatusBar2")
                            && type == 2118) {
                        continue;
                    }

                    View foreign = new View(new Context(owner, 0));
                    var p = params(title, type);
                    StatusBarRouter.addView(original, foreign, p);
                    check(foreign.parent == original, "unrelated window must use original manager");
                    StatusBarRouter.removeView(original, foreign);
                }
            }
        }

        check(
                original.attached.isEmpty() && wm7.attached.isEmpty() && wm9.attached.isEmpty(),
                "all own views removed");
        System.out.println(
                "routing lifecycle PASS:"
                        + " late/recreate/ambiguous/geometry/name/input-flags/main-thread/remove;"
                        + " unrelated windows unchanged");
    }
}
