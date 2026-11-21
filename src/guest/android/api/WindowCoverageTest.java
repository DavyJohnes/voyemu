package com.voyemu.api;

public final class WindowCoverageTest {
    private static void reject(WindowCoverage coverage) {
        try {
            coverage.verify();
            throw new AssertionError("system-only tree accepted");
        } catch (IllegalStateException expected) {
            if (!"application_window_unverified".equals(expected.getMessage())) {
                throw expected;
            }
        }
    }

    private static void navigationPanelCoverage() {
        panelCoverage();
        panelDenials();
    }

    private static void panelCoverage() {
        int[] frame = {0, 0, 1920, 720};
        WindowCoverage.Owner panel =
                new WindowCoverage.Owner(
                        "deee05d",
                        0,
                        2024,
                        "com.voyah.os.carlauncher",
                        frame,
                        new WindowCoverage.State(true, true, true, false, null, null));
        WindowCoverage underlying = new WindowCoverage(0, true);

        underlying.add(1, true);
        underlying.verify(); // This root alone previously concealed the displayed shade.

        if (!WindowCoverage.unresolvedNavigationPanel(0, panel, true, true)) {
            throw new AssertionError("visible drawn Passion shade concealed by application root");
        }

        WindowCoverage missingRoot = new WindowCoverage(0, true);
        missingRoot.add(3, true);
        reject(missingRoot);

        if (!missingRoot.recordNavigationPanel(panel, true, true)) {
            throw new AssertionError("missing panel identity lost");
        }

        missingRoot.verifyContent(false, 0, 0);
        missingRoot.verifyContent(true, 0, 0);
        WindowCoverage ordinaryEmpty = new WindowCoverage();
        ordinaryEmpty.add(1, true);

        try {
            ordinaryEmpty.verifyContent(false, 0, 1);
            throw new AssertionError("ordinary empty tree accepted");
        } catch (IllegalStateException expected) {
            if (!"empty_tree_unverified".equals(expected.getMessage())) {
                throw expected;
            }
        }

        try {
            ordinaryEmpty.verifyContent(true, 0, 0);
            throw new AssertionError("ordinary empty selector window accepted");
        } catch (IllegalStateException expected) {
            if (!"empty_tree_unverified".equals(expected.getMessage())) {
                throw expected;
            }
        }

        missingRoot.verify(); // Partial evidence is returned, never an invented verified AX owner.

        if (missingRoot.verifiedOwner() != null
                || !"unresolved_displayed_navigation_panel".equals(missingRoot.basis())) {
            throw new AssertionError("partial shade incorrectly labelled verified");
        }
    }

    private static void panelDenials() {
        int[] frame = {0, 0, 1920, 720};
        WindowCoverage.Owner secondary =
                new WindowCoverage.Owner(
                        "7a87b56",
                        1,
                        2024,
                        "com.voyah.os.carlauncher",
                        frame,
                        new WindowCoverage.State(false, true, true, false, null, null));

        if (WindowCoverage.unresolvedNavigationPanel(1, secondary, true, true)) {
            throw new AssertionError(
                    "passive NOT_TOUCHABLE secondary shell counted as interactive gap");
        }

        WindowCoverage.Owner closed =
                new WindowCoverage.Owner(
                        "12544df",
                        0,
                        2024,
                        "com.voyah.os.carlauncher",
                        frame,
                        new WindowCoverage.State(false, true, true, false, null, null));

        if (WindowCoverage.unresolvedNavigationPanel(0, closed, true, true)) {
            throw new AssertionError(
                    "actual closed HOME flags0x01040338 reported as missing interactive panel");
        }

        WindowCoverage.Owner unknownTouch =
                new WindowCoverage.Owner(
                        "12544df",
                        0,
                        2024,
                        "com.voyah.os.carlauncher",
                        frame,
                        new WindowCoverage.State(null, true, true, false, null, null));

        if (WindowCoverage.unresolvedNavigationPanel(0, unknownTouch, true, true)) {
            throw new AssertionError("unknown touchability admitted as known interactive panel");
        }

        if (WindowCoverage.unresolvedNavigationPanel(0, secondary, true, true)) {
            throw new AssertionError("foreign display shade leaked");
        }

        WindowCoverage.Owner navigationBar =
                new WindowCoverage.Owner(
                        "nav",
                        0,
                        2019,
                        "com.voyah.os.carlauncher",
                        frame,
                        new WindowCoverage.State(true, true, true, false, null, null));

        if (WindowCoverage.unresolvedNavigationPanel(0, navigationBar, true, true)) {
            throw new AssertionError("NAVIGATION_BAR2019 confused with NAVIGATION_BAR_PANEL2024");
        }

        deniedPanels();
    }

    private static void deniedPanels() {
        int[] frame = {0, 0, 1920, 720};

        for (int denial = 0; denial < 9; ++denial) {
            WindowCoverage.Owner other =
                    new WindowCoverage.Owner(
                            denial == 0 ? null : "panel",
                            0,
                            denial == 1 ? 2106 : 2024,
                            "fixture.owner",
                            denial == 2 ? new int[] {0, 0, 0, 720} : frame,
                            new WindowCoverage.State(
                                    true,
                                    denial != 3,
                                    denial == 4 ? null : true,
                                    denial == 5,
                                    null,
                                    null));

            if (WindowCoverage.unresolvedNavigationPanel(
                    0,
                    other,
                    denial == 6 ? Boolean.FALSE : denial == 7 ? null : Boolean.TRUE,
                    denial != 8)) {
                throw new AssertionError("non-displayed or unsupported panel admitted: " + denial);
            }
        }
    }

    private static void navigationPanelRoot() {
        panelRoot();
        panelRootDenials();
    }

    private static void panelRoot() {
        int[] frame = {0, 0, 1920, 720};
        WindowCoverage.Owner panel =
                new WindowCoverage.Owner(
                        "12544df",
                        0,
                        2024,
                        "com.voyah.os.carlauncher",
                        frame,
                        new WindowCoverage.State(true, true, true, false, true, true));
        WindowCoverage good = new WindowCoverage(0, true, ObserverPlatform.PASSION);

        if (!good.reconcilesNavigationPanels(false) || !good.reconcilesNavigationPanels(true)) {
            throw new AssertionError(
                    "actual primary drag default scope bypasses proven panel ownership");
        }

        if (new WindowCoverage(0, true, ObserverPlatform.QINGGAN11)
                        .reconcilesNavigationPanels(false)
                || new WindowCoverage(0, false, ObserverPlatform.PASSION)
                        .reconcilesNavigationPanels(false)
                || new WindowCoverage(0, true, "foreign").reconcilesNavigationPanels(false)) {
            throw new AssertionError("default panel policy escaped exact Passion/API30 binding");
        }

        good.add(3, true);
        good.addOwnedNavigationPanel(
                3,
                true,
                "com.voyah.os.carlauncher",
                frame,
                frame,
                java.util.Collections.singletonList(panel));

        if (good.recordNavigationPanel(panel, true, true)) {
            throw new AssertionError("real AX shade root still unavailable");
        }

        good.verifyContent(false, 47, 1);

        if (good.verifiedOwner() != panel || !"verified_navigation_panel".equals(good.basis())) {
            throw new AssertionError("verified 47-node navigation panel root lost owner proof");
        }
    }

    private static void panelRootDenials() {
        int[] frame = {0, 0, 1920, 720};

        for (int denial = 0; denial < 18; ++denial) {
            WindowCoverage denied =
                    new WindowCoverage(
                            denial == 0 ? 1 : 0,
                            denial != 1,
                            denial == 2
                                    ? ObserverPlatform.QINGGAN11
                                    : denial == 3 ? "foreign" : ObserverPlatform.PASSION);
            WindowCoverage.Owner candidate = deniedRootCandidate(denial, frame);
            java.util.List<WindowCoverage.Owner> owners =
                    denial == 16
                            ? java.util.Arrays.asList(candidate, candidate)
                            : java.util.Collections.singletonList(candidate);
            addDeniedPanel(denied, denial, frame, owners);

            if (denial == 17) {
                denied.addOwnedNavigationPanel(
                        3, true, "com.voyah.os.carlauncher", frame, frame, owners);
            }

            denied.recordNavigationPanel(candidate, candidate.drawn, candidate.viewVisible);

            if (denied.verifiedOwner() != null
                    || "verified_navigation_panel".equals(denied.basis())) {
                throw new AssertionError("unproven system panel admitted: " + denial);
            }
        }
    }

    private static WindowCoverage.Owner deniedRootCandidate(int denial, int[] frame) {
        return new WindowCoverage.Owner(
                "12544df",
                0,
                2024,
                "com.voyah.os.carlauncher",
                frame,
                new WindowCoverage.State(
                        denial != 10,
                        denial != 11,
                        denial == 12 ? null : true,
                        denial == 13,
                        denial != 14,
                        denial == 15 ? null : true));
    }

    private static void addDeniedPanel(
            WindowCoverage denied,
            int denial,
            int[] frame,
            java.util.List<WindowCoverage.Owner> owners) {
        denied.addOwnedNavigationPanel(
                denial == 4 ? 1 : denial == 5 ? -1 : 3,
                denial != 6,
                denial == 7 ? "foreign.owner" : "com.voyah.os.carlauncher",
                denial == 8 ? new int[] {0, 0, 140, 720} : frame,
                denial == 9 ? new int[] {0, 0, 1920, 660} : frame,
                owners);
    }

    public static void main(String[] args) throws Exception {
        navigationPanelCoverage();
        navigationPanelRoot();
        WindowCoverage bars = focusedRoots();
        WindowCoverage application = ownedUnclassified();
        WindowCoverage.Owner panel = unfocusedCurtain();
        invalidOwners(panel);
        emptyTreeRecovery(bars, application);
    }

    private static WindowCoverage focusedRoots() {
        WindowCoverage bars = new WindowCoverage();

        for (int type : new int[] {3, 3, -1}) {
            bars.add(type, true);
        }

        reject(bars);
        WindowCoverage unavailableApp = new WindowCoverage();
        unavailableApp.add(1, false);
        unavailableApp.add(3, true);
        reject(unavailableApp);
        String launcher = "com.qinggan.app.launcher";
        int[] drawerFrame = {0, 0, 1224, 720};
        WindowCoverage.Owner drawer =
                new WindowCoverage.Owner(
                        "ad70633",
                        0,
                        2008,
                        launcher,
                        drawerFrame,
                        new WindowCoverage.State(true, true, true, true, true, true));
        WindowCoverage drawerCoverage = new WindowCoverage();
        drawerCoverage.noteFocusedSystemRoot(3, true, true, true, launcher);

        if (!drawerCoverage.needsOwnerVerification()) {
            throw new AssertionError("drawer should need WMS verification");
        }

        reject(drawerCoverage);
        drawerCoverage.addOwnedVehicleDrawer(3, true, true, true, launcher, drawerFrame, drawer);
        drawerCoverage.verify();

        if (drawerCoverage.verifiedOwner() != drawer
                || !"verified_owned_vehicle_drawer".equals(drawerCoverage.basis())) {
            throw new AssertionError("drawer should have stable owner");
        }

        WindowCoverage wrongFrame = new WindowCoverage();
        wrongFrame.noteFocusedSystemRoot(3, true, true, true, launcher);
        wrongFrame.addOwnedVehicleDrawer(
                3, true, true, true, launcher, new int[] {0, 0, 1920, 720}, drawer);
        reject(wrongFrame);

        return bars;
    }

    private static void unclassifiedDenials(java.util.List<WindowCoverage.Owner> owners) {
        int[] frame = {0, 0, 1920, 720};

        for (int denial = 0; denial < 10; ++denial) {
            WindowCoverage denied = new WindowCoverage();
            java.util.List<WindowCoverage.Owner> inventory = owners;

            if (denial == 6) {
                inventory =
                        java.util.Collections.singletonList(
                                new WindowCoverage.Owner(1, 2106, "fixture.vehicle", frame));
            }

            if (denial == 7) {
                inventory =
                        java.util.Collections.singletonList(
                                new WindowCoverage.Owner(0, 1, "fixture.vehicle", frame));
            }

            if (denial == 8) {
                inventory = java.util.Collections.emptyList();
            }

            if (denial == 9) {
                inventory = java.util.Arrays.asList(owners.get(0), owners.get(0));
            }

            denied.addOwnedUnclassified(
                    denial == 0 ? 3 : -1,
                    denial != 1,
                    denial != 2,
                    denial != 3,
                    denial == 4 ? "foreign.owner" : "fixture.vehicle",
                    denial == 5 ? new int[] {0, 0, 100, 100} : frame,
                    inventory);
            reject(denied);
        }
    }

    private static WindowCoverage ownedUnclassified() {
        WindowCoverage application = new WindowCoverage();

        application.add(3, true);
        application.add(1, true);
        application.add(-1, false);
        application.verify();
        int[] frame = {0, 0, 1920, 720};
        java.util.List<WindowCoverage.Owner> owners =
                java.util.Collections.singletonList(
                        new WindowCoverage.Owner(0, 2106, "fixture.vehicle", frame));
        WindowCoverage service = new WindowCoverage();
        service.add(-1, true);
        reject(service); // A real root with unknown AX type alone is not sufficient.
        service.addOwnedUnclassified(-1, true, true, true, "fixture.vehicle", frame, owners);
        service.verify();

        if (service.verifiedOwner() != owners.get(0)) {
            throw new AssertionError("owner correlation lost");
        }

        if (!"verified_owned_unclassified_window".equals(service.basis())) {
            throw new AssertionError();
        }

        if (!"accessibility_application_root".equals(application.basis())) {
            throw new AssertionError();
        }

        unclassifiedDenials(owners);
        WindowCoverage secondaryService = new WindowCoverage(7);
        secondaryService.add(-1, true);
        secondaryService.addOwnedUnclassified(
                -1, true, true, true, "fixture.vehicle", frame, owners);
        reject(secondaryService);
        java.util.List<WindowCoverage.Owner> secondaryOwners =
                java.util.Collections.singletonList(
                        new WindowCoverage.Owner(7, 2106, "fixture.vehicle", frame));
        secondaryService.addOwnedUnclassified(
                -1, true, true, true, "fixture.vehicle", frame, secondaryOwners);
        secondaryService.verify();

        if (secondaryService.verifiedOwner().display != 7) {
            throw new AssertionError("secondary owner was inferred from primary");
        }

        try {
            new WindowCoverage(-1);
            throw new AssertionError("negative display accepted");
        } catch (IllegalArgumentException expected) {
            // A negative display must be rejected.
        }

        return application;
    }

    private static WindowCoverage.Owner unfocusedCurtain() {
        int[] frame = {0, 0, 1920, 720};
        WindowCoverage curtain = new WindowCoverage(0, true);

        curtain.add(-1, true);
        WindowCoverage.Owner panel =
                new WindowCoverage.Owner(
                        "ed4c9c7",
                        0,
                        2106,
                        "com.qinggan.app.hiboard",
                        frame,
                        new WindowCoverage.State(true, true, true, true, null, null));
        WindowCoverage.Owner dim =
                new WindowCoverage.Owner(
                        "3ca79eb",
                        0,
                        2106,
                        "com.qinggan.app.hiboard",
                        frame,
                        new WindowCoverage.State(false, true, true, false, null, null));
        curtain.addOwnedUnclassified(
                -1,
                true,
                false,
                false,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Arrays.asList(panel, dim));
        curtain.verify();

        if (curtain.verifiedOwner() != panel) {
            throw new AssertionError("non-touchable dim surface correlated");
        }

        WindowCoverage api28Curtain = new WindowCoverage(0);
        api28Curtain.add(-1, true);
        api28Curtain.addOwnedUnclassified(
                -1,
                true,
                false,
                false,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Arrays.asList(panel, dim));
        reject(api28Curtain);
        WindowCoverage api28UnfocusedSingle = new WindowCoverage(0);
        api28UnfocusedSingle.add(-1, true);
        api28UnfocusedSingle.addOwnedUnclassified(
                -1,
                true,
                false,
                false,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Collections.singletonList(panel));
        reject(api28UnfocusedSingle);
        WindowCoverage focusedCurtain = new WindowCoverage(0, true);
        focusedCurtain.add(-1, true);
        WindowCoverage.Owner bar =
                new WindowCoverage.Owner(
                        "ed4c9c7",
                        0,
                        2106,
                        "com.qinggan.app.hiboard",
                        frame,
                        new WindowCoverage.State(true, true, true, true, null, null));
        WindowCoverage.Owner nonFocusableDim =
                new WindowCoverage.Owner(
                        "3ca79eb",
                        0,
                        2106,
                        "com.qinggan.app.hiboard",
                        frame,
                        new WindowCoverage.State(false, true, true, false, null, null));
        focusedCurtain.addOwnedUnclassified(
                -1,
                true,
                true,
                true,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Arrays.asList(bar, nonFocusableDim));
        focusedCurtain.verify();

        if (focusedCurtain.verifiedOwner() != bar) {
            throw new AssertionError("active dim ambiguity resolved to panel");
        }

        WindowCoverage api28Focused = new WindowCoverage(0);
        api28Focused.add(-1, true);
        api28Focused.addOwnedUnclassified(
                -1,
                true,
                true,
                true,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Arrays.asList(bar, nonFocusableDim));
        reject(api28Focused);
        WindowCoverage dims = new WindowCoverage(0, true);
        dims.add(-1, true);
        dims.addOwnedUnclassified(
                -1,
                true,
                true,
                true,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Arrays.asList(nonFocusableDim, nonFocusableDim));
        reject(dims);
        WindowCoverage unknownFlagsFocused = new WindowCoverage(0, true);
        unknownFlagsFocused.add(-1, true);
        WindowCoverage.Owner noFlags =
                new WindowCoverage.Owner(
                        "ff0000f",
                        0,
                        2106,
                        "com.qinggan.app.hiboard",
                        frame,
                        new WindowCoverage.State(true, true, true, null, null, null));
        unknownFlagsFocused.addOwnedUnclassified(
                -1,
                true,
                true,
                true,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Arrays.asList(noFlags, nonFocusableDim));
        reject(unknownFlagsFocused);
        WindowCoverage twoTouchableFocused = new WindowCoverage(0, true);
        twoTouchableFocused.add(-1, true);
        twoTouchableFocused.addOwnedUnclassified(
                -1,
                true,
                true,
                true,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Arrays.asList(bar, bar));
        reject(twoTouchableFocused);

        return panel;
    }

    private static void invalidOwners(WindowCoverage.Owner panel) {
        int[] frame = {0, 0, 1920, 720};
        // Each negative case mutates exactly one causal predicate while every other owner
        // field is a fully valid 9-arg owner (focusable explicit TRUE); a null/wrong value in
        // an 8-arg fixture (focusable NULL) would make all cases reject through the same
        // missing-focusable predicate, proving nothing about the predicate under test.
        WindowCoverage.Owner[] invalidOwners = {
            new WindowCoverage.Owner(
                    "bad",
                    1,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(true, true, true, true, null, null)), // display
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2011,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(true, true, true, true, null, null)), // type
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2106,
                    "foreign.owner",
                    frame,
                    new WindowCoverage.State(true, true, true, true, null, null)), // package
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    new int[] {0, 0, 100, 100},
                    new WindowCoverage.State(true, true, true, true, null, null)), // frame
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(
                            null, true, true, true, null, null)), // touchable unknown
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(
                            false, true, true, true, null, null)), // touchable false
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(
                            true, null, true, true, null, null)), // visible unknown
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(true, false, true, true, null, null)), // visible false
            new WindowCoverage.Owner(
                    null,
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(true, true, true, true, null, null)), // id null
            new WindowCoverage.Owner(
                    "",
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(true, true, true, true, null, null)), // id empty
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(
                            true, true, null, true, null, null)), // surface unknown
            new WindowCoverage.Owner(
                    "bad",
                    0,
                    2106,
                    "com.qinggan.app.hiboard",
                    frame,
                    new WindowCoverage.State(true, true, false, true, null, null)) // surface false
        };

        for (WindowCoverage.Owner invalid : invalidOwners) {
            WindowCoverage denied = new WindowCoverage(0, true);
            denied.add(-1, true);
            denied.addOwnedUnclassified(
                    -1,
                    true,
                    false,
                    false,
                    "com.qinggan.app.hiboard",
                    frame,
                    java.util.Collections.singletonList(invalid));
            reject(denied);
        }

        WindowCoverage twoPanels = new WindowCoverage(0, true);
        twoPanels.add(-1, true);
        twoPanels.addOwnedUnclassified(
                -1,
                true,
                false,
                false,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Arrays.asList(panel, panel));
        reject(twoPanels);
        // Missing-focusable (8-arg constructor -> focusable NULL) isolates its own rejection.
        WindowCoverage missingFocusable = new WindowCoverage(0, true);
        missingFocusable.add(-1, true);
        missingFocusable.addOwnedUnclassified(
                -1,
                true,
                false,
                false,
                "com.qinggan.app.hiboard",
                frame,
                java.util.Collections.singletonList(
                        new WindowCoverage.Owner(
                                "bad",
                                0,
                                2106,
                                "com.qinggan.app.hiboard",
                                frame,
                                new WindowCoverage.State(true, true, true, null, null, null))));
        reject(missingFocusable);
    }

    private static void emptyTreeRecovery(WindowCoverage bars, WindowCoverage application)
            throws Exception {
        int[] repairs = {0}, reads = {0};
        String result =
                EmptyTreeRecovery.read(
                        () -> {
                            if (++reads[0] == 1) {
                                bars.verify();
                            }

                            application.verify();

                            return "real_application";
                        },
                        () -> repairs[0]++);

        if (!result.equals("real_application") || repairs[0] != 1 || reads[0] != 2) {
            throw new AssertionError();
        }

        repairs[0] = 0;

        try {
            EmptyTreeRecovery.read(
                    () -> {
                        bars.verify();

                        return "invalid";
                    },
                    () -> repairs[0]++);
            throw new AssertionError("persistent omission accepted");
        } catch (IllegalStateException expected) {
            if (!expected.getMessage().equals("application_window_unverified") || repairs[0] != 1) {
                throw new AssertionError();
            }
        }

        System.out.println(
                "system-only inventory rejected; one repair and persistent rejection passed;"
                        + " active focused dim ambiguity resolved only for API30 explicit owners");
    }
}
