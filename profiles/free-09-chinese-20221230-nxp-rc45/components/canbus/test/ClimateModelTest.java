package com.qinggan.canbus.service;

import java.util.HashMap;
import java.util.Map;

public final class ClimateModelTest {
    static final class Store implements ClimateModel.Store {
        Map<String, Integer> data = new HashMap<>();
        boolean fail;

        public Map<String, Integer> load() {
            return new HashMap<>(data);
        }

        public boolean save(Map<String, Integer> values) {
            if (fail) {
                return false;
            }

            data = new HashMap<>(values);

            return true;
        }
    }

    static void check(boolean value, String name) {
        if (!value) {
            throw new AssertionError(name);
        }
    }

    public static void main(String[] args) {
        Store store = new Store();
        ClimateModel model = new ClimateModel(store);

        check(
                model.get("AC_LEFT_TEMP") == 240
                        && model.get("AC_RIGHT_TEMP") == 240
                        && model.get("AC_POWER_SWITCH") == 1,
                "documented A9 emulator defaults");
        check(model.set(Map.of("AC_LEFT_TEMP", 225)), "persist left");
        check(model.get("AC_RIGHT_TEMP") == 240, "dual channels independent");
        model = new ClimateModel(store);
        check(model.get("AC_LEFT_TEMP") == 225, "restart restoration");
        check(model.set(Map.of("AC_DUAL", 0)), "disable dual");
        check(model.get("AC_RIGHT_TEMP") == 225, "disable dual synchronizes left to right");
        check(model.set(Map.of("AC_RIGHT_TEMP", 260)), "right in synchronized mode");
        check(
                model.get("AC_LEFT_TEMP") == 260 && model.get("AC_RIGHT_TEMP") == 260,
                "right synchronizes both");
        store.fail = true;
        check(!model.set(Map.of("AC_LEFT_TEMP", 230)), "persist failure explicit");
        check(
                model.get("AC_LEFT_TEMP") == 260
                        && new ClimateModel(store).get("AC_LEFT_TEMP") == 260,
                "failed persistence has no partial publication");
        store.fail = false;

        try {
            model.set(Map.of("AC_LEFT_TEMP", 225, "AC_RIGHT_TEMP", 100));
            throw new AssertionError("invalid temp accepted");
        } catch (IllegalArgumentException expected) {
        }

        check(model.get("AC_LEFT_TEMP") == 260, "invalid bundle atomic");
        check(model.set(Map.of("AC_LEFT_TEMP", 155)), "OEM Lo accepted");
        check(model.set(Map.of("AC_LEFT_TEMP", 305)), "OEM Hi accepted");
        check(
                model.get("AC_DOR_SWITCH") == -9999 && model.get("made_up") == -9999,
                "unsupported reads UNKNOWN");

        for (String key : new String[] {"AC_DOR_SWITCH", "made_up"}) {
            try {
                model.set(Map.of("AC_LEFT_TEMP", 225, key, 1));
                throw new AssertionError("unsupported patch accepted");
            } catch (IllegalArgumentException expected) {
            }
        }

        check(model.get("AC_LEFT_TEMP") == 305, "unsupported bundle has no partial write");

        for (Map<String, Integer> patch :
                java.util.List.of(
                        Map.of("AC_POWER_SWITCH", 2),
                        Map.of("AC_BLOWER", 9),
                        Map.of("AC_FLOW_MODE", 7),
                        Map.of("AC_WORKING_MODE", 4))) {
            try {
                model.set(patch);
                throw new AssertionError("invalid range accepted");
            } catch (IllegalArgumentException expected) {
            }
        }

        check(model.get("AC_WORKING_MODE") == 2, "A9 normal working mode default");
        System.out.println(
                "ClimateModelTest PASS: defaults, dual, persistence, atomic failure, Lo/Hi");
    }
}
