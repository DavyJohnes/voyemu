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
        if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            context.startService(new Intent(context, SecondScreenService.class));
        }
    }
}
