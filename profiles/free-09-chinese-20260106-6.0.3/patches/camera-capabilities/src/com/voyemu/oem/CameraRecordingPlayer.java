package com.voyemu.oem;

import android.app.Activity;
import android.media.MediaDataSource;
import android.media.MediaPlayer;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.FrameLayout;
import android.widget.MediaController;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;

/** Read private recordings inside Camera, rather than asking mediaextractor to open their paths. */
final class CameraRecordingPlayer extends FrameLayout
        implements MediaController.MediaPlayerControl {
    private final MediaPlayer player = new MediaPlayer();
    private final MediaController controls;
    private MediaDataSource source;
    private boolean prepared, closed;

    CameraRecordingPlayer(Activity activity, File file) {
        super(activity);
        SurfaceView surface = new SurfaceView(activity);
        addView(
                surface,
                new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT));
        controls = new MediaController(activity);
        controls.setMediaPlayer(this);
        controls.setAnchorView(this);
        controls.setEnabled(false);
        player.setOnPreparedListener(ready -> {
            if (closed) {
                return;
            }

            prepared = true;
            controls.setEnabled(true);
            player.start();
            controls.show();
        });
        player.setOnCompletionListener(done -> {
            if (!closed) {
                controls.show(0);
            }
        });
        player.setOnErrorListener((failed, what, extra) -> {
            if (!closed) {
                Toast.makeText(activity, "Cannot play recording", Toast.LENGTH_LONG).show();
            }

            return true;
        });
        surface.setOnTouchListener((view, event) -> {
            if (prepared && !closed) {
                controls.show();
            }

            return true;
        });
        surface.getHolder()
                .addCallback(
                        new SurfaceHolder.Callback() {
                            @Override
                            public void surfaceCreated(SurfaceHolder holder) {
                                if (closed) {
                                    return;
                                }

                                try {
                                    source = new PrivateSource(file);
                                    player.setDataSource(source);
                                    player.setDisplay(holder);
                                    player.prepareAsync();
                                } catch (IOException error) {
                                    android.util.Log.e(
                                            "VoyemuCamera", "camera-recording-open-failed", error);
                                    Toast.makeText(
                                                    activity,
                                                    "Cannot play recording",
                                                    Toast.LENGTH_LONG)
                                            .show();
                                }
                            }

                            @Override
                            public void surfaceChanged(
                                    SurfaceHolder holder, int format, int width, int height) {}

                            @Override
                            public void surfaceDestroyed(SurfaceHolder holder) {
                                close();
                            }
                        });
    }

    void close() {
        if (closed) {
            return;
        }

        closed = true;
        controls.hide();
        player.release();

        if (source != null) {
            try {
                source.close();
            } catch (IOException ignored) {
                // Closing a released recording source has nothing left to report.
            }
        }
    }

    @Override
    public void start() {
        if (prepared && !closed) {
            player.start();
        }
    }

    @Override
    public void pause() {
        if (prepared && !closed) {
            player.pause();
        }
    }

    @Override
    public int getDuration() {
        return prepared && !closed ? player.getDuration() : 0;
    }

    @Override
    public int getCurrentPosition() {
        return prepared && !closed ? player.getCurrentPosition() : 0;
    }

    @Override
    public void seekTo(int position) {
        if (prepared && !closed) {
            player.seekTo(position);
        }
    }

    @Override
    public boolean isPlaying() {
        return prepared && !closed && player.isPlaying();
    }

    @Override
    public int getBufferPercentage() {
        return 100;
    }

    @Override
    public boolean canPause() {
        return true;
    }

    @Override
    public boolean canSeekBackward() {
        return true;
    }

    @Override
    public boolean canSeekForward() {
        return true;
    }

    @Override
    public int getAudioSessionId() {
        return closed ? 0 : player.getAudioSessionId();
    }

    private static final class PrivateSource extends MediaDataSource {
        private final RandomAccessFile bytes;

        PrivateSource(File file) throws IOException {
            bytes = new RandomAccessFile(file, "r");
        }

        @Override
        public synchronized int readAt(long position, byte[] buffer, int offset, int size)
                throws IOException {
            if (position < 0) {
                throw new IOException("negative-recording-position");
            }

            if (size == 0) {
                return 0;
            }

            if (position >= bytes.length()) {
                return -1;
            }

            bytes.seek(position);

            return bytes.read(buffer, offset, size);
        }

        @Override
        public synchronized long getSize() throws IOException {
            return bytes.length();
        }

        @Override
        public synchronized void close() throws IOException {
            bytes.close();
        }
    }
}
