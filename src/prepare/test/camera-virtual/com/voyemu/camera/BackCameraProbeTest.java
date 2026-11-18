package com.voyemu.camera;

import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.CamcorderProfile;

import androidx.camera.core.impl.CameraInfoInternal;
import androidx.camera.video.Quality;

import com.qinggan.camera.AppMain;
import com.qinggan.camera.common.CameraSetting;

import java.util.ArrayList;
import java.util.List;

public class BackCameraProbeTest {
    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static class Info implements CameraInfoInternal {
        final String id;

        Info(String x) {
            id = x;
        }

        public String getCameraId() {
            return id;
        }
    }

    static CameraCharacteristics camera(int facing, boolean jpeg) {
        var c = new CameraCharacteristics();

        c.facing = facing;
        c.map = new StreamConfigurationMap();
        c.map.jpeg = jpeg;

        return c;
    }

    public static void main(String[] a) {
        AppMain.context = new android.content.Context();
        CameraManager m = new CameraManager();
        AppMain.context.service = m;
        check(!BackCameraProbe.isSupported() && BackCameraProbe.defaultType(0) == 0);
        check(BackCameraProbe.localEntry(1) == 1);
        m.cameras.put("0", camera(0, true));
        m.cameras.put("1", camera(1, false));
        check(!BackCameraProbe.isSupported());
        CameraSetting.getInstance().type = 2;
        check(BackCameraProbe.selectId("0").equals("0"));
        List<Object> emptyInput = new ArrayList<>(), emptyOutput = new ArrayList<>();
        check(
                BackCameraProbe.augment(emptyInput, emptyOutput) == emptyOutput
                        && emptyOutput.isEmpty());
        m.cameras.get("1").map.jpeg = true;
        check(BackCameraProbe.isSupported() && BackCameraProbe.defaultType(0) == 2);
        check(BackCameraProbe.localEntry(1) == 0);

        for (int entry : new int[] {-1, 0, 2, 3, 4}) {
            check(BackCameraProbe.localEntry(entry) == entry);
        }

        CameraSetting.getInstance().type = 0;
        check(BackCameraProbe.selectId("0").equals("0"));
        CameraSetting.getInstance().type = 2;
        check(BackCameraProbe.selectId("0").equals("1"));
        Info front = new Info("0"), back = new Info("1");
        List<Object> input = List.of(front, back), original = new ArrayList<>(List.of(front));
        check(
                BackCameraProbe.augment(input, original) == original
                        && original.equals(List.of(front, back)));
        BackCameraProbe.augment(input, original);
        check(original.size() == 2);
        CamcorderProfile.fhd = false;
        CamcorderProfile.hd = true;
        check(BackCameraProbe.preferredQuality(Quality.FHD) == Quality.HD);
        CamcorderProfile.fhd = true;
        check(BackCameraProbe.preferredQuality(Quality.FHD) == Quality.FHD);
        CamcorderProfile.fhd = false;
        CamcorderProfile.hd = false;
        check(BackCameraProbe.preferredQuality(Quality.FHD) == Quality.FHD);
        System.out.println(
                "PASS BACK availability, scoped limiter, default, selector and supported quality");
    }
}
