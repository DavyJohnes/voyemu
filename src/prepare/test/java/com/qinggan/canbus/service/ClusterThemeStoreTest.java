package com.qinggan.canbus.service;

import android.content.SharedPreferences;

import java.util.HashMap;
import java.util.Map;

public final class ClusterThemeStoreTest {
    private static void need(boolean condition) {
        if (!condition) {
            throw new AssertionError("cluster theme persistence");
        }
    }

    private static final class Disk {
        final Map<String, Integer> values = new HashMap<>();
        boolean writable = true;
    }

    private static final class Preferences implements SharedPreferences {
        final Disk disk;
        final Map<String, Integer> values;

        Preferences(Disk disk) {
            this.disk = disk;
            values = new HashMap<>(disk.values);
        }

        public int getInt(String name, int fallback) {
            return values.getOrDefault(name, fallback);
        }

        public String getString(String name, String fallback) {
            return fallback;
        }

        public Editor edit() {
            return new Editor() {
                final Map<String, Integer> pending = new HashMap<>();

                public Editor putInt(String name, int next) {
                    pending.put(name, next);

                    return this;
                }

                public Editor putString(String name, String next) {
                    throw new AssertionError();
                }

                public Editor remove(String name) {
                    throw new AssertionError();
                }

                public boolean commit() {
                    values.putAll(pending);

                    if (disk.writable) {
                        disk.values.putAll(pending);
                    }

                    return disk.writable;
                }
            };
        }
    }

    public static void main(String[] args) {
        Disk saved = new Disk();

        saved.values.put("mode", -7);
        saved.values.put("skin", -9);
        saved.values.put("unrelated", 42);
        ClusterThemeStore retained = new ClusterThemeStore(new Preferences(saved), "simple");
        need(retained.mode() == -7 && retained.skin() == -9);
        retained.setSkin(1);
        need(saved.values.get("mode") == -7 && saved.values.get("unrelated") == 42);
        Disk disk = new Disk();
        ClusterThemeStore initial = new ClusterThemeStore(new Preferences(disk), "default");
        need(initial.mode() == 2 && initial.skin() == 1001 && disk.values.isEmpty());
        ClusterThemeStore unknown = new ClusterThemeStore(new Preferences(new Disk()), "");
        need(unknown.mode() == 2 && unknown.skin() == ClusterThemeStore.defaultSkin(2));
        initial.setSkin(1);
        need(initial.skin() == 1);
        ClusterThemeStore cold = new ClusterThemeStore(new Preferences(disk), "default");
        need(cold.skin() == 1 && cold.mode() == 2);
        need(new ClusterThemeStore(new Preferences(new Disk()), "simple").skin() == 3001);
        ClusterThemeStore golden = new ClusterThemeStore(new Preferences(new Disk()), "golden");
        need(golden.mode() == 5 && golden.skin() == 4001);
        cold.setMode(5);
        need(cold.mode() == 5 && cold.skin() == 4001);
        cold.setMode(4);
        need(cold.mode() == 4 && cold.skin() == 3001);
        cold.setSkin(3001);
        Preferences currentPreferences = new Preferences(disk);
        ClusterThemeStore next = new ClusterThemeStore(currentPreferences, "default");
        need(next.mode() == 4 && next.skin() == 3001);
        disk.writable = false;

        try {
            next.setSkin(1);
            throw new AssertionError("failed persistence acknowledged");
        } catch (IllegalStateException expected) {
            need(next.skin() == 3001);
            need(new ClusterThemeStore(new Preferences(disk), "simple").skin() == 3001);
        }

        try {
            next.setMode(2);
            throw new AssertionError("failed mode persistence acknowledged");
        } catch (IllegalStateException expected) {
            need(next.mode() == 4 && next.skin() == 3001);
        }

        disk.writable = true;
        next.setSkin(4001);
        ClusterThemeStore recovered = new ClusterThemeStore(currentPreferences, "golden");
        need(recovered.mode() == 4 && recovered.skin() == 4001);
        need(new ClusterThemeStore(new Preferences(disk), "default").skin() == 4001);
        System.out.println(
                "ClusterThemeStoreTest: PASS bootstrap, Classic default, skin-only, mode/skin,"
                        + " cold recreation, failed commit");
    }
}
