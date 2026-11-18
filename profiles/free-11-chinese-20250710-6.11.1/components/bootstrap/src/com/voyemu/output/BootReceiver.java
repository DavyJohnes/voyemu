package com.voyemu.output;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * Boot receiver for emulator second-screen display bootstrap. This is a lab helper, not an OEM
 * application.
 */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent != null && Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            // These API30 OEM services are declared/exported by this profile's owner APKs.
            // SystemServiceApp starts MultiDisplayService and MultiWindowService itself.
            context.startService(
                    new Intent()
                            .setClassName(
                                    "com.qinggan.canbus.service",
                                    "com.qinggan.canbus.service.CanBusService"));
            context.startService(
                    new Intent()
                            .setClassName(
                                    "com.qinggan.systemservice",
                                    "com.qinggan.systemservice.displaymanager.DisplayManagerService"));
            context.startService(
                    new Intent()
                            .setClassName(
                                    "com.qinggan.keymanager.service",
                                    "com.qinggan.keymanager.service.KeyManagerService"));
            context.startService(
                    new Intent()
                            .setClassName(
                                    "com.qinggan.app.setting",
                                    "com.qinggan.app.setting.service.MainSettingService"));
            context.startService(new Intent(context, SecondScreenService.class));
        }
    }
}
