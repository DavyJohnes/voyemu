package com.voyemu.api;

public final class OwnedWindowProofTest {
    static ViewServer.Window actual() {
        ViewServer.Window window = new ViewServer.Window("fe0e6c1", "");
        java.util.Map<String, ViewServer.Window> windows = new java.util.LinkedHashMap<>();

        windows.put(window.hash, window);
        WindowMetadata.assignDisplays(
                windows,
                "  Window #0 Window{fe0e6c1 u0 com.qinggan.bluetoothphone}:\n"
                    + "    mDisplayId=0 stackId=0 mSession=Session{e13a185 2449:1000}"
                    + " mClient=android.os.BinderProxy@d8f87cb\n"
                    + "    mOwnerUid=1000 mShowToOwnerOnly=true package=com.qinggan.bluetoothphone"
                    + " appop=SYSTEM_ALERT_WINDOW\n"
                    + "    mAttrs={(0,0)(fillxfill) ty=SYSTEM_ERROR\n"
                    + "      fl=HARDWARE_ACCELERATED}\n"
                    + "    mToken=WindowToken{37edba8 android.os.BinderProxy@d8f87cb}\n"
                    + "    mViewVisibility=0x0 mHaveFrame=true mObscured=false\n"
                    + "    mHasSurface=true isReadyForDisplay()=true\n"
                    + "    mFrame=[0,0][1920,720]\n"
                    + "      mDrawState=HAS_DRAWN mLastHidden=false\n"
                    + "    isVisible=true\n"
                    + "  mCurrentFocus=Window{fe0e6c1 u0 com.qinggan.bluetoothphone}\n");

        return window;
    }

    private static void denials() {
        for (int denial = 0; denial < 12; ++denial) {
            ViewServer.Window candidate = actual();

            switch (denial) {
                case 0:
                    candidate.token = null;
                    break;
                case 1:
                    candidate.client = null;
                    break;
                case 2:
                    candidate.sessionId = null;
                    break;
                case 3:
                    candidate.currentFocus = false;
                    break;
                case 4:
                    candidate.obscured = true;
                    break;
                case 5:
                    candidate.display = 1;
                    break;
                case 6:
                    candidate.type = 2106;
                    break;
                case 7:
                    candidate.touchable = null;
                    break;
                case 8:
                    candidate.visible = false;
                    break;
                case 9:
                    candidate.ownerPackage = "foreign.package";
                    break;
                case 10:
                    candidate.ownerUid = 1001;
                    break;
                case 11:
                    candidate.transformed = true;
                    break;
                default:
                    throw new AssertionError("unknown denial " + denial);
            }

            if (OwnedWindowProof.eligible(candidate)) {
                throw new AssertionError("unverified window accepted: " + denial);
            }
        }
    }

    public static void main(String[] args) {
        ViewServer.Window window = actual();
        OwnedWindowProof.ProcessOwner owner =
                new OwnedWindowProof.ProcessOwner(
                        2449,
                        1000,
                        "com.qinggan.bluetoothphone",
                        new String[] {"com.qinggan.bluetoothphone"});

        if (!OwnedWindowProof.eligible(window)
                || !OwnedWindowProof.process(window, java.util.Arrays.asList(owner))) {
            throw new AssertionError("captured WMS/process owner not recognized");
        }

        for (OwnedWindowProof.ProcessOwner invalid :
                new OwnedWindowProof.ProcessOwner[] {
                    new OwnedWindowProof.ProcessOwner(2450, 1000, owner.name, owner.packages),
                    new OwnedWindowProof.ProcessOwner(2449, 1001, owner.name, owner.packages),
                    new OwnedWindowProof.ProcessOwner(
                            2449, 1000, "foreign.process", owner.packages),
                    new OwnedWindowProof.ProcessOwner(
                            2449, 1000, owner.name, new String[] {"foreign.package"}),
                    new OwnedWindowProof.ProcessOwner(
                            2449,
                            1000,
                            owner.name,
                            new String[] {owner.name, "foreign.shared.uid"}),
                    new OwnedWindowProof.ProcessOwner(2449, 1000, owner.name, null)
                }) {
            if (OwnedWindowProof.process(window, java.util.Arrays.asList(invalid))) {
                throw new AssertionError("foreign process accepted");
            }
        }

        if (OwnedWindowProof.process(window, java.util.Arrays.asList(owner, owner))
                || OwnedWindowProof.process(
                        window, java.util.Collections.<OwnedWindowProof.ProcessOwner>emptyList())) {
            throw new AssertionError("ambiguous or unavailable process accepted");
        }

        denials();
        java.util.Map<String, ViewServer.Window> windows = new java.util.LinkedHashMap<>();
        windows.put(window.hash, window);
        String before = WindowMetadata.identities(windows);
        window.token = "reused";

        if (before.equals(WindowMetadata.identities(windows))) {
            throw new AssertionError("token reuse escaped identity fence");
        }

        System.out.println("Exact WMS owner, process and identity rejection tests passed");
    }
}
