package com.voyemu.api;

import android.accessibilityservice.AccessibilityServiceInfo;
import android.app.UiAutomation;
import android.graphics.Rect;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Build;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.InputStream;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/**
 * Closed owner-authenticated API28 and profile-scoped API30 tree observer; no shell or arbitrary
 * code endpoint.
 */
public final class ObserverMain {
    private static final int MAX_NODES = 12000, MAX_DEPTH = 80, MAX_BYTES = 6 * 1024 * 1024;
    private static final ThreadLocal<Long> DEADLINES = new ThreadLocal<>();
    private static final ThreadLocal<int[]> SURFACE_SIZE = new ThreadLocal<>();
    private static final ThreadLocal<int[]> COUNTS = new ThreadLocal<>();
    private static final java.util.concurrent.atomic.AtomicLong DISPLAY_GENERATION =
            new java.util.concurrent.atomic.AtomicLong();
    private static final String EPOCH = UUID.randomUUID().toString();
    private static final EventJournal JOURNAL = new EventJournal(2048, 1024 * 1024);
    private static final ViewServer VIEW_SERVER = new ViewServer();
    private static final java.util.concurrent.Semaphore READERS =
            new java.util.concurrent.Semaphore(4);
    private static final SnapshotGate SNAPSHOTS = new SnapshotGate();
    private static final java.util.concurrent.ConcurrentHashMap<String, Long> SUBSCRIPTIONS =
            new java.util.concurrent.ConcurrentHashMap<>();
    private static volatile long sourceDeadline;
    private static volatile boolean running = true;
    private static String selectedProfile = "";
    private static HandlerThread callbacks;
    private static UiAutomation automation;
    private static Method disconnect;

    private ObserverMain() {}

    private static void check(boolean condition, String reason) {
        if (!condition) {
            throw new IllegalStateException(reason);
        }

        if (Thread.currentThread().isInterrupted()
                || (DEADLINES.get() != null && SystemClock.uptimeMillis() >= DEADLINES.get())) {
            throw new IllegalStateException("deadline");
        }
    }

    private static String boot() throws Exception {
        try (InputStream in = new FileInputStream("/proc/sys/kernel/random/boot_id")) {
            byte[] bytes = new byte[64];
            int size = in.read(bytes);
            String id = new String(bytes, 0, size, StandardCharsets.UTF_8).trim();
            check(
                    id.matches("[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}"),
                    "boot_identity");

            return id;
        }
    }

    private static JSONArray bounds(Rect value) throws Exception {
        return new JSONArray(new int[] {value.left, value.top, value.right, value.bottom});
    }

    private static JSONArray bounds(int[] value) throws Exception {
        return new JSONArray(value);
    }

    private static void phase(GuestPhase.Phase phase) {
        GuestPhase.mark(phase, SystemClock.uptimeMillis(), -1);
    }

    private static synchronized void connect() throws Exception {
        if (automation != null) {
            return;
        }

        Class<?> connectionType = Class.forName("android.app.IUiAutomationConnection");
        Object connection =
                Class.forName("android.app.UiAutomationConnection")
                        .getDeclaredConstructor()
                        .newInstance();
        Constructor<UiAutomation> ctor =
                UiAutomation.class.getDeclaredConstructor(Looper.class, connectionType);
        ctor.setAccessible(true);
        UiAutomation candidate = ctor.newInstance(callbacks.getLooper(), connection);
        Method start = UiAutomation.class.getDeclaredMethod("connect", int.class);
        start.setAccessible(true);
        // API24 flag1 preserves existing accessibility services and their behavior.
        Method candidateDisconnect = UiAutomation.class.getDeclaredMethod("disconnect");
        candidateDisconnect.setAccessible(true);
        boolean connected = false;

        try {
            start.invoke(candidate, UiAutomation.FLAG_DONT_SUPPRESS_ACCESSIBILITY_SERVICES);
            AccessibilityServiceInfo info = candidate.getServiceInfo();
            check(info != null, "accessibility_service");
            info.flags |=
                    AccessibilityServiceInfo.FLAG_REPORT_VIEW_IDS
                            | AccessibilityServiceInfo.FLAG_RETRIEVE_INTERACTIVE_WINDOWS
                            | AccessibilityServiceInfo.FLAG_INCLUDE_NOT_IMPORTANT_VIEWS;
            candidate.setOnAccessibilityEventListener(event -> {
                try {
                    // Do not copy text, descriptions, Parcelable data, or source nodes from
                    // callbacks.
                    JSONObject safe =
                            new JSONObject()
                                    .put("type", event.getEventType())
                                    .put("window_id", event.getWindowId())
                                    .put("package", Geometry.text(event.getPackageName(), false))
                                    .put("event_uptime_ms", event.getEventTime())
                                    .put("clock_domain", "android_uptime_ms")
                                    .put("coverage", "accessibility_events_default_display");
                    JOURNAL.append(SystemClock.uptimeMillis(), safe.toString());

                } catch (JSONException error) {
                    JOURNAL.append(SystemClock.uptimeMillis(), "{\"type\":\"callback_data_gap\"}");
                }
            });
            candidate.setServiceInfo(info);
            automation = candidate;
            disconnect = candidateDisconnect;
            connected = true;
        } finally {
            if (!connected) {
                releaseCandidate(candidate, candidateDisconnect, connectionType, connection);
            }
        }
    }

    private static void releaseCandidate(
            UiAutomation candidate,
            Method candidateDisconnect,
            Class<?> connectionType,
            Object connection) {
        try {
            candidateDisconnect.invoke(candidate);
        } catch (ReflectiveOperationException ignored) {
            // Best-effort release; the original connection failure propagates.
        }

        // UiAutomation.connect may register the connection before its callback times out.
        // Its own disconnect then rejects the not-yet-connected client state. Release
        // only this candidate connection; never unregister a different observer.
        try {
            connectionType.getMethod("disconnect").invoke(connection);
        } catch (ReflectiveOperationException ignored) {
            // Best-effort release; the original connection failure propagates.
        }
    }

    private static synchronized void detach() {
        if (automation != null) {
            try {
                if (disconnect != null) {
                    disconnect.invoke(automation);
                }
            } catch (ReflectiveOperationException ignored) {
                // Best-effort release of an observer that is already being dropped.
            }

            automation = null;
            disconnect = null;
        }
    }

    private static JSONObject identity(AccessibilityWindowInfo window) throws Exception {
        Rect rect = new Rect();

        window.getBoundsInScreen(rect);
        AccessibilityNodeInfo root = window.getRoot();
        String packageName = "";

        try {
            if (root != null) {
                packageName = Geometry.text(root.getPackageName(), false);
            }
        } finally {
            if (root != null) {
                root.recycle();
            }
        }

        JSONObject value =
                new JSONObject()
                        .put("id", window.getId())
                        .put("type", window.getType())
                        .put("layer", window.getLayer())
                        .put("active", window.isActive())
                        .put("input_focused", window.isFocused())
                        .put("accessibility_focused", window.isAccessibilityFocused())
                        .put("title", Geometry.text(window.getTitle(), false))
                        .put("bounds", bounds(rect))
                        .put("package", packageName)
                        .put("component", JSONObject.NULL)
                        .put("identity_basis", "accessibility_window_id_and_metadata");

        if (Build.VERSION.SDK_INT == 30) {
            value.put("display_id", window.getDisplayId());
        }

        return value;
    }

    private static List<AccessibilityWindowInfo> windows() {
        List<AccessibilityWindowInfo> result = automation.getWindows();

        check(result != null && result.size() <= 64, "window_limit");

        return result;
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    private static List<AccessibilityWindowInfo> windows(int displayId, boolean interactive) {
        if (!interactive) {
            return windows();
        }

        check(Build.VERSION.SDK_INT == 30, "interactive_scope_unsupported");
        phase(GuestPhase.Phase.AX_INVENTORY_BEGIN);
        android.util.SparseArray<List<AccessibilityWindowInfo>> all =
                automation.getWindowsOnAllDisplays();
        check(all != null, "window_limit");
        List<AccessibilityWindowInfo> selected = all.get(displayId);
        phase(
                selected == null
                        ? GuestPhase.Phase.AX_INVENTORY_MISSING
                        : GuestPhase.Phase.AX_INVENTORY_READY);
        boolean accepted = false;

        try {
            check(all.size() <= 16, "window_limit");

            if (selected == null) {
                throw new EmptyTreeRecovery.InventoryUnavailable();
            }

            check(selected.size() <= 64, "application_window_unverified");
            java.util.Set<Integer> ids = new java.util.HashSet<>();

            for (AccessibilityWindowInfo window : selected) {
                check(
                        window != null
                                && window.getDisplayId() == displayId
                                && ids.add(window.getId()),
                        "window_display_conflict");
            }

            accepted = true;

            return selected;
        } finally {
            for (int i = 0; i < all.size(); ++i) {
                List<AccessibilityWindowInfo> windows = all.valueAt(i);

                if (windows != null && (!accepted || windows != selected)) {
                    for (AccessibilityWindowInfo window : windows) {
                        if (window != null) {
                            window.recycle();
                        }
                    }
                }
            }
        }
    }

    private static JSONArray identities(List<AccessibilityWindowInfo> windows) throws Exception {
        JSONArray result = new JSONArray();

        for (AccessibilityWindowInfo window : windows) {
            result.put(identity(window));
        }

        return result;
    }

    private static void tree(
            AccessibilityNodeInfo node,
            String parent,
            int depth,
            int window,
            int[] viewport,
            JSONArray output,
            String role)
            throws Exception {
        String id = treeNode(node, parent, depth, window, viewport, output, role);
        int count = node.getChildCount();

        check(count >= 0 && count <= MAX_NODES, "child_limit");

        for (int i = 0; i < count; ++i) {
            check(true, "deadline");
            AccessibilityNodeInfo child = node.getChild(i);
            check(child != null, "stale_child");

            try {
                tree(child, id, depth + 1, window, viewport, output, role);
            } finally {
                child.recycle();
            }
        }
    }

    private static String treeNode(
            AccessibilityNodeInfo node,
            String parent,
            int depth,
            int window,
            int[] viewport,
            JSONArray output,
            String role)
            throws Exception {
        check(depth <= MAX_DEPTH && ++COUNTS.get()[0] <= MAX_NODES, "node_limit");
        check(node.refresh(), "stale_node");
        check(node.getWindowId() == window, "node_window_changed");
        String id = Integer.toString(COUNTS.get()[0]);
        Rect rect = new Rect();
        node.getBoundsInScreen(rect);
        int[] clipped =
                Geometry.clip(
                        rect.left,
                        rect.top,
                        rect.right,
                        rect.bottom,
                        viewport,
                        SURFACE_SIZE.get()[0],
                        SURFACE_SIZE.get()[1]);
        boolean visible = node.isVisibleToUser(),
                enabled = node.isEnabled(),
                clickable = node.isClickable();
        boolean password = node.isPassword();
        JSONObject value =
                new JSONObject()
                        .put("id", id)
                        .put("parent", parent == null ? JSONObject.NULL : parent)
                        .put("window_id", window)
                        .put("screen", role)
                        .put("resource_id", Geometry.text(node.getViewIdResourceName(), false))
                        .put("package", Geometry.text(node.getPackageName(), false))
                        .put("class", Geometry.text(node.getClassName(), false))
                        .put("text", Geometry.text(node.getText(), password))
                        .put("description", Geometry.text(node.getContentDescription(), password))
                        .put("text_redacted", password)
                        .put("bounds", bounds(rect))
                        .put("clipped_bounds", bounds(clipped))
                        .put("visible", visible)
                        .put("enabled", enabled)
                        .put("clickable", clickable)
                        .put("checkable", node.isCheckable())
                        .put("checked", node.isChecked())
                        .put("selected", node.isSelected())
                        .put("scrollable", node.isScrollable())
                        .put("focused", node.isFocused())
                        .put("actionable", false)
                        .put(
                                "actionable_candidate",
                                Geometry.actionable(visible, enabled, clickable, clipped))
                        .put("occlusion", "unknown")
                        .put("touchability", "unknown")
                        .put("identity_scope", "this_observation_only")
                        .put("actionable_basis", "node_state_and_viewport_only");
        Map<String, Object> editor =
                EditorMetadata.fields(
                        node.isEditable(),
                        password,
                        password ? null : node.getHintText(),
                        !password && node.isShowingHintText(),
                        password ? -1 : node.getTextSelectionStart(),
                        password ? -1 : node.getTextSelectionEnd());

        for (Map.Entry<String, Object> field : editor.entrySet()) {
            value.put(
                    field.getKey(), field.getValue() == null ? JSONObject.NULL : field.getValue());
        }

        COUNTS.get()[1] += value.toString().getBytes(StandardCharsets.UTF_8).length + 1;
        check(COUNTS.get()[1] <= MAX_BYTES - 65536, "output_limit");
        output.put(value);

        return id;
    }

    /** Returns the requested surface flags by slot: primary, secondary and rear. */
    private static boolean[] requestedSurfaces(JSONArray requested) throws Exception {
        boolean[] wanted = new boolean[Surfaces.SCREENS.length];

        for (int i = 0; i < requested.length(); ++i) {
            int slot = Surfaces.slot(requested.getString(i));

            if (slot < 0) {
                throw new IllegalArgumentException("unsupported_surface");
            }

            check(!wanted[slot], "duplicate_surface");
            wanted[slot] = true;
        }

        return wanted;
    }

    private static void checkSelector(JSONObject selector) {
        check(
                selector != null && Geometry.resourceId(selector.optString("resource_id")),
                "selector_resource");
        java.util.Iterator<String> keys = selector.keys();

        while (keys.hasNext()) {
            String key = keys.next();
            check("resource_id".equals(key) || "package".equals(key), "selector_key");
        }

        if (selector.has("package")) {
            check(Geometry.packageId(selector.optString("package")), "selector_package");
        }
    }

    private static JSONObject observe(JSONObject request) throws Exception {
        long start = SystemClock.uptimeMillis();
        String beforeBoot = boot();
        JSONArray requested = request.optJSONArray("surfaces");

        if (requested == null) {
            requested = new JSONArray().put("media_primary").put("media_secondary");
        }

        check(
                requested.length() >= 1 && requested.length() <= Surfaces.SCREENS.length,
                "surface_count");
        boolean[] wanted = requestedSurfaces(requested);
        boolean primary = wanted[0], secondary = wanted[1], rear = wanted[2];
        JSONObject selector = request.optJSONObject("selector");

        if (request.has("selector")) {
            checkSelector(selector);
        }

        JSONArray surfaces = new JSONArray();
        boolean attached =
                "all_attached"
                        .equals(
                                request.optJSONObject("scope") == null
                                        ? ""
                                        : request.getJSONObject("scope").optString("windows"));
        boolean parallel = request.has("parallel_surfaces");

        if (parallel) {
            check(
                    Boolean.TRUE.equals(request.opt("parallel_surfaces"))
                            && attached
                            && primary
                            && secondary
                            && !rear
                            && selector == null,
                    "parallel_surfaces_unsupported");
        }

        if (attached) {
            return attached(selector, wanted, start, beforeBoot, parallel);
        }

        if ("interactive"
                .equals(
                        request.optJSONObject("scope") == null
                                ? ""
                                : request.getJSONObject("scope").optString("windows"))) {
            return interactive(request, wanted, start, beforeBoot);
        }

        if (secondary || rear) {
            return withOtherDisplays(request, selector, wanted, start, beforeBoot);
        }

        if (primary) {
            surfaces.put(accessibility(selector, 0, "primary", false));
        }

        check(beforeBoot.equals(boot()), "boot_changed");

        return new JSONObject()
                .put("schema", 1)
                .put("ok", true)
                .put("observation_id", UUID.randomUUID().toString())
                .put("boot_id", beforeBoot)
                .put("observer_pid", Process.myPid())
                .put("observer_epoch", EPOCH)
                .put("started_uptime_ms", start)
                .put("completed_uptime_ms", SystemClock.uptimeMillis())
                .put("surfaces", surfaces);
    }

    /** Reads the other displays through their attached windows, after the primary display. */
    private static JSONObject withOtherDisplays(
            JSONObject request,
            JSONObject selector,
            boolean[] wanted,
            long start,
            String beforeBoot)
            throws Exception {
        JSONObject others =
                attached(
                        selector,
                        new boolean[] {false, wanted[1], wanted[2]},
                        start,
                        beforeBoot,
                        false);

        if (!wanted[0]) {
            return others;
        }

        JSONObject primaryRequest =
                new JSONObject(request.toString())
                        .put("surfaces", new JSONArray().put("media_primary"));
        JSONObject result = observe(primaryRequest);
        JSONArray read = others.getJSONArray("surfaces");

        for (int i = 0; i < read.length(); ++i) {
            result.getJSONArray("surfaces").put(read.getJSONObject(i));
        }

        result.put("started_uptime_ms", start).put("atomic_ui_state", false);

        return result;
    }

    private static JSONObject accessibility(
            JSONObject selector, int displayId, String role, boolean interactive) throws Exception {
        android.graphics.Point size = realSize(displayId);

        SURFACE_SIZE.set(new int[] {size.x, size.y});
        connect();
        List<AccessibilityWindowInfo> original = windows(displayId, interactive);

        try {
            JSONArray before = identities(original), elements = new JSONArray();
            COUNTS.set(new int[2]);
            COUNTS.get()[1] = before.toString().getBytes(StandardCharsets.UTF_8).length;
            check(COUNTS.get()[1] <= MAX_BYTES - 65536, "output_limit");
            JSONArray unavailable = new JSONArray();
            WindowCoverage coverage =
                    new WindowCoverage(displayId, Build.VERSION.SDK_INT == 30, selectedProfile);

            for (AccessibilityWindowInfo window : original) {
                readAccessibilityWindow(window, selector, role, elements, unavailable, coverage);
            }

            requireSameWindows(before, displayId, interactive);

            if (coverage.needsOwnerVerification()
                    || coverage.reconcilesNavigationPanels(interactive)) {
                verifyOwners(coverage, original, size, interactive, unavailable);
                requireSameWindows(before, displayId, interactive);
            }

            verifyCoverage(coverage, selector != null, elements.length(), original.size());
            JSONObject surface =
                    new JSONObject()
                            .put("surface", "media_" + role)
                            .put("screen", role)
                            .put("display_id", displayId)
                            .put("width", size.x)
                            .put("height", size.y)
                            .put("status", unavailable.length() == 0 ? "ok" : "partial")
                            .put("complete", unavailable.length() == 0)
                            .put("unavailable_windows", unavailable)
                            .put("windows", before)
                            .put("nodes", elements)
                            .put(
                                    "scope",
                                    interactive
                                            ? "api30_interactive"
                                            : ObserverPlatform.defaultScope(Build.VERSION.SDK_INT))
                            .put("coverage_basis", coverage.basis())
                            .put("atomic_ui_state", false);
            putCoverageOwner(surface, coverage.verifiedOwner());

            if (interactive) {
                surface.put("selection_coverage", "interactive_windows_only");
            }

            if (selector != null) {
                surface.put("selection", "resource_id").put("matched_count", elements.length());
            }

            return surface;
        } finally {
            for (AccessibilityWindowInfo window : original) {
                window.recycle();
            }
        }
    }

    private static android.graphics.Point realSize(int displayId) throws Exception {
        Object displayManager =
                Class.forName("android.hardware.display.DisplayManagerGlobal")
                        .getMethod("getInstance")
                        .invoke(null);
        android.view.Display display =
                (android.view.Display)
                        displayManager
                                .getClass()
                                .getMethod("getRealDisplay", int.class)
                                .invoke(displayManager, displayId);

        check(display != null, "display_unavailable");
        android.graphics.Point size = new android.graphics.Point();
        display.getRealSize(size);
        check(size.x > 0 && size.x <= 3840 && size.y > 0 && size.y <= 2160, "display_unavailable");

        return size;
    }

    private static void readAccessibilityWindow(
            AccessibilityWindowInfo window,
            JSONObject selector,
            String role,
            JSONArray elements,
            JSONArray unavailable,
            WindowCoverage coverage)
            throws Exception {
        Rect rect = new Rect();

        window.getBoundsInScreen(rect);
        AccessibilityNodeInfo root = window.getRoot();
        coverage.add(window.getType(), root != null);

        if (root != null && window.getType() == 3 && window.isActive() && window.isFocused()) {
            coverage.noteFocusedSystemRoot(
                    window.getType(),
                    true,
                    true,
                    true,
                    Geometry.text(root.getPackageName(), false));
        }

        if (root == null) {
            unavailable.put(window.getId());

            return;
        }

        try {
            int[] viewport = new int[] {rect.left, rect.top, rect.right, rect.bottom};

            if (selector == null) {
                tree(root, null, 0, window.getId(), viewport, elements, role);
            } else {
                selectedTrees(root, selector, window.getId(), viewport, elements, role);
            }
        } finally {
            root.recycle();
        }
    }

    private static void selectedTrees(
            AccessibilityNodeInfo root,
            JSONObject selector,
            int window,
            int[] viewport,
            JSONArray elements,
            String role)
            throws Exception {
        List<AccessibilityNodeInfo> matches =
                root.findAccessibilityNodeInfosByViewId(selector.getString("resource_id"));

        check(matches != null, "selector_unavailable");

        try {
            check(matches.size() <= 64, "selector_limit");

            for (AccessibilityNodeInfo match : matches) {
                check(match != null, "stale_match");
                check(match.refresh(), "stale_match");

                if (selector.has("package")
                        && !selector.getString("package")
                                .equals(Geometry.text(match.getPackageName(), false))) {
                    continue;
                }

                check(
                        selector.getString("resource_id")
                                .equals(Geometry.text(match.getViewIdResourceName(), false)),
                        "selector_changed");
                check(COUNTS.get()[0] < 64, "selector_limit");
                treeNode(match, null, 0, window, viewport, elements, role);
            }
        } finally {
            for (AccessibilityNodeInfo match : matches) {
                if (match != null) {
                    match.recycle();
                }
            }
        }
    }

    private static void requireSameWindows(JSONArray before, int displayId, boolean interactive)
            throws Exception {
        List<AccessibilityWindowInfo> current = windows(displayId, interactive);

        try {
            check(before.toString().equals(identities(current).toString()), "window_changed");
        } finally {
            for (AccessibilityWindowInfo window : current) {
                window.recycle();
            }
        }
    }

    private static void verifyOwners(
            WindowCoverage coverage,
            List<AccessibilityWindowInfo> original,
            android.graphics.Point size,
            boolean interactive,
            JSONArray unavailable)
            throws Exception {
        java.util.Map<String, ViewServer.Window> owned = VIEW_SERVER.list(DEADLINES.get());

        ViewServer.assignDisplays(owned, ViewServer.metadata(DEADLINES.get()));
        String ownedBefore = ViewServer.identities(owned);
        java.util.List<WindowCoverage.Owner> owners = new java.util.ArrayList<>();
        WindowCoverage.Owner drawerOwner = null;

        for (ViewServer.Window window : owned.values()) {
            if (window.display == null || window.type == null) {
                continue;
            }

            WindowCoverage.Owner owner =
                    new WindowCoverage.Owner(
                            window.hash,
                            window.display,
                            window.type,
                            window.ownerPackage,
                            window.frame,
                            new WindowCoverage.State(
                                    window.touchable,
                                    window.visible,
                                    window.hasSurface,
                                    window.focusable,
                                    window.drawn,
                                    window.viewVisible));
            owners.add(owner);

            if ("VehicleHiBoardBarNew".equals(window.title)
                    && Boolean.TRUE.equals(window.currentFocus)) {
                drawerOwner = owner;
            }
        }

        for (AccessibilityWindowInfo window : original) {
            addOwnedWindow(coverage, window, size, owners, drawerOwner);
        }

        if (coverage.reconcilesNavigationPanels(interactive)) {
            recordNavigationPanels(coverage, owned, unavailable);
        }

        java.util.Map<String, ViewServer.Window> ownedAfter = VIEW_SERVER.list(DEADLINES.get());
        ViewServer.assignDisplays(ownedAfter, ViewServer.metadata(DEADLINES.get()));
        check(ownedBefore.equals(ViewServer.identities(ownedAfter)), "window_changed");
    }

    private static void addOwnedWindow(
            WindowCoverage coverage,
            AccessibilityWindowInfo window,
            android.graphics.Point size,
            java.util.List<WindowCoverage.Owner> owners,
            WindowCoverage.Owner drawerOwner) {
        AccessibilityNodeInfo root = window.getRoot();

        try {
            Rect frame = new Rect();
            window.getBoundsInScreen(frame);
            coverage.addOwnedNavigationPanel(
                    window.getType(),
                    root != null && root.refresh(),
                    root == null ? null : Geometry.text(root.getPackageName(), false),
                    new int[] {frame.left, frame.top, frame.right, frame.bottom},
                    new int[] {0, 0, size.x, size.y},
                    owners);
            boolean refreshed = root != null && root.refresh();
            String owner = root == null ? null : Geometry.text(root.getPackageName(), false);
            int[] bounds = {frame.left, frame.top, frame.right, frame.bottom};
            coverage.addOwnedUnclassified(
                    window.getType(),
                    refreshed,
                    window.isActive(),
                    window.isFocused(),
                    owner,
                    bounds,
                    owners);
            coverage.addOwnedVehicleDrawer(
                    window.getType(),
                    refreshed,
                    window.isActive(),
                    window.isFocused(),
                    owner,
                    bounds,
                    drawerOwner);
        } finally {
            if (root != null) {
                root.recycle();
            }
        }
    }

    private static void recordNavigationPanels(
            WindowCoverage coverage,
            java.util.Map<String, ViewServer.Window> owned,
            JSONArray unavailable) {
        for (ViewServer.Window window : owned.values()) {
            if (window.display == null || window.type == null) {
                continue;
            }

            WindowCoverage.Owner owner =
                    new WindowCoverage.Owner(
                            window.hash,
                            window.display,
                            window.type,
                            window.ownerPackage,
                            window.frame,
                            new WindowCoverage.State(
                                    window.touchable,
                                    window.visible,
                                    window.hasSurface,
                                    window.focusable,
                                    null,
                                    null));

            if (coverage.recordNavigationPanel(owner, window.drawn, window.viewVisible)) {
                unavailable.put(window.hash);
            }
        }
    }

    private static void verifyCoverage(
            WindowCoverage coverage, boolean selected, int elements, int windows) {
        phase(GuestPhase.Phase.COVERAGE_BEGIN);

        try {
            coverage.verifyContent(selected, elements, windows);
        } catch (IllegalStateException rejected) {
            if ("application_window_unverified".equals(rejected.getMessage())) {
                phase(
                        coverage.needsOwnerVerification()
                                ? GuestPhase.Phase.COVERAGE_UNCLASSIFIED_OWNER_REJECTED
                                : GuestPhase.Phase.COVERAGE_NO_APPLICATION_ROOT);
            }

            throw rejected;
        }

        phase(GuestPhase.Phase.COVERAGE_VERIFIED);
    }

    private static void putCoverageOwner(JSONObject surface, WindowCoverage.Owner verifiedOwner)
            throws Exception {
        if (verifiedOwner == null) {
            return;
        }

        surface.put(
                "coverage_owner",
                new JSONObject()
                        .put(
                                "identity_basis",
                                ObserverPlatform.provenance(
                                        Build.VERSION.SDK_INT,
                                        "stable_",
                                        "windowstate_owner_type_frame"))
                        .put("id", verifiedOwner.id)
                        .put("display_id", verifiedOwner.display)
                        .put("type", verifiedOwner.type)
                        .put("package", verifiedOwner.packageName)
                        .put("frame", bounds(verifiedOwner.frame)));
    }

    private static JSONObject interactive(
            JSONObject request, boolean[] wanted, long start, String beforeBoot) throws Exception {
        boolean primary = wanted[0], secondary = wanted[1], rear = wanted[2];

        check(Build.VERSION.SDK_INT == 30, "interactive_scope_unsupported");
        long generation = DISPLAY_GENERATION.get();
        JSONArray surfaces = new JSONArray();
        JSONObject selector = request.optJSONObject("selector");

        if (primary) {
            surfaces.put(accessibility(selector, 0, "primary", true));
        }

        if (secondary || rear) {
            Object manager =
                    Class.forName("android.hardware.display.DisplayManagerGlobal")
                            .getMethod("getInstance")
                            .invoke(null);
            int selected = -1, rearDisplay = -1;

            for (int id : (int[]) manager.getClass().getMethod("getDisplayIds").invoke(manager)) {
                android.view.Display display =
                        (android.view.Display)
                                manager.getClass()
                                        .getMethod("getRealDisplay", int.class)
                                        .invoke(manager, id);

                if (display == null) {
                    continue;
                }

                if (SecondaryDisplay.matches(30, id, display.getName(), uniqueId(display))) {
                    check(!secondary || selected == -1, "secondary_display_ambiguous");
                    selected = id;
                } else if (id > 0 && Surfaces.rear(uniqueId(display))) {
                    rearDisplay = id;
                }
            }

            if (secondary) {
                check(selected >= 0, "display_unavailable");
                surfaces.put(accessibility(selector, selected, "secondary", true));
            }

            if (rear) {
                check(rearDisplay >= 0, "display_unavailable");
                surfaces.put(accessibility(selector, rearDisplay, "rear", true));
            }
        }

        check(generation == DISPLAY_GENERATION.get(), "display_changed");
        check(beforeBoot.equals(boot()), "boot_changed");

        return new JSONObject()
                .put("schema", 1)
                .put("ok", true)
                .put("observation_id", UUID.randomUUID().toString())
                .put("boot_id", beforeBoot)
                .put("observer_pid", Process.myPid())
                .put("observer_epoch", EPOCH)
                .put("started_uptime_ms", start)
                .put("completed_uptime_ms", SystemClock.uptimeMillis())
                .put("surfaces", surfaces)
                .put("atomic_ui_state", false);
    }

    private static byte[] line(InputStream in) throws Exception {
        ByteArrayOutputStream value = new ByteArrayOutputStream();

        for (; ; ) {
            int next = in.read();
            check(next >= 0, "incomplete_request");

            if (next == 10) {
                return value.toByteArray();
            }

            check(value.size() < 8192, "request_limit");
            value.write(next);
        }
    }

    private static java.util.List<OwnedWindowProof.ProcessOwner> processOwners() {
        java.util.List<OwnedWindowProof.ProcessOwner> result = new java.util.ArrayList<>();

        try {
            Object manager =
                    Class.forName("android.app.ActivityManager")
                            .getMethod("getService")
                            .invoke(null);
            Object value =
                    Class.forName("android.app.IActivityManager")
                            .getMethod("getRunningAppProcesses")
                            .invoke(manager);

            if (!(value instanceof java.util.List) || ((java.util.List<?>) value).size() > 1024) {
                return result;
            }

            for (Object entry : (java.util.List<?>) value) {
                android.app.ActivityManager.RunningAppProcessInfo owner =
                        (android.app.ActivityManager.RunningAppProcessInfo) entry;
                result.add(
                        new OwnedWindowProof.ProcessOwner(
                                owner.pid, owner.uid, owner.processName, owner.pkgList));
            }
        } catch (ReflectiveOperationException unavailable) {
            result.clear();
        }

        return result;
    }

    private static JSONObject attached(
            JSONObject selector, boolean[] wanted, long start, String beforeBoot, boolean parallel)
            throws Exception {
        long displayBefore = DISPLAY_GENERATION.get();
        android.view.Display[] found = new android.view.Display[Surfaces.SCREENS.length];
        boolean ambiguousSecondary = resolveDisplays(found, parallel);
        android.view.Display[] selected = new android.view.Display[found.length];
        boolean resolved = true;

        for (int slot = 0; slot < found.length; ++slot) {
            selected[slot] = wanted[slot] ? found[slot] : null;
            resolved = resolved && (!wanted[slot] || found[slot] != null);
        }

        if (resolved) {
            phase(GuestPhase.Phase.DISPLAY_RESOLVED);
        }

        java.util.Map<String, ViewServer.Window> windows = VIEW_SERVER.list(DEADLINES.get());
        ViewServer.assignDisplays(windows, ViewServer.metadata(DEADLINES.get()));
        String before = ViewServer.identities(windows);
        java.util.List<OwnedWindowProof.ProcessOwner> processesBefore = processOwners();
        AttachedScope scope =
                new AttachedScope(
                        selector, windows, processesBefore, beforeBoot, displayBefore, parallel);
        JSONArray surfaces =
                parallel
                        ? parallelSurfaces(scope, selected, ambiguousSecondary)
                        : sequentialSurfaces(scope, selected, wanted);
        phase(GuestPhase.Phase.IDENTITY_BEGIN);
        java.util.Map<String, ViewServer.Window> after = VIEW_SERVER.list(DEADLINES.get());
        ViewServer.assignDisplays(after, ViewServer.metadata(DEADLINES.get()));
        check(before.equals(ViewServer.identities(after)), "window_changed");
        dropUnverifiedBindings(surfaces, after, processOwners());
        phase(GuestPhase.Phase.IDENTITY_END);
        check(displayBefore == DISPLAY_GENERATION.get(), "display_changed");
        check(beforeBoot.equals(boot()), "boot_changed");
        JSONObject result =
                new JSONObject()
                        .put("schema", 1)
                        .put("format", "voyemu.ui.v1")
                        .put("ok", true)
                        .put("observation_id", UUID.randomUUID().toString())
                        .put("boot_id", beforeBoot)
                        .put("observer_epoch", EPOCH)
                        .put("observer_pid", Process.myPid())
                        .put("started_uptime_ms", start)
                        .put("completed_uptime_ms", SystemClock.uptimeMillis())
                        .put("clock_domain", "android_uptime_ms")
                        .put("surfaces", surfaces)
                        .put("atomic_ui_state", false);

        if (parallel) {
            result.put("parallel_surfaces", true);
        }

        return result;
    }

    // Display.getUniqueId is hidden; null where the platform does not expose it.
    private static String uniqueId(android.view.Display display) {
        try {
            return (String) display.getClass().getMethod("getUniqueId").invoke(display);
        } catch (ReflectiveOperationException e) {
            return null;
        }
    }

    /**
     * Stores the primary display, the only matching secondary display and the rear display in
     * found; returns whether the secondary display is ambiguous, which only a parallel read
     * tolerates.
     */
    private static boolean resolveDisplays(android.view.Display[] found, boolean parallel)
            throws Exception {
        Object displays =
                Class.forName("android.hardware.display.DisplayManagerGlobal")
                        .getMethod("getInstance")
                        .invoke(null);
        Class<?> manager = displays.getClass();
        int[] ids = (int[]) manager.getMethod("getDisplayIds").invoke(displays);
        boolean ambiguousSecondary = false;

        for (int id : ids) {
            android.view.Display display =
                    (android.view.Display)
                            manager.getMethod("getRealDisplay", int.class).invoke(displays, id);

            if (display == null) {
                continue;
            }

            if (id == 0) {
                found[0] = display;
            } else if (Surfaces.rear(uniqueId(display))) {
                found[2] = display;
            } else if (SecondaryDisplay.matches(
                    Build.VERSION.SDK_INT, id, display.getName(), uniqueId(display))) {
                if (found[1] == null) {
                    found[1] = display;
                } else {
                    check(parallel, "secondary_display_ambiguous");
                    ambiguousSecondary = true;
                }
            }
        }

        if (ambiguousSecondary) {
            found[1] = null;
        }

        return ambiguousSecondary;
    }

    private static JSONArray parallelSurfaces(
            AttachedScope scope, android.view.Display[] selected, boolean secondaryAmbiguous)
            throws Exception {
        long deadline =
                ParallelSurfaces.workerDeadline(SystemClock.uptimeMillis(), DEADLINES.get());
        GuestPhase.State diagnostic = GuestPhase.state();
        List<ParallelSurfaces.Result<JSONObject>> results =
                ParallelSurfaces.read(
                        SystemClock::uptimeMillis,
                        slot -> {
                            DEADLINES.set(deadline);
                            GuestPhase.use(diagnostic);

                            try {
                                check(
                                        slot == 0 || !secondaryAmbiguous,
                                        "secondary_display_ambiguous");

                                return attachedSurface(scope, slot, selected[slot]);
                            } finally {
                                GuestPhase.end();
                                DEADLINES.remove();
                            }
                        });
        JSONArray surfaces = new JSONArray();

        for (int slot = 0; slot < 2; ++slot) {
            ParallelSurfaces.Result<JSONObject> result = results.get(slot);
            JSONObject surface =
                    result.error == null
                            ? result.value
                            : unavailableSurface(slot, selected[slot], result.error);
            surfaces.put(
                    surface.put("started_uptime_ms", result.started)
                            .put("completed_uptime_ms", result.completed)
                            .put("clock_domain", "android_uptime_ms"));
        }

        return surfaces;
    }

    private static JSONArray sequentialSurfaces(
            AttachedScope scope, android.view.Display[] selected, boolean[] wanted)
            throws Exception {
        JSONArray surfaces = new JSONArray();

        for (int slot = 0; slot < wanted.length; ++slot) {
            if (!wanted[slot]) {
                continue;
            }

            surfaces.put(attachedSurface(scope, slot, selected[slot]));
        }

        return surfaces;
    }

    private static void dropUnverifiedBindings(
            JSONArray surfaces,
            java.util.Map<String, ViewServer.Window> after,
            java.util.List<OwnedWindowProof.ProcessOwner> processesAfter)
            throws Exception {
        for (int i = 0; i < surfaces.length(); ++i) {
            JSONArray outputWindows = surfaces.getJSONObject(i).getJSONArray("windows");

            for (int j = 0; j < outputWindows.length(); ++j) {
                JSONObject window = outputWindows.getJSONObject(j);

                if (window.has("verified_binding")
                        && !OwnedWindowProof.process(
                                after.get(window.getString("id")), processesAfter)) {
                    window.remove("verified_binding");
                }
            }
        }
    }

    private static JSONObject unavailableSurface(
            int slot, android.view.Display display, Exception error) throws Exception {
        String reason = error.getMessage();

        if (!"display_unavailable".equals(reason)
                && !"secondary_display_ambiguous".equals(reason)
                && !"deadline".equals(reason)) {
            reason = "observer_unavailable";
        }

        return new JSONObject()
                .put("surface", Surfaces.surface(slot))
                .put("screen", Surfaces.SCREENS[slot])
                .put("sdk_display_id", slot)
                .put("display_id", display == null ? JSONObject.NULL : display.getDisplayId())
                .put("scope", "all_attached")
                .put("status", "unavailable")
                .put("reason", reason)
                .put("complete", false)
                .put("enumeration_complete", false)
                .put("metadata_complete", false)
                .put("atomic_ui_state", false)
                .put("nodes", new JSONArray())
                .put("windows", new JSONArray())
                .put("unavailable_windows", new JSONArray())
                .put("window_read_failures", new JSONArray());
    }

    /** Inputs shared by both surfaces of one attached-window observation. */
    private static final class AttachedScope {
        final JSONObject selector;
        final Map<String, ViewServer.Window> windows;
        final List<OwnedWindowProof.ProcessOwner> processesBefore;
        final String beforeBoot;
        final long displayBefore;
        final boolean parallel;

        AttachedScope(
                JSONObject selector,
                Map<String, ViewServer.Window> windows,
                List<OwnedWindowProof.ProcessOwner> processesBefore,
                String beforeBoot,
                long displayBefore,
                boolean parallel) {
            this.selector = selector;
            this.windows = windows;
            this.processesBefore = processesBefore;
            this.beforeBoot = beforeBoot;
            this.displayBefore = displayBefore;
            this.parallel = parallel;
        }
    }

    /** Per-surface state accumulated while its attached windows are read. */
    private static final class SurfaceRead {
        final int slot;
        final android.view.Display display;
        final android.graphics.Point size;
        final JSONArray elements = new JSONArray();
        boolean metadataComplete = true;
        boolean selectionComplete = true;
        int matchedCount;
        String dump;

        SurfaceRead(int slot, android.view.Display display, android.graphics.Point size) {
            this.slot = slot;
            this.display = display;
            this.size = size;
        }

        String displayEpoch(long displayBefore) {
            return EPOCH
                    + ":"
                    + displayBefore
                    + ":"
                    + display.getDisplayId()
                    + ":"
                    + size.x
                    + "x"
                    + size.y;
        }

        int[] clip(int[] screen) {
            return Geometry.clip(
                    screen[0],
                    screen[1],
                    screen[2],
                    screen[3],
                    new int[] {0, 0, size.x, size.y},
                    size.x,
                    size.y);
        }
    }

    private static JSONObject attachedSurface(
            AttachedScope scope, int slot, android.view.Display display) throws Exception {
        check(display != null, "display_unavailable");
        android.graphics.Point size = new android.graphics.Point();
        display.getRealSize(size);
        android.util.DisplayMetrics metrics = new android.util.DisplayMetrics();
        display.getRealMetrics(metrics);
        JSONArray outputWindows = new JSONArray(),
                unavailable = new JSONArray(),
                failures = new JSONArray();
        SurfaceRead read = new SurfaceRead(slot, display, size);
        int inventoryOrdinal = 0, surfaceOrdinal = 0;

        for (ViewServer.Window window : scope.windows.values()) {
            check(true, "deadline");
            int inventoryIndex = inventoryOrdinal++;

            if (window.display == null) {
                unavailable.put(window.hash);
                continue;
            }

            if (window.display != display.getDisplayId()) {
                continue;
            }

            JSONObject w = AttachedJson.window(window);
            outputWindows.put(w);
            final int ordinal = scope.parallel ? inventoryIndex : surfaceOrdinal++;
            GuestPhase.markWindow(
                    SystemClock.uptimeMillis(),
                    ordinal,
                    window.hash,
                    window.display,
                    window.type,
                    window.ownerUid);
            long windowReadStarted = SystemClock.uptimeMillis();
            read.dump = null;

            try {
                readAttachedWindow(scope, read, window, w, ordinal);
            } catch (Exception error) {
                // checkstyle-allow IllegalCatch: any failed window read becomes a recorded failure
                if (scope.parallel) {
                    check(true, "deadline");
                }

                unavailable.put(window.hash);
                failures.put(windowReadFailure(error, window, windowReadStarted, read.dump));
            }
        }

        JSONObject surface =
                attachedSurfaceResult(scope, read, metrics, failures, unavailable.length())
                        .put("windows", outputWindows)
                        .put("nodes", read.elements)
                        .put("unavailable_windows", unavailable)
                        .put("atomic_ui_state", false)
                        .put(
                                "source",
                                ObserverPlatform.provenance(
                                        Build.VERSION.SDK_INT, "", "viewserver_dump_wm_text_v1"))
                        .put(
                                "selection_coverage",
                                read.selectionComplete
                                        ? "known_full_resource_identity"
                                        : "unknown_resource_identity");

        if (scope.selector != null) {
            surface.put("selection", "resource_id_with_tree_context")
                    .put("matched_count", read.matchedCount);
        }

        if (scope.parallel) {
            check(true, "deadline");
        }

        return surface;
    }

    private static JSONObject attachedSurfaceResult(
            AttachedScope scope,
            SurfaceRead read,
            android.util.DisplayMetrics metrics,
            JSONArray failures,
            int unavailable)
            throws Exception {
        boolean complete = unavailable == 0 && read.metadataComplete && read.selectionComplete;

        return new JSONObject()
                .put("surface", Surfaces.surface(read.slot))
                .put("screen", Surfaces.SCREENS[read.slot])
                .put("sdk_display_id", read.slot)
                .put("display_id", read.display.getDisplayId())
                .put("guest_display_id", read.display.getDisplayId())
                .put("display_name", read.display.getName())
                .put("width", read.size.x)
                .put("height", read.size.y)
                .put("density", metrics.density)
                .put("display_epoch", read.displayEpoch(scope.displayBefore))
                .put("scope", "all_attached")
                .put("complete", complete)
                .put("window_read_failures", failures)
                .put("enumeration_complete", unavailable == 0)
                .put("metadata_complete", read.metadataComplete)
                .put("status", complete ? "ok" : "partial");
    }

    private static JSONObject windowReadFailure(
            Exception error, ViewServer.Window window, long started, String dump) throws Exception {
        JSONObject failure =
                new JSONObject(
                                WindowReadDiagnostics.fields(
                                        error, SystemClock.uptimeMillis() - started))
                        .put("window_id", window.hash);

        if (dump != null) {
            failure.put("dump_shape", new JSONObject(WindowReadDiagnostics.dumpShape(dump)));
        }

        return failure;
    }

    private static void readAttachedWindow(
            AttachedScope scope,
            SurfaceRead read,
            ViewServer.Window window,
            JSONObject w,
            int ordinal)
            throws Exception {
        String unsupported = WindowReadDiagnostics.unsupportedReason(window.type, window.title);

        if (unsupported != null) {
            throw new IllegalStateException(unsupported);
        }

        GuestPhase.mark(GuestPhase.Phase.DUMP_BEGIN, SystemClock.uptimeMillis(), ordinal);
        read.dump = VIEW_SERVER.dump(window, DEADLINES.get(), scope.parallel);
        GuestPhase.mark(GuestPhase.Phase.DUMP_END, SystemClock.uptimeMillis(), ordinal);
        java.util.List<ViewDump.Node> parsed = ViewDump.parse(read.dump);
        java.util.Map<String, int[]> screenBounds = ViewProperties.bounds(parsed);
        boolean windowMetadataComplete = !parsed.isEmpty();

        for (ViewDump.Node node : parsed) {
            check(read.elements.length() < MAX_NODES, "node_limit");

            if (!BridgeMetadata.privacyKnown(node)) {
                read.metadataComplete = false;
                windowMetadataComplete = false;
            }

            int[] screen = window.transformed ? null : screenBounds.get(node.identity);
            read.elements.put(attachedNode(scope.selector, read, window, node, screen));
        }

        w.put("read_complete", windowMetadataComplete).put("node_count", parsed.size());

        if (!parsed.isEmpty()) {
            w.put("root_id", window.hash + ":" + parsed.get(0).identity);
        }

        if (Build.VERSION.SDK_INT == 28
                && windowMetadataComplete
                && OwnedWindowProof.eligible(window)
                && OwnedWindowProof.process(window, scope.processesBefore)) {
            w.put("verified_binding", verifiedBinding(scope, read, window, parsed));
        }
    }

    private static JSONObject verifiedBinding(
            AttachedScope scope,
            SurfaceRead read,
            ViewServer.Window window,
            java.util.List<ViewDump.Node> parsed)
            throws Exception {
        return new JSONObject()
                .put("schema", 1)
                .put("basis", "api28_wms_process_viewdump_v1")
                .put("boot_id", scope.beforeBoot)
                .put("observer_epoch", EPOCH)
                .put("display_epoch", read.displayEpoch(scope.displayBefore))
                .put("display_id", window.display)
                .put("window_id", window.hash)
                .put("wm_order", window.order)
                .put("wm_order_verified", true)
                .put("session_id", window.sessionId)
                .put("session_pid", window.sessionPid)
                .put("session_uid", window.sessionUid)
                .put("owner_uid", window.ownerUid)
                .put("package", window.ownerPackage)
                .put("process_name", window.ownerPackage)
                .put("window_token", window.token)
                .put("window_client", window.client)
                .put("root_id", window.hash + ":" + parsed.get(0).identity)
                .put("node_count", parsed.size())
                .put("read_complete", true)
                .put("process_verified", true)
                .put("current_focus", true)
                .put("touchable", true)
                .put("focusable", true)
                .put("visible", true)
                .put("has_surface", true)
                .put("drawn", true)
                .put("view_visible", true)
                .put("obscured", false);
    }

    private static JSONObject attachedNode(
            JSONObject selector,
            SurfaceRead read,
            ViewServer.Window window,
            ViewDump.Node node,
            int[] screen)
            throws Exception {
        String resourceId = BridgeMetadata.resourceId(node);
        boolean bridge = BridgeMetadata.present(node);

        if (selector != null && resourceId == null) {
            read.selectionComplete = false;
        }

        JSONObject n = AttachedJson.node(window, node, read.slot, resourceId, bridge);

        if (screen != null) {
            n.put("bounds", bounds(screen))
                    .put("clipped_bounds", bounds(read.clip(screen)))
                    .put("geometry_basis", "exported_screen_origin_rounded_plus_known_axis_scales")
                    .put(
                            "unknown_fields",
                            new JSONArray()
                                    .put("resource_package")
                                    .put("touchability")
                                    .put("animation_matrix"));
        }

        putPrivacy(n, node, bridge, read);
        AttachedJson.putStates(n, node);
        String visibility = putVisibility(n, node, bridge, screen, read);
        n.put("raw_layout", AttachedJson.rawLayout(node));

        if (screen == null) {
            n.put("geometry_basis", "unknown_transform_or_missing_required_property");
        }

        JSONArray unknown =
                new JSONArray().put("touchability").put("occlusion").put("animation_matrix");

        if (screen == null) {
            unknown.put("screen_bounds");
        }

        if (resourceId == null) {
            unknown.put("resource_package");
        }

        if (visibility == null) {
            unknown.put("visibility");
        }

        n.put("unknown_fields", unknown);

        if (selector != null) {
            boolean matched =
                    selector.optString("resource_id").equals(resourceId)
                            && (!selector.has("package")
                                    || selector.getString("package").equals(window.ownerPackage));
            n.put("matched", matched);

            if (matched) {
                ++read.matchedCount;
            }
        }

        return n;
    }

    private static void putPrivacy(
            JSONObject n, ViewDump.Node node, boolean bridge, SurfaceRead read) throws Exception {
        JSONObject privacy = new JSONObject();

        for (String key : new String[] {"sensitive", "editable", "password"}) {
            Boolean verdict = bridge ? ViewProperties.bool(node, "voyemu:" + key) : null;
            privacy.put(key, verdict == null ? JSONObject.NULL : verdict);

            if (verdict == null) {
                read.metadataComplete = false;
            }
        }

        Integer inputType = bridge ? ViewProperties.integer(node, "voyemu:input_type") : null;
        privacy.put("input_type", inputType == null ? JSONObject.NULL : inputType);
        String textStatus = bridge ? node.properties.get("voyemu:text_status") : null;
        n.put("privacy", privacy).put("text_status", textStatus == null ? "unknown" : textStatus);

        if (BridgeMetadata.privacyKnown(node) && "none".equals(textStatus)) {
            n.put("text_redacted", false).put("redaction_reason", JSONObject.NULL);
        }
    }

    private static String putVisibility(
            JSONObject n, ViewDump.Node node, boolean bridge, int[] screen, SurfaceRead read)
            throws Exception {
        Boolean enabled = ViewProperties.bool(node, "isEnabled()"),
                clickable = ViewProperties.bool(node, "isClickable()");

        n.put("enabled", enabled == null ? JSONObject.NULL : enabled)
                .put("clickable", clickable == null ? JSONObject.NULL : clickable);
        String visibility =
                AttachedJson.visibility(ViewProperties.value(node, "getVisibility()"), bridge);

        if ("VISIBLE".equals(visibility)
                || "INVISIBLE".equals(visibility)
                || "GONE".equals(visibility)) {
            n.put("view_visibility", visibility)
                    .put("visible", "VISIBLE".equals(visibility))
                    .put("visible_to_user_basis", "own_view_visibility_only");
        }

        n.put(
                "actionable_candidate",
                screen != null
                        && "VISIBLE".equals(visibility)
                        && Boolean.TRUE.equals(enabled)
                        && Boolean.TRUE.equals(clickable)
                        && Geometry.actionable(true, true, true, read.clip(screen)));

        return visibility;
    }

    private static boolean missingFocusedApplicationRoot(JSONObject value) throws Exception {
        JSONArray surfaces = value.getJSONArray("surfaces");

        for (int i = 0; i < surfaces.length(); ++i) {
            JSONObject surface = surfaces.getJSONObject(i);

            if (!ObserverPlatform.defaultScope(Build.VERSION.SDK_INT)
                            .equals(surface.optString("scope"))
                    && !"api30_interactive".equals(surface.optString("scope"))) {
                continue;
            }

            if (EmptyTreeRecovery.missingInteractivePanel(surface.optString("coverage_basis"))) {
                return true;
            }

            JSONArray unavailable = surface.getJSONArray("unavailable_windows");
            JSONArray windows = surface.getJSONArray("windows");

            for (int j = 0; j < windows.length(); ++j) {
                JSONObject window = windows.getJSONObject(j);
                boolean missing = false;

                for (int k = 0; k < unavailable.length(); ++k) {
                    if (EmptyTreeRecovery.unavailableAccessibilityWindow(
                            unavailable.get(k), window.getInt("id"))) {
                        missing = true;
                    }
                }

                if (EmptyTreeRecovery.focusedApplicationMissing(
                        window.getInt("type"),
                        window.optBoolean("active"),
                        window.optBoolean("input_focused"),
                        missing)) {
                    return true;
                }
            }
        }

        return false;
    }

    private static JSONObject snapshot(JSONObject request) throws Exception {
        long timeout = request.optLong("timeout_ms", 5000);

        check(timeout >= 1 && timeout <= 30000, "timeout_bounds");
        long requestedAt = SystemClock.uptimeMillis();
        long deadline = Math.min(DEADLINES.get(), requestedAt + timeout);
        DEADLINES.set(deadline);
        phase(GuestPhase.Phase.GATE_BEGIN);
        SNAPSHOTS.acquire(deadline, SystemClock::uptimeMillis);
        phase(GuestPhase.Phase.GATE_ACQUIRED);
        sourceDeadline = deadline;

        try {
            final String expectedBoot = boot();
            JSONObject result =
                    EmptyTreeRecovery.read(
                            () -> observe(request),
                            () -> {
                                check(true, "deadline");
                                check(expectedBoot.equals(boot()), "boot_changed");
                                phase(GuestPhase.Phase.AX_REPAIR_BEGIN);

                                synchronized (ObserverMain.class) {
                                    detach();
                                    connect();
                                }

                                check(expectedBoot.equals(boot()), "boot_changed");
                                phase(GuestPhase.Phase.AX_REPAIR_READY);
                                check(true, "deadline");
                            },
                            ObserverMain::missingFocusedApplicationRoot,
                            ObserverPlatform.PASSION.equals(selectedProfile)
                                    ? SystemClock::uptimeMillis
                                    : null,
                            Thread::sleep,
                            deadline);
            JSONArray observedSurfaces = result.getJSONArray("surfaces");

            for (int i = 0; i < observedSurfaces.length(); ++i) {
                observedSurfaces
                        .getJSONObject(i)
                        .put(
                                "budget_diagnostic",
                                new JSONObject()
                                        .put("requested_timeout_ms", timeout)
                                        .put(
                                                "effective_timeout_ms",
                                                Math.max(0, deadline - requestedAt)));
            }

            return result;
        } finally {
            sourceDeadline = 0;
            SNAPSHOTS.release();
        }
    }

    private static JSONObject events(JSONObject request) throws Exception {
        connect();
        String op = request.getString("op");
        long now = SystemClock.uptimeMillis();
        SUBSCRIPTIONS.entrySet().removeIf(entry -> entry.getValue() < now);

        if ("events.subscribe".equals(op)) {
            check(SUBSCRIPTIONS.size() < 32, "subscription_limit");
            long armed = JOURNAL.watermark();
            String id = UUID.randomUUID().toString();
            SUBSCRIPTIONS.put(id, now + 60000);
            JSONObject baseline;
            boolean subscribed = false;

            try {
                baseline = snapshot(request);
                subscribed = true;
            } finally {
                if (!subscribed) {
                    SUBSCRIPTIONS.remove(id);
                }
            }

            return new JSONObject()
                    .put("schema", 1)
                    .put("ok", true)
                    .put("ready", !JOURNAL.gap(armed))
                    .put("gap", JOURNAL.gap(armed))
                    .put("subscription_id", id)
                    .put("baseline", baseline)
                    .put("cursor", cursor(armed))
                    .put("closing_cursor", cursor(JOURNAL.watermark()))
                    .put("baseline_atomic", false)
                    .put(
                            "coverage",
                            "accessibility_events_default_display;secondary_view_state_sampled")
                    .put("expires_uptime_ms", now + 60000);
        }

        String id = request.getString("subscription_id");
        check(SUBSCRIPTIONS.containsKey(id), "subscription_expired");

        if ("events.unsubscribe".equals(op)) {
            SUBSCRIPTIONS.remove(id);

            return new JSONObject().put("schema", 1).put("ok", true).put("unsubscribed", true);
        }

        check("events.read".equals(op), "unsupported_operation");
        JSONObject cursor = request.getJSONObject("cursor");
        check(EPOCH.equals(cursor.optString("source_epoch")), "source_restart");
        String decimal = cursor.getString("sequence");
        check(decimal.matches("0|[1-9][0-9]{0,18}"), "cursor_bounds");
        long sequence = Long.parseLong(decimal), wait = request.optLong("timeout_ms", 0);
        check(wait >= 0 && wait <= 30000, "timeout_bounds");
        SUBSCRIPTIONS.put(id, now + 60000);
        check(READERS.tryAcquire(), "event_reader_limit");
        java.util.List<EventJournal.Entry> records;

        try {
            records = JOURNAL.read(sequence, wait);
        } finally {
            READERS.release();
        }

        check(SUBSCRIPTIONS.containsKey(id) && running, "subscription_cancelled");
        JSONArray values = new JSONArray();
        long next = sequence;

        for (EventJournal.Entry entry : records) {
            values.put(
                    new JSONObject(entry.payload)
                            .put("source_seq", Long.toString(entry.sequence))
                            .put("source_epoch", EPOCH)
                            .put("ingest_uptime_ms", entry.time));
            next = entry.sequence;
        }

        return new JSONObject()
                .put("schema", 1)
                .put("ok", true)
                .put("events", values)
                .put("cursor", cursor(next))
                .put("gap", JOURNAL.gap(sequence))
                .put(
                        "coverage",
                        "accessibility_events_default_display;secondary_view_state_sampled");
    }

    private static JSONObject cursor(long sequence) throws Exception {
        return new JSONObject().put("source_epoch", EPOCH).put("sequence", Long.toString(sequence));
    }

    private static void shutdownPhase(String phase) {
        System.err.println("VOYEMU_UI_SHUTDOWN " + phase);
    }

    private static void wakeListener(String socketName) throws Exception {
        // close() on another thread need not wake a blocking Linux accept().
        // This connects only to our private listener; no request is dispatched.
        try (LocalSocket wake = new LocalSocket()) {
            wake.connect(
                    new android.net.LocalSocketAddress(
                            socketName, android.net.LocalSocketAddress.Namespace.ABSTRACT));
        }
    }

    private static void serve(LocalSocket socket, String auth, String socketName) {
        try (LocalSocket owned = socket) {
            owned.setSoTimeout(2000);
            DEADLINES.set(SystemClock.uptimeMillis() + 35000);
            JSONObject response;

            try {
                int peer = owned.getPeerCredentials().getUid();
                check(peer == 0 || peer == 2000, "peer_owner");
                JSONObject request =
                        new JSONObject(
                                new String(line(owned.getInputStream()), StandardCharsets.UTF_8));
                check(
                        request.optInt("schema") == 1 && auth.equals(request.optString("auth")),
                        "request_owner");
                owned.setSoTimeout(35000);
                String op = request.optString("op");

                if ("tree".equals(op) || "ui".equals(op)) {
                    GuestPhase.begin(Process.myPid());
                    phase(GuestPhase.Phase.REQUEST_ENTERED);
                    response = snapshot(request);
                } else if (op.startsWith("events.")) {
                    response = events(request);
                } else if ("hello".equals(op)) {
                    connect();
                    response =
                            new JSONObject()
                                    .put("schema", 1)
                                    .put("ok", true)
                                    .put("boot_id", boot())
                                    .put("observer_pid", Process.myPid())
                                    .put("api_level", Build.VERSION.SDK_INT)
                                    .put("observer_epoch", EPOCH)
                                    .put("uptime_ms", SystemClock.uptimeMillis())
                                    .put(
                                            "elapsed_realtime_ns",
                                            Long.toString(SystemClock.elapsedRealtimeNanos()))
                                    .put("nano_time", Long.toString(System.nanoTime()))
                                    .put("persistent", true)
                                    .put("parallel_surfaces", true)
                                    .put(
                                            "ui_scopes",
                                            Build.VERSION.SDK_INT == 30
                                                    ? new JSONArray()
                                                            .put("all_attached")
                                                            .put("interactive")
                                                    : new JSONArray().put("all_attached"));
                } else if ("close".equals(op)) {
                    response =
                            new JSONObject().put("schema", 1).put("ok", true).put("closed", true);
                    running = false;
                    JOURNAL.close();
                } else {
                    throw new IllegalArgumentException("unsupported_operation");
                }
            } catch (Exception error) {
                // checkstyle-allow IllegalCatch: every request failure becomes an error response
                String reason = error.getMessage();

                if (reason == null || !reason.matches("[a-z0-9_]{1,80}")) {
                    reason = "observer_unavailable";
                }

                response =
                        new JSONObject()
                                .put("schema", 1)
                                .put("ok", false)
                                .put("reason", reason)
                                .put("error_type", error.getClass().getSimpleName())
                                .put(
                                        "cause_type",
                                        error.getCause() == null
                                                ? ""
                                                : error.getCause().getClass().getSimpleName())
                                .put("failure", new JSONObject(FailureMetadata.fields(error)));
            }

            byte[] bytes = response.toString().getBytes(StandardCharsets.UTF_8);

            if (bytes.length > MAX_BYTES) {
                throw new IllegalStateException("output_limit");
            }

            phase(GuestPhase.Phase.RESPONSE_WRITE_BEGIN);
            owned.getOutputStream().write(bytes);
            owned.getOutputStream().write(10);
            owned.getOutputStream().flush();
            phase(GuestPhase.Phase.RESPONSE_WRITE_END);

            if (!running) {
                shutdownPhase("close_ack");
                wakeListener(socketName);
            }
        } catch (Exception ignored) {
            // checkstyle-allow IllegalCatch: a connection worker ends with EOF to its owner;
            // never log raw requests or source data.
        } finally {
            GuestPhase.end();
            DEADLINES.remove();
            COUNTS.remove();
            SURFACE_SIZE.remove();
        }
    }

    private static void startSourceWatchdog() {
        Thread watchdog =
                new Thread(
                        () -> {
                            while (running) {
                                long limit = sourceDeadline;

                                if (limit != 0 && SystemClock.uptimeMillis() > limit + 1000) {
                                    Runtime.getRuntime().halt(124);
                                }

                                try {
                                    Thread.sleep(100);
                                } catch (InterruptedException ignored) {
                                    return;
                                }
                            }
                        },
                        "voyemu-ui-source-deadline");

        watchdog.setDaemon(true);
        watchdog.start();
    }

    public static void main(String[] args) throws Exception {
        if ((args.length != 2 && args.length != 3)
                || !args[0].matches("[a-f0-9]{32}")
                || !args[1].matches("[a-f0-9]{32}")
                || (Process.myUid() != 0 && Process.myUid() != 2000)
                || !ObserverPlatform.allowed(
                        Build.VERSION.SDK_INT, args.length == 3 ? args[2] : "")) {
            throw new IllegalArgumentException("owned_api28_observer_required");
        }

        selectedProfile = args.length == 3 ? args[2] : "";
        VIEW_SERVER.onInvalidation(type -> {
            try {
                JOURNAL.append(
                        SystemClock.uptimeMillis(),
                        new JSONObject()
                                .put("type", type)
                                .put("coverage", "viewserver_window_and_focus_events")
                                .put("clock_domain", "android_uptime_ms")
                                .toString());
            } catch (JSONException ignored) {
                // The journal records only events that serialize.
            }
        });
        callbacks = new HandlerThread("voyemu-ui-callbacks");
        callbacks.start();
        Object displayManager =
                Class.forName("android.hardware.display.DisplayManagerGlobal")
                        .getMethod("getInstance")
                        .invoke(null);
        android.hardware.display.DisplayManager.DisplayListener displayListener =
                new android.hardware.display.DisplayManager.DisplayListener() {
                    private void changed(int id, String type) {
                        long generation = DISPLAY_GENERATION.incrementAndGet();

                        try {
                            JOURNAL.append(
                                    SystemClock.uptimeMillis(),
                                    new JSONObject()
                                            .put("type", type)
                                            .put("guest_display_id", id)
                                            .put("display_generation", Long.toString(generation))
                                            .put("coverage", "display_lifecycle")
                                            .toString());
                        } catch (JSONException ignored) {
                            // The journal records only events that serialize.
                        }
                    }

                    @Override
                    public void onDisplayAdded(int id) {
                        changed(id, "display_added");
                    }

                    @Override
                    public void onDisplayRemoved(int id) {
                        changed(id, "display_removed");
                    }

                    @Override
                    public void onDisplayChanged(int id) {
                        changed(id, "display_changed");
                    }
                };
        displayManager
                .getClass()
                .getMethod(
                        "registerDisplayListener",
                        android.hardware.display.DisplayManager.DisplayListener.class,
                        android.os.Handler.class)
                .invoke(
                        displayManager,
                        displayListener,
                        new android.os.Handler(callbacks.getLooper()));
        startSourceWatchdog();
        java.util.concurrent.ThreadPoolExecutor workers =
                new java.util.concurrent.ThreadPoolExecutor(
                        8,
                        8,
                        0,
                        java.util.concurrent.TimeUnit.MILLISECONDS,
                        new java.util.concurrent.ArrayBlockingQueue<Runnable>(8));
        java.util.Set<LocalSocket> sockets = java.util.concurrent.ConcurrentHashMap.newKeySet();
        final String socketName = "voyemu-ui-" + args[0];

        try (LocalServerSocket server = new LocalServerSocket(socketName)) {
            while (running) {
                LocalSocket socket;

                try {
                    socket = server.accept();
                } catch (java.io.IOException error) {
                    if (!running) {
                        break;
                    }

                    throw error;
                }

                if (!running) {
                    socket.close();
                    shutdownPhase("accept_released");
                    break;
                }

                sockets.add(socket);

                try {
                    workers.execute(() -> {
                        try {
                            serve(socket, args[1], socketName);
                        } finally {
                            sockets.remove(socket);
                        }
                    });
                } catch (java.util.concurrent.RejectedExecutionException error) {
                    sockets.remove(socket);
                    socket.close();
                }
            }
        } finally {
            shutdownPhase("cleanup_start");
            running = false;
            JOURNAL.close();
            SUBSCRIPTIONS.clear();

            for (LocalSocket socket : sockets) {
                try {
                    socket.close();
                } catch (java.io.IOException ignored) {
                    // Best-effort close while the server stops.
                }
            }

            workers.shutdownNow();
            workers.awaitTermination(2, java.util.concurrent.TimeUnit.SECONDS);
            shutdownPhase("workers_done");
            displayManager
                    .getClass()
                    .getMethod(
                            "unregisterDisplayListener",
                            android.hardware.display.DisplayManager.DisplayListener.class)
                    .invoke(displayManager, displayListener);
            shutdownPhase("display_listener_done");
            detach();
            shutdownPhase("automation_done");
            VIEW_SERVER.close();
            shutdownPhase("viewserver_done");
            callbacks.quitSafely();
            callbacks.join(1000);
            shutdownPhase("callbacks_done");
        }
    }
}
