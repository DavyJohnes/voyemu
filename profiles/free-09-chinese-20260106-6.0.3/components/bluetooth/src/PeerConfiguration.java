package com.qinggan.bluetooth.internal.ivt8;

import android.content.Context;
import android.util.AtomicFile;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.List;

/** Phone-private durable configuration only: never credentials or an active call. */
final class PeerConfiguration {
    private final AtomicFile file;

    PeerConfiguration(Context context) {
        file = new AtomicFile(new File(context.getFilesDir(), "voyemu-phone-peer.json"));
    }

    void restore(SyntheticPhonePeer peer) {
        try (FileInputStream input = file.openRead()) {
            byte[] bytes = new byte[4097];
            int count = 0, n;

            while (count < bytes.length
                    && (n = input.read(bytes, count, bytes.length - count)) > 0) {
                count += n;
            }

            if (count == bytes.length) {
                throw new IllegalArgumentException("PHONE_PEER_CONFIG_EXTENT");
            }

            JSONObject data =
                    new JSONObject(
                            new String(bytes, 0, count, java.nio.charset.StandardCharsets.UTF_8));

            if (data.length() != 3
                    || !(Integer.valueOf(1).equals(data.get("schema"))
                            || Long.valueOf(1).equals(data.get("schema")))
                    || !(data.get("peer_name") instanceof String)) {
                throw new IllegalArgumentException("PHONE_PEER_CONFIG");
            }

            JSONArray array = data.getJSONArray("contacts");

            if (array.length() != 2) {
                throw new IllegalArgumentException("PHONE_PEER_CONFIG");
            }

            List<SyntheticPhonePeer.Contact> contacts = new ArrayList<>();

            for (int i = 0; i < 2; i++) {
                JSONObject item = array.getJSONObject(i);

                if (item.length() != 2
                        || !(item.get("name") instanceof String)
                        || !(item.get("number") instanceof String)) {
                    throw new IllegalArgumentException("PHONE_PEER_CONFIG");
                }

                contacts.add(
                        new SyntheticPhonePeer.Contact(
                                item.getString("name"), item.getString("number")));
            }

            peer.configure(data.getString("peer_name"), contacts);
        } catch (java.io.FileNotFoundException absent) {
            /* Initial owned fixture uses source defaults. */
        } catch (java.io.IOException
                | org.json.JSONException
                | IllegalArgumentException
                | IllegalStateException invalid) {
            throw new IllegalStateException("PHONE_PEER_CONFIG_INVALID", invalid);
        }
    }

    void configure(SyntheticPhonePeer peer, String name, List<SyntheticPhonePeer.Contact> contacts)
            throws Exception {
        if (peer.connected() || peer.state() != SyntheticPhonePeer.TERMINATED) {
            throw new IllegalStateException("PHONE_CONNECTED");
        }

        SyntheticPhonePeer staged = new SyntheticPhonePeer();
        staged.configure(name, contacts);
        JSONObject data = new JSONObject().put("schema", 1).put("peer_name", name);
        JSONArray values = new JSONArray();

        for (SyntheticPhonePeer.Contact item : contacts) {
            values.put(new JSONObject().put("name", item.name).put("number", item.number));
        }

        byte[] bytes =
                data.put("contacts", values)
                        .toString()
                        .getBytes(java.nio.charset.StandardCharsets.UTF_8);
        FileOutputStream output = null;
        boolean written = false;

        try {
            output = file.startWrite();
            output.write(bytes);
            file.finishWrite(output);
            written = true;
        } finally {
            if (!written && output != null) {
                file.failWrite(output);
            }
        }

        // Same main Looper and already validated: no rejected configuration can reach durable
        // state.
        peer.configure(name, contacts);
    }
}
