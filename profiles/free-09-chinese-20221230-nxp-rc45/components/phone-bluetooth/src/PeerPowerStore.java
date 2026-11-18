package com.qinggan.bluetooth.internal.ivt8;

import android.content.Context;
import android.util.AtomicFile;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;

/** Exact bounded private power choice. Calls and contact payloads are not persisted here. */
final class PeerPowerStore implements PeerPower.Store {
    private final AtomicFile file;

    PeerPowerStore(Context context) {
        file = new AtomicFile(new File(context.getFilesDir(), "voyemu-phone-power"));
    }

    @Override
    public boolean read() throws java.io.IOException {
        try (FileInputStream input = file.openRead()) {
            int state = input.read();

            if ((state != '0' && state != '1') || input.read() != '\n' || input.read() != -1) {
                throw new IllegalArgumentException("PHONE_POWER_CONFIG");
            }

            return state == '1';
        } catch (java.io.FileNotFoundException initial) {
            return true;
        }
    }

    @Override
    public void write(boolean enabled) throws java.io.IOException {
        FileOutputStream output = null;
        boolean written = false;

        try {
            output = file.startWrite();
            output.write(enabled ? '1' : '0');
            output.write('\n');
            file.finishWrite(output);
            written = true;
        } finally {
            if (!written && output != null) {
                file.failWrite(output);
            }
        }
    }
}
