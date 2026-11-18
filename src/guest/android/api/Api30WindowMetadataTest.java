package com.voyemu.api;

/** Real API30 WMS field layout; identity joins use WindowState hash, never title or UID. */
public final class Api30WindowMetadataTest {
    private static void scopeBoundaries() {
        java.util.Map<String, ViewServer.Window> windows = new java.util.LinkedHashMap<>();

        windows.put(
                "5dedd92", new ViewServer.Window("5dedd92", "com.android.systemui.ImageWallpaper"));
        windows.put("a339eae", new ViewServer.Window("a339eae", "GrantPermissionsActivity"));
        windows.put("abc", new ViewServer.Window("abc", "subwindow"));
        WindowMetadata.assignDisplays(
                windows,
                "  Window #3 Window{a339eae u0 GrantPermissionsActivity}:\n"
                        + "    mDisplayId=1\n"
                        + "    mOwnerUid=10124 package=com.android.permissioncontroller\n"
                        + "  Window #15 Window{5dedd92 u0 com.android.systemui.ImageWallpaper}:\n"
                        + "    mDisplayId=0\n"
                        + "    mOwnerUid=10121 package=com.android.systemui\n"
                        + "    mFrame=[0,0][1920,720]\n\n"
                        // Actual v7 WMS auxiliary listing repeats a different WindowState after the
                        // main list.
                        + "  Hiding System Alert Windows:\n"
                        + "  #0 Window{a339eae u0 GrantPermissionsActivity}:\n"
                        + "    mDisplayId=1\n"
                        + "    mOwnerUid=10124 package=com.android.permissioncontroller\n"
                        + "    mFrame=[32,0][896,720]\n");
        ViewServer.Window wallpaper = windows.get("5dedd92"),
                permission = windows.get("a339eae"),
                child = windows.get("abc");

        if (wallpaper.display != 0
                || wallpaper.ownerUid != 10121
                || !"com.android.systemui".equals(wallpaper.ownerPackage)
                || !java.util.Arrays.equals(wallpaper.frame, new int[] {0, 0, 1920, 720})
                || permission.display != 1
                || permission.ownerUid != 10124
                || permission.frame != null) {
            throw new AssertionError("auxiliary WMS section escaped window scope");
        }

        WindowMetadata.assignDisplays(
                windows,
                "  Window #15 Window{5dedd92 u0 com.android.systemui.ImageWallpaper}:\n"
                        + "    mDisplayId=0\n"
                        + "    Window #16 Window{abc u0 subwindow}:\n"
                        + "      mDisplayId=1\n"
                        + "      mOwnerUid=10125 package=owned.child\n"
                        + "  Unrelated section:\n"
                        + "    mDisplayId=9\n"
                        + "    mOwnerUid=999 package=foreign.owner\n"
                        + "  Window #17 Window{def u0 subwindow}:\n"
                        + "    mDisplayId=8\n"
                        + "    mOwnerUid=999 package=foreign.owner\n");

        if (wallpaper.display != 0
                || wallpaper.ownerUid != 10121
                || child.display != 1
                || child.ownerUid != 10125
                || !"owned.child".equals(child.ownerPackage)) {
            throw new AssertionError("subwindow identity or section boundary lost");
        }

        try {
            WindowMetadata.assignDisplays(
                    windows,
                    "  Window #16 Window{abc u0 subwindow}:\n"
                            + "    mDisplayId=1\n\n"
                            + "    mDisplayId=2\n");
            throw new AssertionError("conflicting display inside window block accepted");
        } catch (IllegalStateException expected) {
            if (!"wm_display_conflict".equals(expected.getMessage())) {
                throw expected;
            }
        }
    }

    public static void main(String[] args) {
        scopeBoundaries();
        passionShade();
        hiboardCurtain();
        api30Join();
    }

    private static void passionShade() {
        java.util.Map<String, ViewServer.Window> passion = new java.util.LinkedHashMap<>();

        passion.put("deee05d", new ViewServer.Window("deee05d", "ly-expandStatusBar"));
        WindowMetadata.assignDisplays(
                passion,
                "  Window #0 Window{deee05d u0 ly-expandStatusBar}:\n"
                        + "    mDisplayId=0\n"
                        + "    mOwnerUid=1000 showForAllUsers=true package=com.voyah.os.carlauncher"
                        + " appop=NONE\n"
                        + "    mAttrs={(0,0)(fillx720) ty=NAVIGATION_BAR_PANEL fmt=TRANSLUCENT\n"
                        + "      fl=NOT_FOCUSABLE NOT_TOUCH_MODAL LAYOUT_IN_SCREEN\n"
                        + "    mViewVisibility=0x0 mHaveFrame=true\n"
                        + "    mHasSurface=true isReadyForDisplay()=true\n"
                        + "    mFrame=[0,0][1920,720]\n"
                        + "      mDrawState=HAS_DRAWN mLastHidden=false\n"
                        + "    isVisible=true\n");
        ViewServer.Window shade = passion.get("deee05d");

        if (shade.type == null
                || shade.type != 2024
                || !Boolean.TRUE.equals(shade.drawn)
                || !Boolean.TRUE.equals(shade.viewVisible)
                || !Boolean.FALSE.equals(shade.focusable)) {
            throw new AssertionError("actual Passion symbolic type/drawn metadata lost");
        }

        String shadeIdentity = WindowMetadata.identities(passion);
        shade.drawn = false;

        if (shadeIdentity.equals(WindowMetadata.identities(passion))) {
            throw new AssertionError("draw-state transition escaped identity fence");
        }
    }

    private static void hiboardCurtain() {
        java.util.Map<String, ViewServer.Window> curtain = new java.util.LinkedHashMap<>();

        curtain.put("ed4c9c7", new ViewServer.Window("ed4c9c7", "HiboardBar"));
        curtain.put("3ca79eb", new ViewServer.Window("3ca79eb", "DimView"));
        WindowMetadata.assignDisplays(
                curtain,
                "  Window #8 Window{ed4c9c7 u0 HiboardBar}:\n"
                    + "    mDisplayId=0\n"
                    + "    mOwnerUid=1000 showForAllUsers=false package=com.qinggan.app.hiboard"
                    + " appop=NONE\n"
                    + "    mAttrs={(0,0)(fillx720) ty=2106 fmt=TRANSLUCENT\n"
                    + "      fl=LAYOUT_NO_LIMITS ALT_FOCUSABLE_IM WATCH_OUTSIDE_TOUCH"
                    + " HARDWARE_ACCELERATED\n"
                    + "    mHasSurface=true isReadyForDisplay()=true mWindowRemovalAllowed=false\n"
                    + "    mFrame=[0,0][1920,720] last=[0,0][1920,720]\n"
                    + "    isVisible=true\n"
                    + "  Window #9 Window{3ca79eb u0 DimView}:\n"
                    + "    mDisplayId=0\n"
                    + "    mOwnerUid=1000 showForAllUsers=false package=com.qinggan.app.hiboard"
                    + " appop=NONE\n"
                    + "    mAttrs={(0,0)(fillxfill) ty=2106 fmt=TRANSLUCENT\n"
                    + "      fl=NOT_FOCUSABLE NOT_TOUCHABLE IGNORE_CHEEK_PRESSES"
                    + " HARDWARE_ACCELERATED\n"
                    + "    mHasSurface=true isReadyForDisplay()=true mWindowRemovalAllowed=false\n"
                    + "    mFrame=[0,0][1920,720] last=[0,0][1920,720]\n"
                    + "    isVisible=true\n");
        ViewServer.Window panel = curtain.get("ed4c9c7"), dim = curtain.get("3ca79eb");

        if (!Boolean.TRUE.equals(panel.touchable)
                || !Boolean.FALSE.equals(dim.touchable)
                || !Boolean.TRUE.equals(panel.visible)
                || !Boolean.TRUE.equals(panel.hasSurface)
                || !Boolean.TRUE.equals(panel.focusable)
                || !Boolean.FALSE.equals(dim.focusable)
                || !java.util.Arrays.equals(panel.frame, new int[] {0, 0, 1920, 720})) {
            throw new AssertionError("actual unfocused OEM curtain metadata lost");
        }

        String identity = WindowMetadata.identities(curtain);
        dim.visible = false;

        if (identity.equals(WindowMetadata.identities(curtain))) {
            throw new AssertionError("visibility change omitted from fence");
        }

        dim.focusable = null;
        String provisional = WindowMetadata.identities(curtain);
        dim.focusable = false;

        if (provisional.equals(WindowMetadata.identities(curtain))) {
            throw new AssertionError("focusability change omitted from fence");
        }
    }

    private static void api30Join() {
        java.util.Map<String, ViewServer.Window> windows = new java.util.LinkedHashMap<>();

        windows.put("622e884", new ViewServer.Window("622e884", "pip-dismiss-overlay"));
        windows.put("f00092c", new ViewServer.Window("f00092c", "NotificationShade"));
        WindowMetadata.assignDisplays(
                windows,
                "  Window #0 Window{622e884 u0 pip-dismiss-overlay}:\n"
                        + "    mDisplayId=0 rootTaskId=1 mSession=Session{912e32 694:u0a10121}\n"
                        + "    mOwnerUid=10121 showForAllUsers=true package=com.android.systemui"
                        + " appop=NONE\n"
                        + "    mAttrs={(0,470)(fillx250) ty=2024 fmt=TRANSLUCENT\n"
                        + "    mFrame=[0,470][1920,720] last=[0,0][0,0]\n"
                        + "  Window #1 Window{f00092c u0 NotificationShade}:\n"
                        + "    mDisplayId=1 rootTaskId=1\n"
                        + "    mOwnerUid=10121 showForAllUsers=true package=com.android.systemui"
                        + " appop=NONE\n");
        ViewServer.Window first = windows.get("622e884"), second = windows.get("f00092c");

        if (first.display != 0
                || first.ownerUid != 10121
                || !"com.android.systemui".equals(first.ownerPackage)
                || first.type != 2024
                || !java.util.Arrays.equals(first.frame, new int[] {0, 470, 1920, 720})
                || second.display != 1
                || second.frame != null
                || second.type != null) {
            throw new AssertionError("API30 metadata join");
        }

        String before = WindowMetadata.identities(windows);
        WindowMetadata.assignDisplays(
                windows,
                "  Window #2 Window{abcd u0 pip-dismiss-overlay}:\n"
                        + "    mDisplayId=7\n"
                        + "    mOwnerUid=10121 package=foreign.pkg\n");

        if (!before.equals(WindowMetadata.identities(windows))) {
            throw new AssertionError("title or shared UID inferred owner");
        }

        try {
            WindowMetadata.assignDisplays(
                    windows,
                    "  Window #0 Window{622e884 u0 pip-dismiss-overlay}:\n    mDisplayId=2\n");
            throw new AssertionError("conflicting display accepted");
        } catch (IllegalStateException expected) {
            if (!"wm_display_conflict".equals(expected.getMessage())) {
                throw expected;
            }
        }

        System.out.println("API30 WMS exact identity metadata and negative joins passed");
    }
}
