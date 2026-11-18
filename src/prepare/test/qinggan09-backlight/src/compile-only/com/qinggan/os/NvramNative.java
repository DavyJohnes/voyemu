package com.qinggan.os;

final class NvramNative {
    static native String native_nvramread(int key);

    static native int native_nvramwrite(String value, int key);
}
