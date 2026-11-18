package com.voyemu.api;

/** A WMS package name or shared UID alone never attests to a service window's process. */
final class OwnedWindowProof {
    static final class ProcessOwner {
        final int pid, uid;
        final String name;
        final String[] packages;

        ProcessOwner(int pid, int uid, String name, String[] packages) {
            this.pid = pid;
            this.uid = uid;
            this.name = name;
            this.packages = packages == null ? null : packages.clone();
        }
    }

    static boolean process(ViewServer.Window window, Iterable<ProcessOwner> processes) {
        if (window == null
                || window.sessionPid == null
                || window.sessionPid <= 0
                || window.sessionUid == null
                || window.ownerUid == null
                || !window.ownerUid.equals(window.sessionUid)
                || window.ownerPackage == null
                || window.ownerPackage.isEmpty()) {
            return false;
        }

        int matches = 0;

        for (ProcessOwner owner : processes) {
            if (owner.pid == window.sessionPid) {
                if (owner.uid != window.ownerUid
                        || !window.ownerPackage.equals(owner.name)
                        || owner.packages == null
                        || owner.packages.length != 1
                        || !window.ownerPackage.equals(owner.packages[0])) {
                    return false;
                }

                ++matches;
            }
        }

        return matches == 1;
    }

    static boolean eligible(ViewServer.Window window) {
        return Integer.valueOf(2010).equals(window.type)
                && Integer.valueOf(0).equals(window.display)
                && "com.qinggan.bluetoothphone".equals(window.ownerPackage)
                && Integer.valueOf(1000).equals(window.ownerUid)
                && Integer.valueOf(1000).equals(window.sessionUid)
                && window.sessionId != null
                && window.token != null
                && window.client != null
                && window.order != null
                && window.order >= 0
                && !window.transformed
                && Boolean.TRUE.equals(window.currentFocus)
                && displayed(window);
    }

    private static boolean displayed(ViewServer.Window window) {
        return Boolean.FALSE.equals(window.obscured)
                && Boolean.TRUE.equals(window.touchable)
                && Boolean.TRUE.equals(window.focusable)
                && Boolean.TRUE.equals(window.visible)
                && Boolean.TRUE.equals(window.hasSurface)
                && Boolean.TRUE.equals(window.drawn)
                && Boolean.TRUE.equals(window.viewVisible)
                && window.frame != null
                && window.frame.length == 4
                && window.frame[0] < window.frame[2]
                && window.frame[1] < window.frame[3];
    }
}
