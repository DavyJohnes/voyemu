package com.qinggan.canbus.service;

public final class BootReceiver extends android.content.BroadcastReceiver {
    @Override
    public void onReceive(android.content.Context context, android.content.Intent intent) {
        if (android.content.Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            context.startForegroundService(
                    new android.content.Intent(context, RegistrationService.class));
        }
    }
}
