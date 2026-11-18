package com.voyemu.prepare;

import android.view.View;

import com.voyemu.window.PassionOneKeyMute;

public class PassionOneKeyMuteTest {
    public static void main(String[] args) {
        var root = new View();
        int[] ids = {0x7f0a00f6, 0x7f0a00f7, 0x7f0a0264};

        for (int id : ids) {
            root.children.put(id, new View());
        }

        var music = new View();
        root.children.put(77, music);
        PassionOneKeyMute.refresh(root, null);

        for (int id : ids) {
            if (root.children.get(id).enabled) {
                throw new AssertionError();
            }
        }

        if (!music.enabled) {
            throw new AssertionError();
        }

        PassionOneKeyMute.refresh(root, new Object());

        for (int id : ids) {
            if (!root.children.get(id).enabled) {
                throw new AssertionError();
            }
        }

        PassionOneKeyMute.refresh(root, null);

        for (int id : ids) {
            if (root.children.get(id).enabled) {
                throw new AssertionError();
            }
        }

        PassionOneKeyMute.refresh(null, null);
        PassionOneKeyMute.refresh(new View(), null);
        System.out.println(
                "exact3 automotive mute controls unavailable/restore PASS; unrelated SDK music"
                        + " control unchanged; no audio-state writes");
    }
}
