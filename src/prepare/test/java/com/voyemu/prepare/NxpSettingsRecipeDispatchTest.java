package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Production dispatch check: settings recipe must reach donorSlot, never mergeClasses, in
 * producer-first order.
 */
public final class NxpSettingsRecipeDispatchTest {
    static final class Recorder implements Recipes.Target {
        final List<String> events = new ArrayList<>();

        @Override
        public int api() {
            return 28;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return Path.of("/unused");
        }

        @Override
        public Path output() {
            return null;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public void addDex(String source) {
            throw new AssertionError("addDex not allowed");
        }

        @Override
        public void removeEntry(String name) {
            throw new AssertionError("removeEntry not allowed");
        }

        @Override
        public void putEntry(Path source, String name) {
            throw new AssertionError("putEntry not allowed");
        }

        @Override
        public String originalSha256() {
            return "a20c1ee2c682aad846b93010356d434ef7e43ffb314b21186a91f348ba4c497b";
        }

        @Override
        public String originalPin() {
            return "a20c1ee2c682aad846b93010356d434ef7e43ffb314b21186a91f348ba4c497b";
        }

        @Override
        public void useLibraries(List<String> libraries) {
            events.add("useLibraries:" + libraries);
        }

        @Override
        public void mergeClasses(String reference, Map<String, Object> pins) {
            events.add("mergeClasses:" + reference);
        }

        @Override
        public void donorSlot(String reference, Map<String, Object> pins) {
            events.add("donorSlot:" + reference + ":" + pins.size());
        }
    }

    public static void main(String[] arguments) throws Exception {
        Map<String, Object> metadata =
                Json.object(Json.parse(Files.readString(Path.of(arguments[0]))));
        // 1. production dispatch on the pinned recipe
        Recorder recorder = new Recorder();

        Recipes.apply("nxp-systemui-settings-managers.json", metadata, recorder);
        List<String> expected =
                List.of(
                        "donorSlot:component:settings-runtime:"
                                + NxpSettingsLibrary.RUNTIME_TYPES.size(),
                        "donorSlot:component:settings-managers:"
                                + NxpSettingsLibrary.MANAGER_TYPES.size(),
                        "donorSlot:component:systemui-runtime-a:"
                                + NxpSettingsLibrary.SYSTEM_UI_RUNTIME_A.size(),
                        "donorSlot:component:systemui-runtime-b:"
                                + NxpSettingsLibrary.SYSTEM_UI_RUNTIME_B.size(),
                        "donorSlot:component:systemui-runtime-c:"
                                + NxpSettingsLibrary.SYSTEM_UI_RUNTIME_C.size());

        if (!recorder.events.equals(expected)) {
            throw new AssertionError("dispatch drift: " + recorder.events);
        }

        // 2. negative: slot order swapped in metadata must be rejected (mutate the live metadata
        // list, not a copy)
        Map<String, Object> swapped =
                Json.object(Json.parse(Files.readString(Path.of(arguments[0]))));
        @SuppressWarnings("unchecked")
        List<Object> swapCalls = (List<Object>) swapped.get("calls");
        Object slot1 = swapCalls.get(0), slot2 = swapCalls.get(1);
        swapCalls.set(0, slot2);
        swapCalls.set(1, slot1);
        expectRejection(swapped, "order");

        // 2b. positive gate proof: producer-first order must pass the same gate the swap failed
        try {
            Recipes.apply(
                    "nxp-systemui-settings-managers.json",
                    Json.object(Json.parse(Files.readString(Path.of(arguments[0])))),
                    new Recorder());
        } catch (Failure error) {
            throw new AssertionError("gate rejected the valid order", error);
        }

        // 3. negative: legacy mergeClasses operation must be rejected
        Map<String, Object> old = Json.object(Json.parse(Files.readString(Path.of(arguments[0]))));
        @SuppressWarnings("unchecked")
        List<Object> oldCalls = (List<Object>) old.get("calls");

        for (Object item : oldCalls) {
            @SuppressWarnings("unchecked")
            Map<String, Object> call = (Map<String, Object>) item;
            @SuppressWarnings("unchecked")
            List<Object> args = (List<Object>) call.get("args");

            if (args.get(0) instanceof String) {
                call.put("operation", "mergeClasses");
            }
        }

        expectRejection(old, "legacy mergeClasses");
        // 4. negative: required runtime slot removal
        Map<String, Object> bad = Json.object(Json.parse(Files.readString(Path.of(arguments[0]))));
        ((List<Object>) bad.get("calls")).remove(0);
        expectRejection(bad, "runtime slot removal");
        // 5. negative: non-null pin
        Map<String, Object> pinned =
                Json.object(Json.parse(Files.readString(Path.of(arguments[0]))));
        @SuppressWarnings("unchecked")
        List<Object> pinCalls = (List<Object>) pinned.get("calls");
        @SuppressWarnings("unchecked")
        Map<String, Object> slot = (Map<String, Object>) pinCalls.get(0);
        @SuppressWarnings("unchecked")
        Map<String, Object> selection =
                (Map<String, Object>) ((List<Object>) slot.get("args")).get(1);
        selection.put(
                selection.keySet().iterator().next(),
                "0000000000000000000000000000000000000000000000000000000000000000");
        expectRejection(pinned, "non-null pin");
        // 6. control: with production gate removed the swapped order would dispatch silently (zero
        // cannot pass silently)
        Recorder control = new Recorder();
        Map<String, Object> forced =
                Json.object(Json.parse(Files.readString(Path.of(arguments[0]))));
        @SuppressWarnings("unchecked")
        List<Object> forcedCalls = (List<Object>) forced.get("calls");
        Object f1 = forcedCalls.get(0), f2 = forcedCalls.get(1);
        forcedCalls.set(0, f2);
        forcedCalls.set(1, f1);

        try {
            Recipes.apply("generic-unregistered.json", forced, control);

            if (!control.events.equals(
                    List.of(
                            "donorSlot:component:settings-managers:"
                                    + NxpSettingsLibrary.MANAGER_TYPES.size(),
                            "donorSlot:component:settings-runtime:"
                                    + NxpSettingsLibrary.RUNTIME_TYPES.size(),
                            "donorSlot:component:systemui-runtime-a:"
                                    + NxpSettingsLibrary.SYSTEM_UI_RUNTIME_A.size(),
                            "donorSlot:component:systemui-runtime-b:"
                                    + NxpSettingsLibrary.SYSTEM_UI_RUNTIME_B.size(),
                            "donorSlot:component:systemui-runtime-c:"
                                    + NxpSettingsLibrary.SYSTEM_UI_RUNTIME_C.size()))) {
                throw new AssertionError("control dispatch wrong: " + control.events);
            }
        } catch (Failure error) {
            throw new AssertionError("control should bypass registered gate", error);
        }

        System.out.println(
                "NXP settings recipe dispatch: production path uses donorSlot producers-first; 4"
                        + " rejections plus unregistered control passed");
    }

    private static void expectRejection(Map<String, Object> metadata, String label) {
        try {
            Recipes.apply("nxp-systemui-settings-managers.json", metadata, new Recorder());
        } catch (Failure error) {
            return;
        } catch (Exception error) {
            throw new AssertionError("unexpected exception for " + label, error);
        }

        throw new AssertionError("missing rejection: " + label);
    }
}
