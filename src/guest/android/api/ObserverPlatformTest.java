package com.voyemu.api;

public final class ObserverPlatformTest {
    public static void main(String[] args) {
        if (!ObserverPlatform.allowed(28, "")
                || !ObserverPlatform.allowed(30, ObserverPlatform.QINGGAN11_611)
                || !ObserverPlatform.allowed(30, ObserverPlatform.QINGGAN11_731)
                || !ObserverPlatform.allowed(30, ObserverPlatform.PASSION)) {
            throw new AssertionError();
        }

        for (int api : new int[] {27, 28, 29, 30, 31}) {
            if (api != 28 && ObserverPlatform.allowed(api, "")) {
                throw new AssertionError("unscoped API accepted");
            }

            if (api != 30
                    && (ObserverPlatform.allowed(api, ObserverPlatform.QINGGAN11_611)
                            || ObserverPlatform.allowed(api, ObserverPlatform.QINGGAN11_731)
                            || ObserverPlatform.allowed(api, ObserverPlatform.PASSION))) {
                throw new AssertionError("profile API mismatch accepted");
            }
        }

        for (String profile :
                new String[] {
                    "",
                    ObserverPlatform.QINGGAN11_611 + "x",
                    ObserverPlatform.PASSION + "x",
                    ObserverPlatform.QINGGAN11_731 + "x"
                }) {
            if (ObserverPlatform.allowed(30, profile)) {
                throw new AssertionError("foreign API30 profile accepted");
            }
        }

        if (!"api30_default_display".equals(ObserverPlatform.defaultScope(30))
                || !"stable_api30_windowstate_owner_type_frame"
                        .equals(
                                ObserverPlatform.provenance(
                                        30, "stable_", "windowstate_owner_type_frame"))
                || !"api28_viewserver_dump_wm_text_v1"
                        .equals(
                                ObserverPlatform.provenance(
                                        28, "", "viewserver_dump_wm_text_v1"))) {
            throw new AssertionError("false provenance");
        }

        System.out.println("Exact API30 profile admission and versioned provenance passed");
    }
}
