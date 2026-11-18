package com.voyemu.oem;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.BitmapFactory;
import android.widget.ImageView;
import android.widget.Toast;

import java.io.File;

/** Browser for the virtual camera's own recordings; no external Gallery dependency. */
public final class CameraAlbum {
    private CameraAlbum() {}

    public static void open(Activity activity) {
        try {
            File[] files = CameraStoragePolicy.completedMedia(activity.getFilesDir());
            String[] names = new String[files.length];

            for (int i = 0; i < files.length; i++) {
                names[i] = files[i].getName();
            }

            AlertDialog.Builder dialog =
                    new AlertDialog.Builder(activity)
                            .setTitle("Camera recordings")
                            .setNegativeButton(android.R.string.cancel, null);

            if (files.length == 0) {
                dialog.setMessage("No recordings yet");
            } else {
                dialog.setItems(names, (parent, index) -> preview(activity, files[index]));
            }

            dialog.show();
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: The OEM album entry point reports Android or
            // private-storage failure to the user.
            Toast.makeText(activity, "Cannot open camera recordings", Toast.LENGTH_LONG).show();
            android.util.Log.e("VoyemuCamera", "camera-album-unavailable", error);
        }
    }

    private static void preview(Activity activity, File file) {
        final CameraRecordingPlayer video;
        AlertDialog.Builder builder = new AlertDialog.Builder(activity).setTitle(file.getName());

        if (file.getName().endsWith(".mp4")) {
            video = new CameraRecordingPlayer(activity, file);
            builder.setView(video);
        } else {
            video = null;
            ImageView image = new ImageView(activity);
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inSampleSize = 2;
            image.setImageBitmap(BitmapFactory.decodeFile(file.getAbsolutePath(), options));
            image.setAdjustViewBounds(true);
            builder.setView(image);
        }

        AlertDialog dialog =
                builder.setNegativeButton("Close", null)
                        .setPositiveButton(
                                "Delete",
                                (parent, which) ->
                                        new AlertDialog.Builder(activity)
                                                .setTitle("Delete recording?")
                                                .setMessage(file.getName())
                                                .setNegativeButton(android.R.string.cancel, null)
                                                .setPositiveButton(
                                                        "Delete",
                                                        (confirmation, button) -> {
                                                            try {
                                                                if (!CameraStoragePolicy
                                                                        .deleteCompletedMedia(
                                                                                activity
                                                                                        .getFilesDir(),
                                                                                file)) {
                                                                    throw new java.io.IOException(
                                                                            "camera-recording-delete-failed");
                                                                }

                                                                open(activity);
                                                            } catch (Exception error) {
                                                                // checkstyle-allow IllegalCatch: UI
                                                                // The delete callback reports
                                                                // private-storage or Android UI
                                                                // failure to the user.
                                                                Toast.makeText(
                                                                                activity,
                                                                                "Cannot delete"
                                                                                    + " recording",
                                                                                Toast.LENGTH_LONG)
                                                                        .show();
                                                            }
                                                        })
                                                .show())
                        .create();
        dialog.setOnDismissListener(ignored -> {
            if (video != null) {
                video.close();
            }
        });
        dialog.show();
        dialog.getWindow().setLayout(1100, 650);
    }
}
