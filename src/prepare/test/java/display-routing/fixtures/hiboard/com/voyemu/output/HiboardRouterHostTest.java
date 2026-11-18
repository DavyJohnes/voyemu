package com.voyemu.output;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.view.Display;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;

public class HiboardRouterHostTest {
    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static WindowManager.LayoutParams params(String title, int memory) {
        var p = new WindowManager.LayoutParams();

        p.type = 2106;
        p.memoryType = memory;
        p.setTitle(title);

        return p;
    }

    static final String[]
            PRIMARY =
                    {
                        "HiboardBar", "HiboardDelegate", "VehicleHiBoardBarNew",
                        "VehicleHiBoardBarNewOTA", "VehicleHiboardDelegate", "DimView"
                    },
            SECONDARY =
                    {
                        "SecondHiboardBar",
                        "SecondHiboardDelegate",
                        "SecondVehicleHiboardBar",
                        "SecondDimView"
                    };

    public static void main(String[] a) {
        Context c = new Context("com.qinggan.app.hiboard", 0);
        var dm = Context.DISPLAYS;

        dm.add(new Display(0, "Built-in Screen"));
        var original = (WindowManager) c.getSystemService(Context.WINDOW_SERVICE);
        checkSourceTuples(c);

        View edge = new View(c), body = new View(c), dim = new View(c), cancelled = new View(c);
        Display screen = new Display(7, "Emulator 2D Display");
        var wm7 = checkDelayedAttach(original, edge, body, dim, cancelled, screen);
        var wm9 = checkDisplayChanges(original, edge, body, dim, wm7, screen);
        checkTitleChanges(original, body, wm9);
        checkMemoryChange(c, original, wm9);
        checkWorkerRemoval(original, edge, dim, wm9);
        checkPassthrough(c, original);

        check(
                original.attached.isEmpty() && wm7.attached.isEmpty() && wm9.attached.isEmpty(),
                "every own and passthrough view removed");
        System.out.println(
                "Hiboard lifecycle PASS: exact source tuples, delayed edge/body/dim, same args,"
                        + " duplicate, ambiguity, geometry, recreate, changed title/memory,"
                        + " removeImmediate, worker thread, foreign passthrough");
    }

    static void checkSourceTuples(Context c) {
        for (String title : PRIMARY) {
            check(
                    HiboardRouter.target(c.owner, 2106, title, 1) == 0,
                    "primary source tuple " + title);
        }

        for (String title : SECONDARY) {
            check(
                    HiboardRouter.target(c.owner, 2106, title, 2) == 1,
                    "secondary source tuple " + title);
        }

        for (String title : PRIMARY) {
            check(
                    HiboardRouter.target(c.owner, 2106, title, 2) == -1,
                    "primary title cannot migrate on memory alone");
        }

        for (String title : SECONDARY) {
            check(
                    HiboardRouter.target(c.owner, 2106, title, 1) == -1,
                    "secondary title cannot migrate on memory alone");
        }
    }

    static WindowManager checkDelayedAttach(
            WindowManager original,
            View edge,
            View body,
            View dim,
            View cancelled,
            Display screen) {
        var dm = Context.DISPLAYS;
        var pe = params("SecondVehicleHiboardBar", 2);
        var pb = params("SecondVehicleHiboardBar", 2);
        var pd = params("SecondDimView", 2);

        HiboardRouter.addView(original, edge, pe);
        HiboardRouter.addView(original, body, pb);
        HiboardRouter.addView(original, dim, pd);
        HiboardRouter.addView(original, cancelled, pd);
        HiboardRouter.removeViewImmediate(original, cancelled);
        check(
                edge.parent == null
                        && body.parent == null
                        && dim.parent == null
                        && original.adds == 0,
                "all delayed secondary windows wait without primary fallback");

        var next = params("SecondVehicleHiboardBar", 2);
        next.x = -419;
        next.y = -7;
        next.flags = 0x60028;
        next.width = 826;
        next.height = -1;
        next.dimAmount = 0.73f;
        HiboardRouter.updateViewLayout(original, body, next);

        dm.add(screen);
        var wm7 = Context.MANAGERS.get(7);
        check(
                edge.parent == wm7
                        && body.parent == wm7
                        && dim.parent == wm7
                        && cancelled.parent == null,
                "edge, original body and dim attach together; removed pending entry stays removed");
        check(
                wm7.attached.get(body) == next
                        && next.x == -419
                        && next.y == -7
                        && next.width == 826
                        && next.height == -1
                        && next.flags == 0x60028
                        && next.gravity == 8388627
                        && next.format == -3
                        && next.dimAmount == 0.73f
                        && next.memoryType == 2,
                "exact original object and every input/layout field preserved");
        HiboardRouter.addView(original, body, next);
        check(wm7.adds == 3 && wm7.updates == 1, "same view duplicate add updates once");

        return wm7;
    }

    static WindowManager checkDisplayChanges(
            WindowManager original,
            View edge,
            View body,
            View dim,
            WindowManager wm7,
            Display screen) {
        var dm = Context.DISPLAYS;
        var moved = params("SecondVehicleHiboardBar", 2);

        moved.x = -200;
        HiboardRouter.updateViewLayout(original, body, moved);
        check(
                wm7.attached.get(body) == moved && wm7.updates == 2,
                "animation update stays on actual secondary manager");

        dm.add(new Display(8, "Emulator2D"));
        check(
                edge.parent == null && body.parent == null && dim.parent == null,
                "ambiguity detaches all own secondary windows");
        dm.remove(8);
        check(body.parent == wm7, "unique display reattaches");

        screen.name = "Emulator2D spoof";
        dm.changed(7);
        check(body.parent == null, "suffix spoof rejected");
        screen.name = "Emulator2D";
        screen.width = 1280;
        dm.changed(7);
        check(body.parent == null, "wrong size rejected");
        screen.width = 1920;
        screen.density = 240;
        dm.changed(7);
        check(body.parent == null, "wrong density rejected");
        screen.density = 160;
        dm.changed(7);
        check(body.parent == wm7, "restored exact display reattaches");

        wm7.platformDisplayRemoval();
        dm.remove(7);
        check(body.parent == null, "platform already detached removed display");
        dm.add(new Display(9, "Emulator2D"));
        var wm9 = Context.MANAGERS.get(9);
        check(
                body.parent == wm9 && wm9.attached.get(body) == moved,
                "new actual display id rebinds same args");

        return wm9;
    }

    static void checkTitleChanges(WindowManager original, View body, WindowManager wm9) {
        var dm = Context.DISPLAYS;
        var primary = params("HiboardBar", 1);

        HiboardRouter.updateViewLayout(original, body, primary);
        check(
                body.parent == original && original.attached.get(body) == primary,
                "changed recognized title and memory re-evaluate slot");
        HiboardRouter.updateViewLayout(original, body, params("SecondVehicleHiboardBar", 2));
        check(body.parent == wm9, "slot can rebind secondary");
        var foreign = params("UnrelatedTitle", 2);
        HiboardRouter.updateViewLayout(original, body, foreign);
        check(
                body.parent == original && original.attached.get(body) == foreign,
                "changed unrecognized title leaves routing with original args");
        dm.changed(9);
        check(body.parent == original, "foreign title cannot resurrect route");
        HiboardRouter.removeView(original, body);
    }

    static void checkMemoryChange(Context c, WindowManager original, WindowManager wm9) {
        var dm = Context.DISPLAYS;
        View memory = new View(c);
        var memoryParams = params("SecondDimView", 2);

        HiboardRouter.addView(original, memory, memoryParams);
        memoryParams.memoryType = 1;
        HiboardRouter.updateViewLayout(original, memory, memoryParams);
        check(
                memory.parent == null,
                "tracked known title with wrong memory detaches without primary fallback");
        dm.add(new Display(10, "Emulator 2D Display"));
        check(memory.parent == null, "wrong memory stays detached with ambiguous secondary");
        memoryParams.memoryType = 2;
        HiboardRouter.updateViewLayout(original, memory, memoryParams);
        check(memory.parent == null, "corrected secondary tuple still rejects ambiguity");
        dm.remove(10);
        check(memory.parent == wm9, "corrected tuple reattaches when display becomes unique");
        HiboardRouter.removeViewImmediate(original, memory);
    }

    static void checkWorkerRemoval(WindowManager original, View edge, View dim, WindowManager wm9) {
        var dm = Context.DISPLAYS;

        Looper.main = false;
        HiboardRouter.removeViewImmediate(original, edge);
        check(edge.parent == wm9, "worker lifecycle queued on main");
        Handler.drain();
        check(
                edge.parent == null && wm9.immediates > 0,
                "immediate remove calls actual secondary immediate method");
        dm.changed(9);
        check(edge.parent == null, "removed routed view never resurrects");
        HiboardRouter.removeView(original, dim);
    }

    static void checkPassthrough(Context c, WindowManager original) {
        for (String owner : new String[] {"foreign.owner", "com.qinggan.app.hiboard"}) {
            for (String title :
                    new String[] {
                        "SecondVehicleHiboardBar suffix", "SecondVehicleHiboardBar", null
                    }) {
                for (int type : new int[] {2106, 2038}) {
                    for (int memoryType : new int[] {0, 1, 2, 3}) {
                        if (owner.equals(c.owner)
                                && type == 2106
                                && "SecondVehicleHiboardBar".equals(title)
                                && memoryType == 2) {
                            continue;
                        }

                        View v = new View(new Context(owner, 0));
                        var p = params(title, memoryType);
                        p.type = type;
                        HiboardRouter.addView(original, v, p);
                        check(
                                v.parent == original && original.attached.get(v) == p,
                                "unrelated owner/type/title/memory keeps original manager and same"
                                        + " params");
                        HiboardRouter.updateViewLayout(original, v, p);
                        HiboardRouter.removeViewImmediate(original, v);
                    }
                }
            }
        }

        View generic = new View(c);
        var genericParams = new ViewGroup.LayoutParams();
        HiboardRouter.addView(original, generic, genericParams);
        HiboardRouter.updateViewLayout(original, generic, genericParams);
        check(
                original.attached.get(generic) == genericParams,
                "non-window LayoutParams passthrough");
        HiboardRouter.removeView(original, generic);

        for (String title : PRIMARY) {
            View v = new View(c);
            var p = params(title, 1);
            HiboardRouter.addView(original, v, p);
            check(v.parent == original && original.attached.get(v) == p, "primary exact tuple");
            HiboardRouter.removeView(original, v);
        }
    }
}
