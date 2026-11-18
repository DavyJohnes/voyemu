package com.voyemu.launcher;

import android.content.Context;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;

import com.qinggan.launcher.base.BaseApplication;

public class DvrAvailabilityTest {
    static void check(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        check(DvrAvailability.select(1) == 1);
        check(DvrAvailability.select(-1) == -1);
        Context c = new Context();
        BaseApplication.context = c;
        CameraManager m = new CameraManager();
        c.service = m;
        CameraCharacteristics camera = new CameraCharacteristics();
        camera.facing = CameraCharacteristics.LENS_FACING_BACK;
        camera.map = new StreamConfigurationMap();
        camera.map.jpeg = true;
        m.cameras.put("1", camera);
        check(DvrAvailability.select(-1) == -1);
        c.packages.installed = true;
        camera.facing = 0;
        check(DvrAvailability.select(-1) == -1);
        camera.facing = 1;
        camera.map.jpeg = false;
        check(DvrAvailability.select(-1) == -1);
        camera.map.jpeg = true;
        check(DvrAvailability.select(-1) == 1);
        check(DvrAvailability.useService(null));
        check(!DvrAvailability.useService(new Object()));
        c.service = null;
        check(DvrAvailability.select(0) == 0);
        check(!DvrAvailability.useService(null));
        System.out.println(
                "PASS installed Camera and real BACK JPEG required; OEM enabled and negative cases"
                        + " retained");
    }
}
