package com.qinggan.canbus.service;

import android.app.Application;
import android.content.Intent;

/**
 * Application of the persistent CanBus package. Like the OEM CanApplication it starts the service
 * that publishes qg.canbus when the persistent process starts, before ActivityManager starts HOME.
 */
public final class CanApplication extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        startService(new Intent("qg.canbus").setPackage(getPackageName()));
    }
}
