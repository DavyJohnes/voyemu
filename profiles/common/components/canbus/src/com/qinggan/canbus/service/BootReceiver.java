package com.qinggan.canbus.service;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Boot receiver for CAN bus stub registration only. Second-screen display bootstrap moved to
 * com.voyemu.
 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            context.startService(new Intent(context, CanBusService.class));
        }
    }
}
