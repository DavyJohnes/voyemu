package com.voyemu.passion.dvr;

import android.content.Context;
import android.graphics.ImageFormat;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import android.util.Size;
import android.view.Surface;

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** The virtual BACK camera stands in for the DVR unit stream of the OEM preview client. */
public final class PassionDvrCamera {
    private static final String TAG = "PassionDvrCamera";
    private static final long TIMEOUT_MS = 3000;
    // CamcorderManager.STATUS_CAM_NO_STREAM and STATUS_CAM_OK of the OEM client.
    private static final int NO_STREAM = 0, STREAM = 1;
    private static Handler handler;
    private static volatile CameraDevice device;
    private static volatile CameraCaptureSession session;
    private static volatile ImageReader still;

    private PassionDvrCamera() {}

    /** CamcorderNative.startPreview(Object): shows the camera picture on the preview surface. */
    public static synchronized boolean startPreview(Object client, Object target) {
        close();

        if (!(target instanceof Surface) || !((Surface) target).isValid()) {
            Log.w(TAG, "preview surface unavailable");

            return false;
        }

        Surface surface = (Surface) target;

        try {
            CameraManager manager = application().getSystemService(CameraManager.class);
            String id = backCamera(manager);

            if (id == null) {
                Log.w(TAG, "no BACK camera with JPEG output");
                status(client, NO_STREAM);

                return false;
            }

            if (handler == null) {
                HandlerThread thread = new HandlerThread(TAG);
                thread.start();
                handler = new Handler(thread.getLooper());
            }

            Size size = largest(manager, id);
            still = ImageReader.newInstance(size.getWidth(), size.getHeight(), ImageFormat.JPEG, 2);
            device = open(manager, id, client);
            session = configure(device, Arrays.asList(surface, still.getSurface()));
            CaptureRequest.Builder request =
                    device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            request.addTarget(surface);
            session.setRepeatingRequest(request.build(), null, handler);
            Log.i(TAG, "preview started on camera " + id);
            status(client, STREAM);

            return true;
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: a camera or reflection failure leaves the preview off,
            // as a DVR unit without a stream does
            Log.e(TAG, "preview unavailable", e);
            close();
            status(client, NO_STREAM);

            return false;
        }
    }

    /** CamcorderNative.stopPreview(). */
    public static synchronized boolean stopPreview(Object client) {
        close();

        return true;
    }

    /** CamcorderNative.takePicture(): one JPEG still of the camera, or null without preview. */
    public static synchronized byte[] takePicture(Object client) {
        CameraCaptureSession current = session;
        ImageReader reader = still;

        if (current == null || reader == null) {
            return null;
        }

        try {
            AtomicReference<byte[]> jpeg = new AtomicReference<>();
            CountDownLatch done = new CountDownLatch(1);
            reader.setOnImageAvailableListener(
                    r -> {
                        try (Image image = r.acquireNextImage()) {
                            ByteBuffer buffer = image.getPlanes()[0].getBuffer();
                            byte[] bytes = new byte[buffer.remaining()];
                            buffer.get(bytes);
                            jpeg.set(bytes);
                        }

                        done.countDown();
                    },
                    handler);
            CaptureRequest.Builder request =
                    current.getDevice().createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE);
            request.addTarget(reader.getSurface());
            current.capture(request.build(), null, handler);

            if (!done.await(TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
                Log.w(TAG, "still capture timed out");
            }

            return jpeg.get();
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: a failed still capture reports no picture
            Log.e(TAG, "still capture unavailable", e);

            return null;
        }
    }

    private static Context application() throws ReflectiveOperationException {
        return (Context)
                Class.forName("android.app.ActivityThread")
                        .getMethod("currentApplication")
                        .invoke(null);
    }

    private static String backCamera(CameraManager manager) throws CameraAccessException {
        for (String id : manager.getCameraIdList()) {
            CameraCharacteristics c = manager.getCameraCharacteristics(id);
            Integer facing = c.get(CameraCharacteristics.LENS_FACING);
            StreamConfigurationMap map =
                    c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

            if (facing != null
                    && facing == CameraCharacteristics.LENS_FACING_BACK
                    && map != null
                    && map.getOutputSizes(ImageFormat.JPEG) != null
                    && map.getOutputSizes(ImageFormat.JPEG).length > 0) {
                return id;
            }
        }

        return null;
    }

    private static Size largest(CameraManager manager, String id) throws CameraAccessException {
        Size best = null;

        for (Size size :
                manager.getCameraCharacteristics(id)
                        .get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                        .getOutputSizes(ImageFormat.JPEG)) {
            if (best == null
                    || (long) size.getWidth() * size.getHeight()
                            > (long) best.getWidth() * best.getHeight()) {
                best = size;
            }
        }

        return best;
    }

    private static CameraDevice open(CameraManager manager, String id, Object client)
            throws CameraAccessException, InterruptedException {
        AtomicReference<CameraDevice> opened = new AtomicReference<>();
        CountDownLatch done = new CountDownLatch(1);

        manager.openCamera(
                id,
                new CameraDevice.StateCallback() {
                    @Override
                    public void onOpened(CameraDevice camera) {
                        opened.set(camera);
                        done.countDown();
                    }

                    @Override
                    public void onDisconnected(CameraDevice camera) {
                        lost(camera, client);
                        done.countDown();
                    }

                    @Override
                    public void onError(CameraDevice camera, int error) {
                        Log.e(TAG, "camera error " + error);
                        lost(camera, client);
                        done.countDown();
                    }
                },
                handler);

        if (!done.await(TIMEOUT_MS, TimeUnit.MILLISECONDS) || opened.get() == null) {
            throw new IllegalStateException("camera " + id + " did not open");
        }

        return opened.get();
    }

    private static CameraCaptureSession configure(CameraDevice camera, List<Surface> outputs)
            throws CameraAccessException, InterruptedException {
        AtomicReference<CameraCaptureSession> configured = new AtomicReference<>();
        CountDownLatch done = new CountDownLatch(1);

        camera.createCaptureSession(
                outputs,
                new CameraCaptureSession.StateCallback() {
                    @Override
                    public void onConfigured(CameraCaptureSession s) {
                        configured.set(s);
                        done.countDown();
                    }

                    @Override
                    public void onConfigureFailed(CameraCaptureSession s) {
                        done.countDown();
                    }
                },
                handler);

        if (!done.await(TIMEOUT_MS, TimeUnit.MILLISECONDS) || configured.get() == null) {
            throw new IllegalStateException("preview session not configured");
        }

        return configured.get();
    }

    // Camera callbacks run on the camera thread and never take the lock of the client calls.
    private static void lost(CameraDevice camera, Object client) {
        camera.close();

        if (camera.equals(device)) {
            device = null;
            session = null;
            status(client, NO_STREAM);
        }
    }

    private static void close() {
        CameraCaptureSession s = session;
        CameraDevice d = device;
        ImageReader r = still;

        session = null;
        device = null;
        still = null;

        if (s != null) {
            s.close();
        }

        if (d != null) {
            d.close();
        }

        if (r != null) {
            r.close();
        }
    }

    // CamcorderNative.onPreStatusChange(int) carries the stream state of the DVR unit.
    private static void status(Object client, int state) {
        try {
            client.getClass().getMethod("onPreStatusChange", int.class).invoke(client, state);
        } catch (ReflectiveOperationException e) {
            Log.e(TAG, "stream state not delivered", e);
        }
    }
}
