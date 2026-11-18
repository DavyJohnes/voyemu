package com.qinggan.media;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Bundle;
import android.os.IBinder;
import android.service.media.MediaBrowserService;

import com.qinggan.audiopolicy.IAudioPolicyService;

import java.util.ArrayList;
import java.util.List;

/**
 * Base stub MediaBrowserService emulating the missing OEM com.qinggan.media app. Uses only
 * framework classes (no androidx) — fully compatible with MediaBrowserCompat clients such as the
 * OEM Launcher.
 */
public abstract class StubMediaService extends MediaBrowserService {

    private static final String TAG = "QGMediaStub";

    private final String mTag; // MediaSession tag == service class name
    private final String mFakeTitle; // metadata title, e.g. "Emulator BT Music"
    private final int mStreamType; // 3 = USB/media, 4 = BT source
    private final String mClientId; // e.g. "com.qinggan.media.usbmusic"
    private MediaSession mSession;
    private IAudioPolicyService mAudioPolicy;

    protected StubMediaService(String tag, String fakeTitle, int streamType, String clientId) {
        mTag = tag;
        mFakeTitle = fakeTitle;
        mStreamType = streamType;
        mClientId = clientId;
    }

    private final ServiceConnection mApConn =
            new ServiceConnection() {
                @Override
                public void onServiceConnected(ComponentName name, IBinder service) {
                    mAudioPolicy = IAudioPolicyService.Stub.asInterface(service);
                    requestAudioPolicy();
                }

                @Override
                public void onServiceDisconnected(ComponentName name) {
                    mAudioPolicy = null;
                }
            };

    /**
     * Emulate the OEM media app requesting audio focus through qg.audio_policy so the launcher
     * music widget switches the active source to this service (streamType 3=USB, 4=BT).
     */
    private void requestAudioPolicy() {
        IAudioPolicyService ap = mAudioPolicy;

        if (ap == null) {
            return;
        }

        try {
            int r =
                    ap.requestAudioPolicyEx(
                            mStreamType, -1, null, null, mClientId, getPackageName(), 0, 0);
            log(
                    "requestAudioPolicyEx streamType="
                            + mStreamType
                            + " clientId="
                            + mClientId
                            + " -> "
                            + r);
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: An OEM Binder request failure is logged without
            // killing the media session.
            log("requestAudioPolicyEx failed: " + e);
        }
    }

    private void ensureAudioPolicy() {
        if (mAudioPolicy != null) {
            requestAudioPolicy();

            return;
        }

        try {
            Intent intent = new Intent("com.qinggan.audiopolicy.action.AUDIO_POLICY_SERVICE");
            intent.setPackage("com.qinggan.audiopolicy.service");
            bindService(intent, mApConn, Context.BIND_AUTO_CREATE);
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: A failed Android service bind is logged so later
            // requests can retry.
            log("bind audio policy failed: " + e);
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mSession = new MediaSession(this, mTag);
        mSession.setCallback(
                new MediaSession.Callback() {
                    @Override
                    public void onPlay() {
                        log("onPlay");
                        setPlaybackState(PlaybackState.STATE_PLAYING);
                        ensureAudioPolicy();
                    }

                    @Override
                    public void onPause() {
                        log("onPause");
                        setPlaybackState(PlaybackState.STATE_PAUSED);
                    }

                    @Override
                    public void onSkipToNext() {
                        log("onSkipToNext");
                    }

                    @Override
                    public void onSkipToPrevious() {
                        log("onSkipToPrevious");
                    }

                    @Override
                    public void onPlayFromMediaId(String mediaId, Bundle extras) {
                        log("onPlayFromMediaId mediaId=" + mediaId + " extras=" + extras);
                        setPlaybackState(PlaybackState.STATE_PLAYING);
                        ensureAudioPolicy();
                    }

                    @Override
                    public void onCustomAction(String action, Bundle extras) {
                        log("onCustomAction action=" + action + " extras=" + extras);

                        // The widget sends usb_play / change_usb_host(autoplay) when the
                        // user picks this source in the dropdown; the OEM service would
                        // start playback (and thus grab audio policy) in response.
                        if (action != null
                                && (action.endsWith(".usb_play")
                                        || action.contains("change_usb_host"))) {
                            setPlaybackState(PlaybackState.STATE_PLAYING);
                            ensureAudioPolicy();
                        }
                    }
                });
        setPlaybackState(PlaybackState.STATE_NONE);
        mSession.setActive(true);
        setSessionToken(mSession.getSessionToken());
        log("onCreate session=" + mTag);
    }

    @Override
    public void onDestroy() {
        // The bind to the audio policy service and the session outlive the
        // stub otherwise, keeping the bound process alive after destruction.
        try {
            unbindService(mApConn);
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: A failed Android unbind must not prevent media session
            // teardown.
            log("unbind audio policy failed: " + e);
        }

        if (mSession != null) {
            mSession.release();
            mSession = null;
        }

        super.onDestroy();
    }

    private void setPlaybackState(int state) {
        mSession.setPlaybackState(
                new PlaybackState.Builder()
                        .setActions(
                                PlaybackState.ACTION_PLAY
                                        | PlaybackState.ACTION_PAUSE
                                        | PlaybackState.ACTION_PLAY_PAUSE
                                        | PlaybackState.ACTION_SKIP_TO_NEXT
                                        | PlaybackState.ACTION_SKIP_TO_PREVIOUS
                                        | PlaybackState.ACTION_PLAY_FROM_MEDIA_ID)
                        .setState(state, 0, 1.0f)
                        .build());
        mSession.setMetadata(
                new MediaMetadata.Builder()
                        .putString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE, mFakeTitle)
                        .putString(MediaMetadata.METADATA_KEY_TITLE, mFakeTitle)
                        .putString(MediaMetadata.METADATA_KEY_ARTIST, "Voyemu emulator stub")
                        .putLong(MediaMetadata.METADATA_KEY_DURATION, 180000)
                        .build());
    }

    @Override
    public BrowserRoot onGetRoot(String clientPackageName, int clientUid, Bundle rootHints) {
        log("onGetRoot client=" + clientPackageName + " uid=" + clientUid + " hints=" + rootHints);

        return new BrowserRoot("root", null);
    }

    @Override
    public void onLoadChildren(
            String parentId, Result<List<android.media.browse.MediaBrowser.MediaItem>> result) {
        log("onLoadChildren parent=" + parentId);
        result.sendResult(new ArrayList<>());
    }

    private void log(String msg) {
        android.util.Log.i(TAG, mTag + ": " + msg);
    }
}
