package com.voyemu.output;

import android.app.ActivityManager;
import android.app.Dialog;
import android.app.IActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.inputmethodservice.InputMethodService;
import android.os.IBinder;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;

import com.voyemu.input.ImeDisplayRouter;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

public final class DisplayRoutingTest {
    private static int checks;

    private static void check(boolean value, String message) {
        checks++;

        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static final class Manager implements WindowManager {
        int adds, removes;
        boolean reject;
        ViewGroup.LayoutParams last;

        @Override
        public void addView(View view, ViewGroup.LayoutParams params) {
            if (reject) {
                throw new IllegalStateException("window admission failed");
            }

            adds++;
            last = params;
            view.parent = this;
        }

        @Override
        public void updateViewLayout(View view, ViewGroup.LayoutParams params) {
            last = params;
        }

        @Override
        public void removeView(View view) {
            removes++;
            view.parent = null;
        }

        @Override
        public void removeViewImmediate(View view) {
            removeView(view);
        }
    }

    private static IActivityManager.StackInfo stack(
            int display, boolean visible, int position, Object top) {
        return new IActivityManager.StackInfo(display, visible, position, top);
    }

    private static void topActivitySelection() {
        DisplayManager displays = new DisplayManager();

        displays.add(0, "Built-in");
        displays.add(3, null);
        displays.add(4, "Unrelated");
        displays.add(7, "Emulator2D secondary");
        Context context = new Context("launcher", displays, new HashMap<>());
        ActivityManager.service = new IActivityManager();
        int before = ActivityManager.calls;
        check(DisplayTasks.getTopAppInfo(context, -1, 1) == null, "reject slot");
        check(DisplayTasks.getTopAppInfo(context, 0, 3) == null, "reject format");
        check(ActivityManager.calls == before, "invalid request performs no Binder lookup");
        ComponentName primary = new ComponentName("main", "Main");
        ComponentName secondary = new ComponentName("second", "Second");
        ActivityManager.service.stacks =
                Arrays.asList(
                        stack(0, true, 10, primary),
                        stack(7, true, 1, new ComponentName("old", "Old")),
                        stack(7, true, 2, secondary),
                        stack(7, false, 99, new ComponentName("hidden", "Hidden")),
                        stack(7, true, 100, "not a component"),
                        stack(4, true, 999, new ComponentName("foreign", "Foreign")));
        check("main".equals(DisplayTasks.getTopAppInfo(context, 0, 1)), "primary package");
        check(
                "second".equals(DisplayTasks.getTopAppInfo(context, 1, 1)),
                "highest visible package");
        check("Second".equals(DisplayTasks.getTopAppInfo(context, 1, 2)), "class format");
        check("second/Second".equals(DisplayTasks.getTopAppInfo(context, 1, 4)), "flat format");
        displays.live.remove(7);
        before = ActivityManager.calls;
        check(DisplayTasks.getTopAppInfo(context, 1, 1) == null, "missing named secondary");
        check(ActivityManager.calls == before, "missing display avoids stack lookup");
        check(
                DisplayTasks.getTopAppInfo(new Context("launcher", null, new HashMap<>()), 1, 1)
                        == null,
                "missing display service");
    }

    private static void topActivityFailures() {
        DisplayManager displays = new DisplayManager();
        Context context = new Context("launcher", displays, new HashMap<>());

        ActivityManager.service.stacks = "not a list";
        check(DisplayTasks.getTopAppInfo(context, 0, 1) == null, "invalid service response");
        ActivityManager.service.stacks = Arrays.asList(stack(0, true, 1, null));
        check(DisplayTasks.getTopAppInfo(context, 0, 1) == null, "no component");
        ActivityManager.service.stacks = Arrays.asList(new Object());
        check(DisplayTasks.getTopAppInfo(context, 0, 1) == null, "reflection mismatch");
        ActivityManager.service.fail = true;
        check(DisplayTasks.getTopAppInfo(context, 0, 1) == null, "remote reflection failure");
        ActivityManager.service.fail = false;
    }

    private static final class Keyboard {
        final DisplayManager displays = new DisplayManager();
        final Manager primary = new Manager(), secondary = new Manager();
        final InputMethodService service;
        final View decor;
        final WindowManager.LayoutParams attributes = new WindowManager.LayoutParams();
        final IBinder originalToken = new IBinder() {};
        final EditorInfo editor = new EditorInfo();
        final Window window;

        Keyboard(boolean attached) {
            displays.add(0, "Built-in");
            displays.add(7, "Emulator2D secondary");
            Map<Integer, WindowManager> managers = new HashMap<>();
            managers.put(0, primary);
            managers.put(7, secondary);
            service = new InputMethodService(displays, managers);
            decor = new View(service);
            decor.parent = attached ? primary : null;
            attributes.type = 2011;
            attributes.flags = 16;
            attributes.token = originalToken;
            window = new Window(primary, attributes, decor);
            service.dialog = new Dialog(primary, window);
            editor.packageName = "editor";
            ActivityManager.service = new IActivityManager();
            focus(7, "editor");
        }

        void focus(int display, String owner) {
            ActivityManager.service.focused =
                    stack(display, true, 1, new ComponentName(owner, "Editor"));
        }
    }

    private static void keyboardMoveAndRestore() {
        Keyboard keyboard = new Keyboard(true);

        ImeDisplayRouter.route(keyboard.service, keyboard.editor);
        check(keyboard.primary.removes == 1 && keyboard.secondary.adds == 1, "move attached decor");
        check(
                keyboard.window.manager() == keyboard.secondary
                        && keyboard.service.dialog.manager() == keyboard.secondary,
                "both window managers follow display");
        check(
                keyboard.attributes.type == WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                        && keyboard.attributes.token == null
                        && keyboard.attributes.flags
                                == (16 | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE),
                "secondary overlay attributes");
        ImeDisplayRouter.route(keyboard.service, keyboard.editor);
        check(keyboard.secondary.adds == 1, "same display is unchanged");
        keyboard.focus(7, "foreign");
        ImeDisplayRouter.route(keyboard.service, keyboard.editor);
        check(
                keyboard.secondary.removes == 1 && keyboard.primary.adds == 1,
                "foreign editor restores primary");
        check(
                keyboard.attributes.type == 2011
                        && keyboard.attributes.token == keyboard.originalToken
                        && keyboard.attributes.flags == 16,
                "restore exact primary attributes");
        ImeDisplayRouter.destroy(keyboard.service);
        ImeDisplayRouter.route(keyboard.service, null);
        check(keyboard.primary.adds == 1, "destroy removes routing state");
    }

    private static void keyboardFallbacks() {
        Keyboard keyboard = new Keyboard(false);

        keyboard.focus(4, "editor");
        ImeDisplayRouter.route(keyboard.service, keyboard.editor);
        check(keyboard.window.manager() == keyboard.primary, "missing display stays primary");
        keyboard.displays.add(4, "Foreign display");
        ImeDisplayRouter.route(keyboard.service, keyboard.editor);
        check(keyboard.window.manager() == keyboard.primary, "unnamed secondary stays primary");
        ActivityManager.service.fail = true;
        ImeDisplayRouter.route(keyboard.service, keyboard.editor);
        check(keyboard.window.manager() == keyboard.primary, "focus lookup failure stays primary");
        ActivityManager.service.fail = false;
        keyboard.focus(7, "editor");
        ImeDisplayRouter.route(keyboard.service, keyboard.editor);
        check(
                keyboard.secondary.adds == 0 && keyboard.primary.removes == 0,
                "unattached decor is not removed or added");
        check(keyboard.window.manager() == keyboard.secondary, "unattached manager still routes");
        ImeDisplayRouter.destroy(keyboard.service);
    }

    private static void keyboardAdmissionFailure() {
        Keyboard keyboard = new Keyboard(true);

        keyboard.secondary.reject = true;

        try {
            ImeDisplayRouter.route(keyboard.service, keyboard.editor);
            throw new AssertionError("window failure must escape");
        } catch (IllegalStateException expected) {
            check(
                    "window admission failed".equals(expected.getMessage()),
                    "preserve unchecked failure");
        }

        check(
                keyboard.primary.removes == 1 && keyboard.secondary.adds == 0,
                "preserve effects before failed admission");
        ImeDisplayRouter.destroy(keyboard.service);
    }

    public static void main(String[] args) {
        topActivitySelection();
        topActivityFailures();
        keyboardMoveAndRestore();
        keyboardFallbacks();
        keyboardAdmissionFailure();
        System.out.println("PASS " + checks + " DisplayTasks and IME behavior assertions");
    }
}
