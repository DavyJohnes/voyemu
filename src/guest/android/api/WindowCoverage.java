package com.voyemu.api;

/** Bars alone cannot prove that the foreground application's or service's tree was captured. */
final class WindowCoverage {
    static final class Owner {
        final int display, type;
        final String id, packageName;
        final int[] frame;
        final Boolean touchable, visible, hasSurface, focusable, drawn, viewVisible;

        Owner(int display, int type, String packageName, int[] frame) {
            this(null, display, type, packageName, frame);
        }

        Owner(String id, int display, int type, String packageName, int[] frame) {
            this(
                    id,
                    display,
                    type,
                    packageName,
                    frame,
                    new State(null, null, null, null, null, null));
        }

        Owner(String id, int display, int type, String packageName, int[] frame, State state) {
            this.drawn = state.drawn;
            this.viewVisible = state.viewVisible;
            this.touchable = state.touchable;
            this.visible = state.visible;
            this.hasSurface = state.hasSurface;
            this.focusable = state.focusable;
            this.id = id;
            this.display = display;
            this.type = type;
            this.packageName = packageName;
            this.frame = frame;
        }
    }

    /** WindowManager state flags of an owner window; null when the dump omits one. */
    static final class State {
        final Boolean touchable, visible, hasSurface, focusable, drawn, viewVisible;

        State(
                Boolean touchable,
                Boolean visible,
                Boolean hasSurface,
                Boolean focusable,
                Boolean drawn,
                Boolean viewVisible) {
            this.touchable = touchable;
            this.visible = visible;
            this.hasSurface = hasSurface;
            this.focusable = focusable;
            this.drawn = drawn;
            this.viewVisible = viewVisible;
        }
    }

    private final int display;
    private final boolean unfocusedServiceSupported, navigationPanelSupported;
    private final java.util.Map<String, Integer> navigationRoots =
            new java.util.HashMap<String, Integer>();

    WindowCoverage() {
        this(0);
    }

    WindowCoverage(int display) {
        this(display, false);
    }

    WindowCoverage(int display, boolean unfocusedServiceSupported) {
        this(display, unfocusedServiceSupported, "");
    }

    WindowCoverage(int display, boolean unfocusedServiceSupported, String platform) {
        navigationPanelSupported =
                unfocusedServiceSupported && ObserverPlatform.PASSION.equals(platform);

        if (display < 0) {
            throw new IllegalArgumentException("display_unavailable");
        }

        this.display = display;
        this.unfocusedServiceSupported = unfocusedServiceSupported;
    }

    private boolean applicationRoot, unclassifiedRoot, focusedVehicleDrawerRoot, unresolvedPanel;
    private Owner verifiedOwner;

    void add(int type, boolean hasRoot) {
        if (type == 1 && hasRoot) {
            applicationRoot = true;
        }

        if (type == -1 && hasRoot) {
            unclassifiedRoot = true;
        }
    }

    // Observed API30 non-focusable navigation panels can be absent from the AX inventory.
    // An application root underneath cannot attest to their content or input targets.
    static boolean unresolvedNavigationPanel(
            int display, Owner owner, Boolean drawn, Boolean viewVisible) {
        return owner.display == display
                && owner.type == 2024
                && Boolean.FALSE.equals(owner.focusable)
                && Boolean.TRUE.equals(owner.touchable)
                && Boolean.TRUE.equals(owner.visible)
                && Boolean.TRUE.equals(owner.hasSurface)
                && Boolean.TRUE.equals(drawn)
                && Boolean.TRUE.equals(viewVisible)
                && owner.id != null
                && !owner.id.isEmpty()
                && owner.frame != null
                && owner.frame.length == 4
                && owner.frame[0] < owner.frame[2]
                && owner.frame[1] < owner.frame[3];
    }

    void addOwnedNavigationPanel(
            int type,
            boolean hasRoot,
            String packageName,
            int[] frame,
            int[] viewport,
            Iterable<Owner> owners) {
        if (!navigationPanelSupported
                || type != 3
                || !hasRoot
                || !"com.voyah.os.carlauncher".equals(packageName)
                || !java.util.Arrays.equals(frame, viewport)) {
            return;
        }

        Owner match = null;
        int count = 0;

        for (Owner owner : owners) {
            if (packageName.equals(owner.packageName)
                    && java.util.Arrays.equals(frame, owner.frame)
                    && unresolvedNavigationPanel(display, owner, owner.drawn, owner.viewVisible)) {
                match = owner;
                ++count;
            }
        }

        if (count == 1) {
            navigationRoots.put(
                    match.id,
                    navigationRoots.containsKey(match.id) ? navigationRoots.get(match.id) + 1 : 1);
        }
    }

    boolean recordNavigationPanel(Owner owner, Boolean drawn, Boolean viewVisible) {
        if (!unresolvedNavigationPanel(display, owner, drawn, viewVisible)) {
            return false;
        }

        if (navigationPanelSupported && Integer.valueOf(1).equals(navigationRoots.get(owner.id))) {
            verifiedOwner = owner;

            return false;
        }

        unresolvedPanel = true;

        return true;
    }

    void verifyContent(boolean selector, int nodes, int windows) {
        verify();

        if (!unresolvedPanel && (selector ? windows == 0 : nodes == 0)) {
            throw new IllegalStateException("empty_tree_unverified");
        }
    }

    boolean reconcilesNavigationPanels(boolean interactive) {
        return interactive || navigationPanelSupported;
    }

    private boolean vehicleDrawerRoot(
            int type, boolean hasRoot, boolean active, boolean focused, String owner) {
        return !unfocusedServiceSupported
                && display == 0
                && type == 3
                && hasRoot
                && active
                && focused
                && "com.qinggan.app.launcher".equals(owner);
    }

    void noteFocusedSystemRoot(
            int type, boolean hasRoot, boolean active, boolean focused, String owner) {
        focusedVehicleDrawerRoot |= vehicleDrawerRoot(type, hasRoot, active, focused, owner);
    }

    boolean needsOwnerVerification() {
        return !applicationRoot && (unclassifiedRoot || focusedVehicleDrawerRoot);
    }

    void addOwnedVehicleDrawer(
            int type,
            boolean refreshed,
            boolean active,
            boolean focused,
            String owner,
            int[] frame,
            Owner wms) {
        if (!vehicleDrawerRoot(type, refreshed, active, focused, owner)
                || wms == null
                || wms.id == null
                || wms.display != display
                || wms.type != 2008
                || !owner.equals(wms.packageName)
                || !java.util.Arrays.equals(frame, wms.frame)
                || !Boolean.TRUE.equals(wms.focusable)
                || !Boolean.TRUE.equals(wms.touchable)
                || !Boolean.TRUE.equals(wms.visible)
                || !Boolean.TRUE.equals(wms.hasSurface)
                || !Boolean.TRUE.equals(wms.drawn)
                || !Boolean.TRUE.equals(wms.viewVisible)) {
            return;
        }

        verifiedOwner = wms;
    }

    void addOwnedUnclassified(
            int type,
            boolean hasRoot,
            boolean active,
            boolean focused,
            String packageName,
            int[] frame,
            Iterable<Owner> owners) {
        if (!unclassifiedCandidate(type, hasRoot, active, focused, packageName, frame)) {
            return;
        }

        java.util.List<Owner> rawMatches = new java.util.ArrayList<Owner>();

        for (Owner owner : owners) {
            if (owner.display == display
                    && owner.type >= 2000
                    && owner.type <= 2999
                    && packageName.equals(owner.packageName)
                    && java.util.Arrays.equals(frame, owner.frame)) {
                rawMatches.add(owner);
            }
        }

        int matches = rawMatches.size();

        // Legacy API28 path keeps the historical layout-only correlation, only for a window
        // that is both active and focused; API28 never owner-verifies unfocused service panels.
        if (matches == 1 && !unfocusedServiceSupported) {
            if (!active || !focused) {
                return;
            }

            verifiedOwner = rawMatches.get(0);

            return;
        }

        if (matches == 0 || !unfocusedServiceSupported) {
            return;
        }

        Owner resolved = resolveFlagged(rawMatches, active);

        if (resolved != null) {
            verifiedOwner = resolved;
        }
    }

    private static boolean unclassifiedCandidate(
            int type,
            boolean hasRoot,
            boolean active,
            boolean focused,
            String packageName,
            int[] frame) {
        return type == -1
                && hasRoot
                && active == focused
                && packageName != null
                && !packageName.isEmpty()
                && frame != null
                && frame.length == 4
                && frame[0] < frame[2]
                && frame[1] < frame[3];
    }

    /**
     * API30 requires explicit WMS flags even for a single candidate; when multiple window states
     * share display/type/package/frame (OEM panel plus its NOT_FOCUSABLE NOT_TOUCHABLE dim surface)
     * resolve the owner exclusively. API30 AccessibilityController excludes NOT_TOUCHABLE windows
     * (except the dock divider); a NOT_FOCUSABLE surface can never own a focused accessibility
     * window. Unknown flags or a remaining ambiguity stays rejected and returns null.
     */
    private static Owner resolveFlagged(java.util.List<Owner> rawMatches, boolean active) {
        Owner resolved = null;
        int resolvedMatches = 0;

        for (Owner owner : rawMatches) {
            if (Boolean.FALSE.equals(owner.touchable)
                    || Boolean.FALSE.equals(owner.visible)
                    || Boolean.FALSE.equals(owner.hasSurface)
                    || Boolean.FALSE.equals(owner.focusable)) {
                continue;
            }

            if (!Boolean.TRUE.equals(owner.touchable)
                    || !Boolean.TRUE.equals(owner.visible)
                    || !Boolean.TRUE.equals(owner.hasSurface)) {
                return null;
            }

            if (!Boolean.TRUE.equals(owner.focusable)) {
                return null;
            }

            if (owner.id == null || owner.id.isEmpty()) {
                return null;
            }

            if (!active && owner.type != 2106) {
                return null;
            }

            ++resolvedMatches;
            resolved = owner;
        }

        return resolvedMatches == 1 ? resolved : null;
    }

    Owner verifiedOwner() {
        return verifiedOwner;
    }

    String basis() {
        return unresolvedPanel
                ? "unresolved_displayed_navigation_panel"
                : verifiedOwner != null && verifiedOwner.type == 2024
                        ? "verified_navigation_panel"
                        : applicationRoot
                                ? "accessibility_application_root"
                                : verifiedOwner != null
                                                && verifiedOwner.type == 2008
                                                && focusedVehicleDrawerRoot
                                        ? "verified_owned_vehicle_drawer"
                                        : "verified_owned_unclassified_window";
    }

    void verify() {
        if (!unresolvedPanel && !applicationRoot && verifiedOwner == null) {
            throw new IllegalStateException("application_window_unverified");
        }
    }
}
