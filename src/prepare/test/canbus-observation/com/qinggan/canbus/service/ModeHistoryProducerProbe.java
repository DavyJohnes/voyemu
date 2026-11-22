package com.qinggan.canbus.service;

import android.content.SharedPreferences;

import java.lang.reflect.Proxy;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Writes the history of the actual ModeCommandHistory after an ACC ON packet and a client command.
 */
public final class ModeHistoryProducerProbe {
    static Object preferences(Class<?> type) {
        return Proxy.newProxyInstance(
                type.getClassLoader(),
                new Class<?>[] {type},
                (proxy, method, args) -> {
                    switch (method.getName()) {
                        case "getString":
                            return null;
                        case "edit":
                            return preferences(SharedPreferences.Editor.class);
                        case "putString":
                            return proxy;
                        case "commit":
                            return true;
                        default:
                            throw new AssertionError(method.getName());
                    }
                });
    }

    public static void main(String[] args) throws Exception {
        ModeCommandHistory history =
                new ModeCommandHistory(
                        (SharedPreferences) preferences(SharedPreferences.class),
                        "1b4e28ba-2fa1-11d2-883f-0016d3cca427",
                        "service");
        Map<String, Integer> packet = new LinkedHashMap<>();

        packet.put("DRIVING_MODE_SET", 1);
        packet.put("EPS_MODE_SET", 2);
        packet.put("PROP_MODE_SET", 1);
        history.accepted("DRIVING_MODE_SET", packet, ModeCommandHistory.ACC_ON);
        history.accepted(
                "IVI_SOC_MODESET", Map.of("IVI_SOC_MODESET", 4), ModeCommandHistory.CLIENT);
        Files.writeString(Path.of(args[0]), history.history("{\"schema\":1}").toString());
        System.out.println("ModeCommandHistory producer wrote 2 events");
    }
}
