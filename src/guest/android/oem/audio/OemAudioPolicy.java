package com.voyemu.oem.audio;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.WeakHashMap;

/** Private prototype: policy ownership is scoped to the original Binder Stub. */
public final class OemAudioPolicy {
    private static final IdentityHashMap<IBinder, Session> SESSIONS = new IdentityHashMap<>();
    private static final Map<IBinder, Config> CONFIGS = new WeakHashMap<>();
    private static final IdentityHashMap<Session, WeakReference<IBinder>> RETIRED =
            new IdentityHashMap<>();

    private OemAudioPolicy() {}

    public static void start(Context context, IBinder stub) {
        startForTest(
                stub,
                new AndroidPlatform((AudioManager) context.getSystemService(Context.AUDIO_SERVICE)),
                new SharedPreferencesConfig(context));
    }

    static void startForTest(IBinder stub, Platform platform, Config config) {
        retryRetired(stub);
        Session old;

        synchronized (SESSIONS) {
            CONFIGS.put(stub, config);
            old = SESSIONS.put(stub, new Session(platform, config));
        }

        if (old != null) {
            old.stop();
            retainPending(stub, old);
        }
    }

    public static void stop(IBinder stub) {
        retryRetired(stub);
        Session old;

        synchronized (SESSIONS) {
            old = SESSIONS.remove(stub);
        }

        if (old != null) {
            old.stop();
            retainPending(stub, old);
        }
    }

    private static void retainPending(IBinder stub, Session session) {
        synchronized (SESSIONS) {
            if (session.hasPending()) {
                RETIRED.put(session, new WeakReference<>(stub));
            }
        }
    }

    private static void pruneRetired(Session session) {
        synchronized (SESSIONS) {
            if (!session.hasPending()) {
                RETIRED.remove(session);
            }
        }
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    private static void retryRetired(IBinder stub) {
        List<Session> copy = new ArrayList<>();

        synchronized (SESSIONS) {
            for (Map.Entry<Session, WeakReference<IBinder>> item : RETIRED.entrySet()) {
                IBinder owner = item.getValue().get();

                if (owner == stub || owner == null) {
                    copy.add(item.getKey());
                }
            }
        }

        for (Session session : copy) {
            session.retryPending();
        }
    }

    public static Object dispatch(IBinder stub, String signature, Object[] args)
            throws RemoteException {
        if (OemAudioPolicyUnavailable.matches(signature)) {
            return OemAudioPolicyUnavailable.dispatch(signature);
        }

        Session session;
        Config config;

        synchronized (SESSIONS) {
            session = SESSIONS.get(stub);
            config = CONFIGS.get(stub);
        }

        if (session == null) {
            return stopped(signature, config);
        }

        return session.dispatch(signature, args);
    }

    private static Object stopped(String signature, Config config) {
        if (!exact(signature)) {
            throw unsupported("exact signature required: " + signature);
        }

        String name = signature.substring(0, signature.indexOf('('));

        switch (name) {
            case "isNaviMix":
                if (config == null) {
                    throw unsupported("persisted navi_mix configuration unavailable");
                }

                return config.read();
            case "requestAudioPolicy":
            case "requestAudioPolicyEx":
            case "requestAudioFocus":
            case "abandonAudioPolicy":
            case "abandonAudioFocus":
                return 0;
            case "getStreamVolume":
            case "getStreamMaxVolume":
            case "setMicMute":
                return -1;
            case "isMasterMute":
            case "isMediaPlaying":
            case "isMicMute":
            case "isStreamMute":
            case "registerAudioClientCallback":
            case "registerAudioPolicyInfoCallback":
            case "unregisterAudioClientCallback":
            case "unregisterAudioPolicyInfoCallback":
                return false;
            case "getAudioPolicyInfo":
            case "getCurrentAudioPolicyInfo":
            case "getA2dpAudioPolicyInfo":
            case "getCurrAudioPolicyInfoByChannel":
            case "getCurrMediaAudioPolicyInfoByChannel":
            case "getLastMediaAudioPolicyInfoByChannel":
            case "getAudioClients":
                return null;
            default:
                throw unsupported("service stopped: " + signature);
        }
    }

    interface Config {
        boolean read();

        boolean commit(boolean value);
    }

    private static final class SharedPreferencesConfig implements Config {
        final SharedPreferences preferences;

        SharedPreferencesConfig(Context context) {
            preferences =
                    context.getSharedPreferences("qg_audiopolicy_settings", Context.MODE_PRIVATE);
        }

        @Override
        public synchronized boolean read() {
            return preferences.getBoolean("navi_mix", true);
        }

        @Override
        public synchronized boolean commit(boolean value) {
            boolean before = read();

            if (preferences.edit().putBoolean("navi_mix", value).commit() && read() == value) {
                return true;
            }

            // Android commit may update its in-memory view even when disk persistence fails.
            preferences.edit().putBoolean("navi_mix", before).commit();

            return false;
        }
    }

    interface Change {
        void changed(int change);
    }

    interface Platform {
        Object create(int usage, int content, int gain, int flags, Change listener);

        int request(Object request);

        int abandon(Object request);

        Object volume(String operation, int stream, int value, int flags);

        default boolean captureAvailable() {
            return false;
        }
    }

    private static final class AndroidPlatform implements Platform {
        final AudioManager manager;

        AndroidPlatform(AudioManager manager) {
            if (manager == null) {
                throw new IllegalStateException("AudioManager unavailable");
            }

            this.manager = manager;
        }

        @Override
        public Object create(int usage, int content, int gain, int flags, final Change listener) {
            AudioAttributes attributes =
                    new AudioAttributes.Builder().setUsage(usage).setContentType(content).build();

            return new AudioFocusRequest.Builder(gain)
                    .setAudioAttributes(attributes)
                    .setAcceptsDelayedFocusGain((flags & 1) != 0)
                    .setWillPauseWhenDucked(true)
                    .setOnAudioFocusChangeListener(
                            new AudioManager.OnAudioFocusChangeListener() {
                                @Override
                                public void onAudioFocusChange(int change) {
                                    listener.changed(change);
                                }
                            })
                    .build();
        }

        @Override
        public int request(Object request) {
            return manager.requestAudioFocus((AudioFocusRequest) request);
        }

        @Override
        public int abandon(Object request) {
            return manager.abandonAudioFocusRequest((AudioFocusRequest) request);
        }

        @Override
        public boolean captureAvailable() {
            try {
                return !manager.isMicrophoneMute()
                        && manager.getActiveRecordingConfigurations().isEmpty();
            } catch (RuntimeException failure) {
                // checkstyle-allow IllegalCatch: an AudioManager binder failure reads as no capture
                return false;
            }
        }

        @Override
        public Object volume(String operation, int stream, int value, int flags) {
            switch (operation) {
                case "getStreamVolume":
                    return manager.getStreamVolume(stream);
                case "getStreamMaxVolume":
                    return manager.getStreamMaxVolume(stream);
                case "setStreamVolume":
                    manager.setStreamVolume(stream, value, flags);

                    return null;
                case "adjustStreamVolume":
                    manager.adjustStreamVolume(stream, value, flags);

                    return null;
                case "adjustVolume":
                    manager.adjustVolume(value, flags);

                    return null;
                case "isStreamMute":
                    return manager.isStreamMute(stream);
                case "setStreamMute":
                    manager.adjustStreamVolume(
                            stream,
                            value != 0 ? AudioManager.ADJUST_MUTE : AudioManager.ADJUST_UNMUTE,
                            0);

                    return null;
                case "isMicMute":
                    return manager.isMicrophoneMute();
                case "isMediaPlaying":
                    return manager.isMusicActive();
                default:
                    throw new IllegalArgumentException(operation);
            }
        }
    }

    private static boolean exact(String signature) {
        switch (signature) {
            case "abandonAudioFocus(Lcom/qinggan/audiopolicy/IAudioPolicyCallback;Ljava/lang/String;Ljava/lang/String;Z)I":
            case "abandonAudioPolicy(Lcom/qinggan/audiopolicy/IAudioPolicyCallback;Ljava/lang/String;Z)I":
            case "adjustStreamVolume(IIII)V":
            case "adjustVolume(II)V":
            case "getA2dpAudioPolicyInfo(Ljava/lang/String;)Lcom/qinggan/audiopolicy/AudioPolicyInfo;":
            case "getAudioClients()Ljava/util/List;":
            case "getAudioPolicyInfo(I)Ljava/util/List;":
            case "getCurrAudioPolicyInfoByChannel(I)Lcom/qinggan/audiopolicy/AudioPolicyInfo;":
            case "getCurrMediaAudioPolicyInfoByChannel(I)Lcom/qinggan/audiopolicy/AudioPolicyInfo;":
            case "getCurrentAudioPolicyInfo(ILjava/lang/String;)Lcom/qinggan/audiopolicy/AudioPolicyInfo;":
            case "getLastMediaAudioPolicyInfoByChannel(I)Lcom/qinggan/audiopolicy/AudioPolicyInfo;":
            case "getStreamMaxVolume(I)I":
            case "getStreamVolume(ILjava/lang/String;)I":
            case "isMasterMute()Z":
            case "isMediaPlaying()Z":
            case "isMicMute()Z":
            case "isNaviMix()Z":
            case "setNaviMix(Z)V":
            case "isStreamMute(I)Z":
            case "registerAudioClientCallback(Lcom/qinggan/audiopolicy/IAudioClientCallback;Ljava/lang/String;)Z":
            case "registerAudioPolicyInfoCallback(Lcom/qinggan/audiopolicy/IAudioPolicyInfoCallback;Ljava/lang/String;)Z":
            case "requestAudioFocus(Lcom/qinggan/audiopolicy/IAudioPolicyCallback;Ljava/lang/String;IILjava/lang/String;II)I":
            case "requestAudioPolicy(IILandroid/os/IBinder;Lcom/qinggan/audiopolicy/IAudioPolicyCallback;Ljava/lang/String;Ljava/lang/String;I)I":
            case "requestAudioPolicyEx(IILandroid/os/IBinder;Lcom/qinggan/audiopolicy/IAudioPolicyCallback;Ljava/lang/String;Ljava/lang/String;II)I":
            case "setMasterMute(ZI)V":
            case "setMediaPlaying(ZLandroid/os/Bundle;)V":
            case "setMicMute(Z)I":
            case "setStreamMute(IZ)V":
            case "setStreamVolume(IIILjava/lang/String;)V":
            case "unregisterAudioClientCallback(Lcom/qinggan/audiopolicy/IAudioClientCallback;)Z":
            case "unregisterAudioPolicyClient(Ljava/lang/String;)V":
            case "unregisterAudioPolicyInfoCallback(Lcom/qinggan/audiopolicy/IAudioPolicyInfoCallback;)Z":
                return true;
            default:
                return false;
        }
    }

    private static int unsupportedLogs;

    private static synchronized void logUnsupported(String route) {
        if (unsupportedLogs < 8) {
            unsupportedLogs++;
            System.err.println("OemAudioPolicy unsupported: " + route);
        }
    }

    private static UnsupportedOperationException unsupported(String message) {
        return new UnsupportedOperationException("OemAudioPolicy: " + message);
    }

    private static int number(Object value) {
        return ((Number) value).intValue();
    }

    private static IBinder binder(Object callback) {
        return callback instanceof IInterface ? ((IInterface) callback).asBinder() : null;
    }

    private static int mediaStream(int stream) throws RemoteException {
        if (stream == 3 || stream == 4) {
            return AudioManager.STREAM_MUSIC;
        }

        throw unsupported("unmapped OEM stream " + stream);
    }

    private static final class Entry implements IBinder.DeathRecipient {
        final Session owner;
        String id, pkg;
        final int stream, usage, gain;
        volatile Object callback;
        volatile IBinder token;
        Object request;
        int state = 399;
        long observedOrder;
        boolean cancelled,
                pending = true,
                requested,
                abandonInFlight,
                active,
                ducked,
                duckEmitted,
                paused,
                mixed,
                mayDuck;
        Entry duckCause;
        final List<Integer> earlyChanges = new ArrayList<>();

        Entry(Session owner, int stream, Object callback, IBinder token, int usage, int gain) {
            this.owner = owner;
            this.stream = stream;
            this.callback = callback;
            this.token = token;
            this.usage = usage;
            this.gain = gain;
        }

        @Override
        public void binderDied() {
            owner.cancel(this);
        }
    }

    /** Callback work decided under the session lock for one focus change. */
    private static final class FocusStep {
        int code;
        int pairState;
        boolean remove;
        boolean changed = true;
        Entry pair;
    }

    private static final class PolicySnapshot {
        final String id, pkg;
        final int stream;
        final long observedOrder;

        PolicySnapshot(Entry e) {
            id = e.id;
            pkg = e.pkg;
            stream = e.stream;
            observedOrder = e.observedOrder;
        }
    }

    private static final class Registration implements IBinder.DeathRecipient {
        final Session owner;
        final Object callback;
        final IBinder token;
        final Map<IBinder, Registration> registry;

        Registration(Session owner, Object callback, Map<IBinder, Registration> registry) {
            this.owner = owner;
            this.callback = callback;
            this.token = binder(callback);
            this.registry = registry;
        }

        @Override
        public void binderDied() {
            synchronized (owner) {
                if (registry.get(token) == this) {
                    registry.remove(token);
                }
            }
        }
    }

    private static final class Session {
        final Platform platform;
        final Config config;
        final Map<String, Entry> entries = new LinkedHashMap<>();
        // The opaque platform request retains its listener/Entry/Session until confirmed release;
        // client payload is detached.
        final IdentityHashMap<Entry, Boolean> pendingCleanup = new IdentityHashMap<>();
        final IdentityHashMap<IBinder, Registration> clients = new IdentityHashMap<>();
        final IdentityHashMap<IBinder, Registration> infos = new IdentityHashMap<>();
        PolicySnapshot lastMedia;
        long observedOrder;
        boolean stopped;

        // Written only so that a non-Boolean OEM media report fails as before.
        @SuppressWarnings("UnusedVariable")
        boolean reportedMediaPlaying;

        Session(Platform platform, Config config) {
            this.platform = platform;
            this.config = config;
        }

        synchronized boolean hasPending() {
            return !pendingCleanup.isEmpty();
        }

        void unlink(IBinder token, IBinder.DeathRecipient recipient) {
            if (token == null) {
                return;
            }

            try {
                token.unlinkToDeath(recipient, 0);
            } catch (java.util.NoSuchElementException failure) {
                logUnsupported("death recipient unlink failed");
            }
        }

        void clearPayload(Entry e) {
            synchronized (this) {
                e.id = null;
                e.pkg = null;
                e.callback = null;
                e.token = null;
                e.earlyChanges.clear();
            }
        }

        void attemptCleanup(Entry e) {
            Object request;

            synchronized (this) {
                if (!pendingCleanup.containsKey(e) || e.pending || e.abandonInFlight) {
                    return;
                }

                e.abandonInFlight = true;
                request = e.request;
            }

            boolean released = false;

            try {
                released = platform.abandon(request) == 1;

                if (!released) {
                    logUnsupported("focus cleanup pending: abandon denied");
                }
            } catch (RuntimeException failure) {
                // checkstyle-allow IllegalCatch: an AudioManager binder failure keeps the cleanup
                // pending
                logUnsupported("focus cleanup pending: abandon exception");
            } finally {
                synchronized (this) {
                    e.abandonInFlight = false;

                    if (released) {
                        pendingCleanup.remove(e);
                        e.requested = false;
                        e.request = null;
                    }
                }

                pruneRetired(this);
            }
        }

        void retryPending() {
            List<Entry> copy;

            synchronized (this) {
                copy = new ArrayList<>(pendingCleanup.keySet());
            }

            for (Entry e : copy) {
                attemptCleanup(e);
            }
        }

        void cancel(Entry e) {
            Entry pair = null;
            int pairState = 0;
            IBinder token;
            boolean first;

            synchronized (this) {
                first = !e.cancelled;

                if (first) {
                    e.cancelled = true;
                    e.active = false;
                    e.state = 301;
                    e.mayDuck = false;
                    pair = releaseCause(e);
                    pairState = pair == null ? 0 : pair.state;

                    if (entries.get(e.id) == e) {
                        entries.remove(e.id);
                    }
                }

                if (e.requested) {
                    pendingCleanup.put(e, Boolean.TRUE);
                }

                token = e.token;
                e.token = null;
                e.callback = null;
            }

            try {
                attemptCleanup(e);
            } finally {
                unlink(token, e);

                if (first) {
                    try {
                        notifyRecord(e, 301);

                        if (pair != null) {
                            notifyRecord(pair, pairState);
                        }
                    } finally {
                        clearPayload(e);
                    }
                }
            }
        }

        void stop() {
            List<Entry> copy;
            List<Registration> callbacks;

            synchronized (this) {
                stopped = true;
                lastMedia = null;
                copy = new ArrayList<>(entries.values());

                for (Entry e : pendingCleanup.keySet()) {
                    if (!copy.contains(e)) {
                        copy.add(e);
                    }
                }

                callbacks = new ArrayList<>(clients.values());
                callbacks.addAll(infos.values());
                clients.clear();
                infos.clear();
            }

            try {
                for (Entry e : copy) {
                    try {
                        cancel(e);
                    } catch (RuntimeException failure) {
                        // checkstyle-allow IllegalCatch: one failed entry cleanup never stops the
                        // others
                        logUnsupported("entry cleanup failed");
                    }
                }
            } finally {
                for (Registration r : callbacks) {
                    unlink(r.token, r);
                }
            }
        }

        private static boolean captureArguments(Object[] a) {
            return number(a[0]) == 20
                    && number(a[1]) == -1
                    && number(a[6]) == 0
                    && number(a[7]) == 105
                    && "AudioPolicy_For_Record105".equals(a[4])
                    && "com.qinggan.camera".equals(a[5]);
        }

        // Compares identity on purpose: the same Binder, token or instance, never an equal one.
        @SuppressWarnings("ReferenceEquality")
        int requestCapture(Object[] a) throws RemoteException {
            if (!captureArguments(a)) {
                return 0;
            }

            IBinder token = (IBinder) a[2];
            Object callback = a[3];
            IBinder callbackToken = binder(callback);

            if (token == null || callbackToken == null) {
                return 0;
            }

            synchronized (this) {
                if (stopped) {
                    return 0;
                }

                Entry old = entries.get((String) a[4]);

                if (old != null) {
                    return old.stream == 20
                                    && old.active
                                    && old.token == token
                                    && binder(old.callback) == callbackToken
                            ? 1
                            : 0;
                }

                if (captureBusy() || !platform.captureAvailable()) {
                    return 0;
                }

                return startCapture(a, callback, token);
            }
        }

        /**
         * Returns whether another capture or phone entry holds focus; the caller holds this lock.
         */
        private boolean captureBusy() {
            for (Entry e : entries.values()) {
                if (!e.cancelled && (e.stream == 20 || e.stream == 4)) {
                    return true;
                }
            }

            return false;
        }

        /** Links and publishes a new capture entry; the caller holds this lock. */
        private int startCapture(Object[] a, Object callback, IBinder token) {
            Entry entry = new Entry(this, 20, callback, token, 0, 1);

            entry.id = (String) a[4];
            entry.pkg = (String) a[5];

            try {
                token.linkToDeath(entry, 0);
            } catch (RemoteException dead) {
                return 0;
            }

            if (entry.cancelled) {
                unlink(token, entry);

                return 0;
            }

            entry.pending = false;
            entry.active = true;
            entry.state = 307;
            entry.observedOrder = ++observedOrder;
            entries.put(entry.id, entry);

            return 1;
        }

        int request(Object[] a, boolean alias, boolean extended) throws RemoteException {
            if (!alias && extended && number(a[0]) == 20) {
                return requestCapture(a);
            }

            int stream = alias ? -1 : number(a[0]); // Alias supplies attributes, not a proven OEM

            // stream identity.
            if (!alias && stream != 3 && stream != 4) {
                logUnsupported("request OEM stream");

                return 0;
            }

            final Entry e = alias ? aliasEntry(a, extended) : streamEntry(a, extended, stream);

            if (e == null) {
                return 0;
            }

            String id = e.id;
            IBinder token = e.token;

            if (!register(e, id, token)) {
                return 0;
            }

            Integer result = platformRequest(e, token);

            return result == null ? 0 : granted(e, id, token, result);
        }

        /** Returns the platform result, or null after a rejected request is undone. */
        private Integer platformRequest(Entry e, IBinder token) {
            try {
                return platform.request(e.request);
            } catch (IllegalArgumentException invalid) {
                synchronized (this) {
                    e.pending = false;
                    e.requested = false;
                    pendingCleanup.remove(e);
                }

                cancel(e);
                unlink(token, e);
                pruneRetired(this);

                return null;
            } catch (RuntimeException failure) {
                // checkstyle-allow IllegalCatch: a failed platform request is undone, then rethrown
                synchronized (this) {
                    e.pending = false;
                }

                cancel(e);
                unlink(token, e);
                throw failure;
            }
        }

        /** Reads the OEM stream request arguments; null rejects the request. */
        private Entry streamEntry(Object[] a, boolean extended, int stream) {
            Object callback = a[3];
            String id = (String) a[4], pkg = (String) a[5];
            IBinder token = (IBinder) a[2];
            int flags = number(a[6]);

            if (!validRequest(id, callback, token, 1, flags) || (extended && number(a[7]) != 0)) {
                return null;
            }

            return withRequest(
                    entry(id, pkg, stream, callback, token, 1, 1), stream == 3 ? 0 : 19, flags);
        }

        /** Reads the attribute alias request arguments; null rejects the request. */
        private Entry aliasEntry(Object[] a, boolean extended) {
            Object callback = a[0];
            String id = (String) a[1], pkg = (String) a[4];
            IBinder token = binder(callback);
            int gain = number(a[5]);
            int flags = number(a[6]);

            if (!validRequest(id, callback, token, gain, flags)
                    || (extended && number(a[7]) != 0)) {
                return null;
            }

            return withRequest(
                    entry(id, pkg, -1, callback, token, number(a[2]), gain), number(a[3]), flags);
        }

        private static boolean validRequest(
                String id, Object callback, IBinder token, int gain, int flags) {
            // The extra OEM argument checked by the callers has no proven public-platform meaning.
            return id != null
                    && callback != null
                    && token != null
                    && gain >= 1
                    && gain <= 3
                    && (flags & ~1) == 0;
        }

        private Entry entry(
                String id,
                String pkg,
                int stream,
                Object callback,
                IBinder token,
                int usage,
                int gain) {
            Entry e = new Entry(this, stream, callback, token, usage, gain);

            e.id = id;
            e.pkg = pkg;

            return e;
        }

        /** Adds the platform focus request to the entry; null rejects the request. */
        private Entry withRequest(final Entry e, int content, int flags) {
            if (content < 0 || content > 4 || e.usage < 0 || e.usage > 16) {
                return null;
            }

            try {
                e.request =
                        platform.create(
                                e.usage,
                                content,
                                e.gain,
                                flags,
                                new Change() {
                                    @Override
                                    public void changed(int change) {
                                        focus(e, change);
                                    }
                                });
            } catch (IllegalArgumentException invalid) {
                return null;
            }

            return e;
        }

        /** Publishes the entry and links its token; false rejects the request. */
        private boolean register(Entry e, String id, IBinder token) throws RemoteException {
            Entry old;

            synchronized (this) {
                if (stopped) {
                    return false;
                }

                old = entries.put(id, e);
            }

            if (old != null) {
                cancel(old);
            }

            try {
                token.linkToDeath(e, 0);
            } catch (RemoteException dead) {
                cancel(e);

                return false;
            }

            synchronized (this) {
                if (e.cancelled || stopped) {
                    e.pending = false;
                    unlink(token, e);

                    return false;
                }

                e.requested = true;
            }

            return true;
        }

        private int granted(Entry e, String id, IBinder token, int result) throws RemoteException {
            boolean stale;
            List<Integer> early;

            synchronized (this) {
                e.pending = false;
                stale = e.cancelled || stopped || entries.get(id) != e;

                if (!stale) {
                    recordResult(e, id, result);
                }

                early = new ArrayList<>(e.earlyChanges);
                e.earlyChanges.clear();
            }

            if (stale) {
                synchronized (this) {
                    if (result == 1 || result == 2) {
                        pendingCleanup.put(e, Boolean.TRUE);
                    } else {
                        pendingCleanup.remove(e);
                        e.requested = false;
                    }
                }

                unlink(token, e);
                attemptCleanup(e);
                pruneRetired(this);

                return 0;
            }

            if (result != 1 && result != 2) {
                synchronized (this) {
                    e.requested = false;
                }

                unlink(token, e);

                try {
                    notifyRecord(e, 301);
                } finally {
                    clearPayload(e);
                }

                return 0;
            }

            notifyRecord(e, result == 1 ? 307 : 399);

            if (result == 1) {
                emit(e, 1);
            }

            for (Integer change : early) {
                focus(e, change);
            }

            if (result == 1 && e.usage == 12 && e.gain == 3) {
                duckObservedMedia(e);
            }

            return result;
        }

        /** Applies the platform request result to a current entry; the caller holds this lock. */
        private void recordResult(Entry e, String id, int result) {
            e.active = result == 1;

            if (result == 1) {
                e.observedOrder = ++observedOrder;
            }

            e.state = result == 1 ? 307 : result == 2 ? 399 : 301;

            if (result == 1 && (e.stream == 3 || e.stream == 4)) {
                lastMedia = new PolicySnapshot(e);
            }

            if (result != 1 && result != 2) {
                entries.remove(id);
                e.cancelled = true;
            }
        }

        private void duckObservedMedia(Entry e) {
            List<Entry> observed = new ArrayList<>();

            synchronized (this) {
                for (Entry media : entries.values()) {
                    if (media != e && media.mayDuck && !media.cancelled) {
                        observed.add(media);
                    }
                }
            }

            // Resolve a synchronous media duck callback only after our Navi request is actually
            // granted.
            for (Entry media : observed) {
                focus(media, -3);
            }
        }

        void setMix(boolean value) {
            synchronized (config) {
                boolean before = config.read();

                if (!config.commit(value) || config.read() != value) {
                    if (config.read() != before) {
                        config.commit(before);
                    }

                    throw unsupported("navi_mix persistence failed");
                }
            }

            List<Entry> observed = new ArrayList<>();

            synchronized (this) {
                for (Entry e : entries.values()) {
                    if (e.mayDuck && !e.cancelled) {
                        observed.add(e);
                    }
                }
            }

            // Recompute only conflicts backed by an actual focus callback; settings never grant
            // focus.
            for (Entry e : observed) {
                focus(e, -3);
            }
        }

        Entry ownNavi(Entry media) {
            Entry result = null;

            if (media.usage != 1) {
                return null;
            }

            for (Entry e : entries.values()) {
                if (e != media
                        && e.usage == 12
                        && e.gain == 3
                        && e.active
                        && !e.cancelled
                        && (result == null || e.observedOrder > result.observedOrder)) {
                    result = e;
                }
            }

            return result;
        }

        Entry releaseCause(Entry media) {
            Entry cause = media.duckCause;

            media.duckCause = null;

            if (cause == null || cause.cancelled || !cause.active) {
                return null;
            }

            for (Entry e : entries.values()) {
                if (e != media && e.mayDuck && e.duckCause == cause && !e.cancelled) {
                    return null;
                }
            }

            if (cause.state != 304 && cause.state != 306) {
                return null;
            }

            cause.state = 307;

            return cause;
        }

        void focus(Entry e, int change) {
            FocusStep step = new FocusStep();
            int eventState;
            boolean mixing = config.read();

            synchronized (this) {
                if (stopped || e.cancelled || entries.get(e.id) != e) {
                    return;
                }

                if (e.pending) {
                    e.earlyChanges.add(change);

                    return;
                }

                if (!applyFocus(e, change, mixing, step)) {
                    return;
                }

                if (change == 1 && (e.stream == 3 || e.stream == 4)) {
                    lastMedia = new PolicySnapshot(e);
                }

                eventState = e.state;
            }

            publishFocus(e, step, eventState);
        }

        private void publishFocus(Entry e, FocusStep step, int eventState) {
            if (step.remove) {
                unlink(e.token, e);
            }

            if (step.changed) {
                notifyRecord(e, eventState);
            }

            if (step.pair != null) {
                notifyRecord(step.pair, step.pairState);
            }

            if (step.code != 0) {
                emit(e, step.code, step.remove);
            }

            if (step.remove) {
                synchronized (this) {
                    if (entries.get(e.id) == e) {
                        entries.remove(e.id);
                    }

                    e.requested = false;
                }

                clearPayload(e);
            }
        }

        /**
         * Applies a platform focus change to the entry state; the caller holds this lock. Returns
         * false when the change needs no callback.
         */
        private boolean applyFocus(Entry e, int change, boolean mixing, FocusStep step) {
            switch (change) {
                case 1:
                    if (e.active && e.state == 307 && !e.ducked && !e.paused && !e.mixed) {
                        return false;
                    }

                    step.code =
                            e.duckEmitted
                                    ? 6
                                    : e.paused ? 3 : e.active || e.mayDuck || e.mixed ? 0 : 1;
                    e.state = 307;
                    e.observedOrder = ++observedOrder;
                    e.active = true;
                    e.ducked = false;
                    e.paused = false;
                    e.mixed = false;
                    e.mayDuck = false;
                    releaseFocusCause(e, step);

                    return true;
                case -1:
                    step.code = 4;
                    e.state = 301;
                    e.active = false;
                    e.cancelled = true;
                    e.mayDuck = false;
                    step.remove = true;
                    releaseFocusCause(e, step);

                    return true;
                case -2:
                    step.code = 2;
                    e.state = 303;
                    e.active = false;
                    e.paused = true;
                    e.mixed = false;
                    e.mayDuck = false;
                    releaseFocusCause(e, step);

                    return true;
                case -3:
                    duck(e, mixing, step);

                    return true;
                default:
                    return false;
            }
        }

        private void releaseFocusCause(Entry e, FocusStep step) {
            step.pair = releaseCause(e);

            if (step.pair != null) {
                step.pairState = step.pair.state;
            }
        }

        private void duck(Entry e, boolean mixing, FocusStep step) {
            e.mayDuck = true;
            int previous = e.state;

            if (mixing) {
                step.code = e.duckEmitted ? 6 : 0;
                e.state = 306;
                e.mixed = true;
                e.ducked = false;
            } else {
                step.code = e.duckEmitted ? 0 : 5;
                e.state = 305;
                e.ducked = true;
                e.mixed = false;
            }

            step.changed = previous != e.state;
            step.pair = ownNavi(e);

            if (step.pair != null) {
                e.duckCause = step.pair;
                step.pairState = mixing ? 306 : 304;

                if (step.pair.state == step.pairState) {
                    step.pair = null;
                } else {
                    step.pair.state = step.pairState;
                }
            }
            // An unattributed external duck callback uses this same configured generic mix policy.
        }

        void emit(Entry e, int code) {
            emit(e, code, false);
        }

        void emit(Entry e, int code, boolean terminal) {
            Object callback;
            String clientId;

            synchronized (this) {
                if (stopped || entries.get(e.id) != e || (!terminal && e.cancelled)) {
                    return;
                }

                if (code == 5) {
                    if (e.state != 305 || !e.ducked || e.duckEmitted) {
                        return;
                    }

                    e.duckEmitted = true;
                }

                if (code == 6) {
                    if (!e.duckEmitted || e.ducked) {
                        return;
                    }

                    e.duckEmitted = false;
                }

                callback = e.callback;
                clientId = e.id;
            }

            try {
                Class.forName("com.qinggan.audiopolicy.IAudioPolicyCallback")
                        .getMethod("onAudioPolicyMessage", int.class, String.class, int.class)
                        .invoke(callback, code, clientId, -1);
            } catch (ReflectiveOperationException ignored) {
                if (code == 5) {
                    synchronized (this) {
                        e.duckEmitted = false;
                    }
                } /* A failed remote client cannot affect focus ownership. */
            }
        }

        // Compares identity on purpose: the same Binder, token or instance, never an equal one.
        @SuppressWarnings("ReferenceEquality")
        int abandon(String id, Object callback) {
            Entry e;
            boolean pending;

            synchronized (this) {
                e = entries.get(id);

                if (e != null
                        && e.stream == 20
                        && (binder(callback) == null || binder(callback) != binder(e.callback))) {
                    return 0;
                }
            }

            if (e != null && e.stream == 20) {
                cancel(e);

                return 1;
            }

            synchronized (this) {
                e = entries.get(id);

                if (e == null || e.abandonInFlight) {
                    return 0;
                }

                pending = e.pending;

                if (!pending) {
                    e.abandonInFlight = true;
                }
            }

            if (pending) {
                cancel(e);

                return 0;
            }

            int result;

            try {
                result = platform.abandon(e.request);
            } finally {
                synchronized (this) {
                    e.abandonInFlight = false;
                }
            }

            if (result == 1) {
                releaseAbandoned(e, id);
            }

            return result == 1 ? 1 : 0;
        }

        private void releaseAbandoned(Entry e, String id) {
            Entry pair;
            int pairState;
            IBinder token;

            synchronized (this) {
                e.cancelled = true;
                e.active = false;
                e.state = 301;
                e.mayDuck = false;
                e.requested = false;
                e.request = null;
                pendingCleanup.remove(e);
                pair = releaseCause(e);
                pairState = pair == null ? 0 : pair.state;

                if (entries.get(id) == e) {
                    entries.remove(id);
                }

                token = e.token;
                e.token = null;
                e.callback = null;
            }

            unlink(token, e);

            try {
                notifyRecord(e, 301);

                if (pair != null) {
                    notifyRecord(pair, pairState);
                }
            } finally {
                clearPayload(e);
                pruneRetired(this);
            }
        }

        boolean register(Object callback, boolean info) {
            Map<IBinder, Registration> registry = info ? infos : clients;
            IBinder token = binder(callback);

            if (token == null) {
                return false;
            }

            Registration r = new Registration(this, callback, registry);

            synchronized (this) {
                if (stopped) {
                    return false;
                }

                if (registry.containsKey(token)) {
                    return true;
                }

                registry.put(token, r);
            }

            try {
                token.linkToDeath(r, 0);
            } catch (RemoteException dead) {
                synchronized (this) {
                    if (registry.get(token) == r) {
                        registry.remove(token);
                    }
                }

                return false;
            }

            synchronized (this) {
                if (!stopped && registry.get(token) == r) {
                    return true;
                }
            }

            token.unlinkToDeath(r, 0);

            return false;
        }

        boolean unregister(Object callback, boolean info) {
            Map<IBinder, Registration> registry = info ? infos : clients;
            Registration r;

            synchronized (this) {
                r = registry.remove(binder(callback));
            }

            if (r != null) {
                r.token.unlinkToDeath(r, 0);
            }

            return r != null;
        }

        Object client(Entry e, int state) throws RemoteException {
            return client(new PolicySnapshot(e), state);
        }

        Object client(PolicySnapshot e, int state) throws RemoteException {
            try {
                Class<?> type = Class.forName("com.qinggan.audiopolicy.AudioClient");
                Object value = type.getConstructor().newInstance();
                type.getMethod("setClientId", String.class).invoke(value, e.id);
                type.getMethod("setPackageName", String.class).invoke(value, e.pkg);
                type.getMethod("setStreamType", int.class).invoke(value, e.stream);
                type.getMethod("setState", int.class).invoke(value, state);

                return value;
            } catch (ReflectiveOperationException failure) {
                throw unsupported("AudioClient ABI unavailable");
            }
        }

        void invoke(Registration r, String api, String method, Class<?>[] types, Object... values) {
            synchronized (this) {
                if (stopped || r.registry.get(r.token) != r) {
                    return;
                }
            }

            try {
                Class.forName("com.qinggan.audiopolicy." + api)
                        .getMethod(method, types)
                        .invoke(r.callback, values);
            } catch (ReflectiveOperationException failure) {
                /* One remote callback cannot prevent the remaining notifications. */
            }
        }

        private synchronized boolean staleRecord(Entry changed, String id, int state) {
            return stopped
                    || changed.state != state
                    || (entries.get(id) != null && entries.get(id) != changed);
        }

        /** Returns the most recently granted active entry; the caller holds this lock. */
        private PolicySnapshot selectedSnapshot() {
            PolicySnapshot selected = null;

            for (Entry e : entries.values()) {
                if (e.active
                        && !e.cancelled
                        && (selected == null || e.observedOrder > selected.observedOrder)) {
                    selected = new PolicySnapshot(e);
                }
            }

            return selected;
        }

        void notifyRecord(Entry changed, int state) {
            List<Registration> clientCallbacks, infoCallbacks;
            PolicySnapshot record;
            PolicySnapshot selected;

            synchronized (this) {
                if (stopped
                        || changed.state != state
                        || (entries.get(changed.id) != null
                                && entries.get(changed.id) != changed)) {
                    return;
                }

                record = new PolicySnapshot(changed);
                clientCallbacks = new ArrayList<>(clients.values());
                infoCallbacks = new ArrayList<>(infos.values());
                selected = selectedSnapshot();
            }

            try {
                Object value = client(record, state);
                Class<?> clientType = Class.forName("com.qinggan.audiopolicy.AudioClient");

                for (Registration r : clientCallbacks) {
                    if (staleRecord(changed, record.id, state)) {
                        return;
                    }

                    invoke(
                            r,
                            "IAudioClientCallback",
                            "onAudioClientChange",
                            new Class<?>[] {clientType},
                            value);
                }

                if (staleRecord(changed, record.id, state)) {
                    return;
                }

                Object policy = selected == null ? null : info(selected);
                Class<?> infoType = Class.forName("com.qinggan.audiopolicy.AudioPolicyInfo");

                for (Registration r : infoCallbacks) {
                    if (staleRecord(changed, record.id, state)) {
                        return;
                    }

                    invoke(
                            r,
                            "IAudioPolicyInfoCallback",
                            "onAudioPolicyInfoChange",
                            new Class<?>[] {infoType},
                            policy);
                }
            } catch (ReflectiveOperationException
                    | RemoteException
                    | UnsupportedOperationException failure) {
                logUnsupported("callback payload ABI");
            }
        }

        void notifyMute(int stream, boolean muted) {
            List<Registration> callbacks;

            synchronized (this) {
                if (stopped) {
                    return;
                }

                callbacks = new ArrayList<>(infos.values());
            }

            for (Registration r : callbacks) {
                invoke(
                        r,
                        "IAudioPolicyInfoCallback",
                        "onStreamMuteStateChange",
                        new Class<?>[] {int.class, boolean.class},
                        stream,
                        muted);
            }
        }

        void notifyVolume(int stream, int mapped, int flags) {
            int actual = number(platform.volume("getStreamVolume", mapped, 0, 0));
            List<Registration> callbacks;

            synchronized (this) {
                if (stopped) {
                    return;
                }

                callbacks = new ArrayList<>(infos.values());
            }

            for (Registration r : callbacks) {
                invoke(
                        r,
                        "IAudioPolicyInfoCallback",
                        "onStreamVolumeChange",
                        new Class<?>[] {int.class, int.class, int.class},
                        stream,
                        actual,
                        flags);
            }
        }

        Object changeVolume(String name, int stream, int value, int flags) throws RemoteException {
            int mapped = mediaStream(stream);

            platform.volume(name, mapped, value, flags);
            notifyVolume(stream, mapped, flags);

            return null;
        }

        Object info(PolicySnapshot e) throws RemoteException {
            try {
                Class<?> type = Class.forName("com.qinggan.audiopolicy.AudioPolicyInfo");
                Object info = type.getConstructor().newInstance();
                type.getMethod("setClientId", String.class).invoke(info, e.id);
                type.getMethod("setPackageName", String.class).invoke(info, e.pkg);
                type.getMethod("setStreamType", int.class).invoke(info, e.stream);

                return info;
            } catch (ReflectiveOperationException failure) {
                throw unsupported("AudioPolicyInfo ABI unavailable");
            }
        }

        Object query(String name, Object[] a) throws RemoteException {
            List<PolicySnapshot> observed = new ArrayList<>();

            synchronized (this) {
                for (Entry e : entries.values()) {
                    if (e.active && !e.cancelled) {
                        observed.add(new PolicySnapshot(e));
                    }
                }
            }

            if (name.equals("getAudioClients")) {
                List<Object> result = new ArrayList<>();

                synchronized (this) {
                    for (Entry e : entries.values()) {
                        if (!e.cancelled) {
                            result.add(client(e, e.state));
                        }
                    }
                }

                return result;
            }

            if (name.equals("getAudioPolicyInfo")) {
                List<Object> result = new ArrayList<>();

                for (PolicySnapshot e : observed) {
                    if (e.stream == number(a[0])) {
                        result.add(info(e));
                    }
                }

                return result;
            }

            if (name.equals("getLastMediaAudioPolicyInfoByChannel")) {
                PolicySnapshot last;

                synchronized (this) {
                    last = lastMedia;
                }

                return number(a[0]) == 0 && last != null ? info(last) : null;
            }

            if (name.endsWith("ByChannel") && number(a[0]) != 0) {
                return null;
            }

            return newestInfo(name, a, observed);
        }

        private Object newestInfo(String name, Object[] a, List<PolicySnapshot> observed)
                throws RemoteException {
            java.util.Collections.sort(
                    observed,
                    new java.util.Comparator<PolicySnapshot>() {
                        @Override
                        public int compare(PolicySnapshot a, PolicySnapshot b) {
                            return Long.compare(b.observedOrder, a.observedOrder);
                        }
                    });

            for (PolicySnapshot e : observed) {
                if (name.equals("getCurrentAudioPolicyInfo") && e.stream != number(a[0])) {
                    continue;
                }

                if (name.equals("getA2dpAudioPolicyInfo") && e.stream != 4) {
                    continue;
                }

                if (name.equals("getCurrMediaAudioPolicyInfoByChannel")
                        && e.stream != 3
                        && e.stream != 4) {
                    continue;
                }

                return info(e);
            }

            return null;
        }

        Object dispatch(String signature, Object[] a) throws RemoteException {
            // Caller guards use the complete descriptor; reject descriptor-less calls.
            int split = signature.indexOf('(');

            if (split <= 0 || !exact(signature)) {
                throw unsupported("exact signature required: " + signature);
            }

            String name = signature.substring(0, split);

            synchronized (this) {
                if (stopped) {
                    return stopped(signature, config);
                }
            }

            retryPending();

            switch (name) {
                case "requestAudioPolicy":
                    return request(a, false, false);
                case "requestAudioPolicyEx":
                    return request(a, false, true);
                case "requestAudioFocus":
                    return request(a, true, false);
                case "abandonAudioPolicy":
                case "abandonAudioFocus":
                    return abandon((String) a[1], a[0]);
                case "registerAudioClientCallback":
                    return register(a[0], false);
                case "registerAudioPolicyInfoCallback":
                    return register(a[0], true);
                case "unregisterAudioClientCallback":
                    return unregister(a[0], false);
                case "unregisterAudioPolicyInfoCallback":
                    return unregister(a[0], true);
                case "unregisterAudioPolicyClient":
                    {
                        Entry e;

                        synchronized (this) {
                            e = entries.get((String) a[0]);
                        }

                        if (e != null) {
                            cancel(e);
                        }

                        return null;
                    }
                case "getAudioPolicyInfo":
                case "getCurrentAudioPolicyInfo":
                case "getA2dpAudioPolicyInfo":
                case "getCurrAudioPolicyInfoByChannel":
                case "getCurrMediaAudioPolicyInfoByChannel":
                case "getLastMediaAudioPolicyInfoByChannel":
                case "getAudioClients":
                    return query(name, a);
                case "isMasterMute":
                    return false;
                case "setMasterMute":
                    throw unsupported("public AudioManager has no master mute");
                case "setMicMute":
                    return -1;
                case "setMediaPlaying":
                    synchronized (this) {
                        reportedMediaPlaying = (Boolean) a[0];
                    }

                    return null;
                case "isNaviMix":
                    return config.read();
                case "setNaviMix":
                    setMix((Boolean) a[0]);

                    return null;
                case "isMediaPlaying":
                case "isMicMute":
                    return platform.volume(name, 3, 0, 0);
                case "adjustVolume":
                    return changeVolume(name, 3, number(a[0]), number(a[1]));
                case "getStreamVolume":
                case "getStreamMaxVolume":
                    if (number(a[0]) != 3 && number(a[0]) != 4) {
                        logUnsupported(name);

                        return -1;
                    }

                    return platform.volume(name, mediaStream(number(a[0])), 0, 0);
                case "isStreamMute":
                    if (number(a[0]) != 3 && number(a[0]) != 4) {
                        logUnsupported(name);

                        return false;
                    }

                    return platform.volume(name, mediaStream(number(a[0])), 0, 0);
                case "setStreamMute":
                    {
                        int stream = number(a[0]), mapped = mediaStream(stream);
                        platform.volume(name, mapped, (Boolean) a[1] ? 1 : 0, 0);
                        notifyMute(stream, (Boolean) platform.volume("isStreamMute", mapped, 0, 0));

                        return null;
                    }
                case "setStreamVolume":
                    return changeVolume(name, number(a[0]), number(a[1]), number(a[2]));
                case "adjustStreamVolume":
                    if (number(a[2]) != 1) {
                        throw unsupported("unmapped OEM volume step");
                    }

                    return changeVolume(name, number(a[0]), number(a[1]), number(a[3]));
                default:
                    throw unsupported(signature);
            }
        }
    }
}
