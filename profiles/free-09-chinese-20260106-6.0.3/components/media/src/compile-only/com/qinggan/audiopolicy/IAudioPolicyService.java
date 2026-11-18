package com.qinggan.audiopolicy;

public interface IAudioPolicyService extends android.os.IInterface {
    abstract class Stub extends android.os.Binder implements IAudioPolicyService {
        public static IAudioPolicyService asInterface(android.os.IBinder p0) {
            return null;
        }
    }

    int requestAudioPolicyEx(
            int p0,
            int p1,
            android.os.IBinder p2,
            IAudioPolicyCallback p3,
            String p4,
            String p5,
            int p6,
            int p7)
            throws android.os.RemoteException;
}
