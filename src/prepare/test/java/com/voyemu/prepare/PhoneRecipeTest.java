package com.voyemu.prepare;

import java.lang.reflect.Proxy;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

public final class PhoneRecipeTest {
    public static void main(String[] args) throws Exception {
        String name = "phone-provider.json";
        Map<String, Object> data = Json.object(Json.parse(Files.readString(Path.of(args[0]))));

        data.put("sourceSha256", FilesSupport.hash(Path.of(args[0])));
        AtomicInteger called = new AtomicInteger();
        Recipes.Target valid =
                (Recipes.Target)
                        Proxy.newProxyInstance(
                                Recipes.class.getClassLoader(),
                                new Class<?>[] {Recipes.Target.class},
                                (self, m, a) -> {
                                    if (m.getName().equals("api")) {
                                        return 28;
                                    }

                                    if (m.getName().equals("originalSha256")
                                            || m.getName().equals("originalPin")) {
                                        // The profile pins the original; any pin it declares is
                                        // accepted.
                                        return "1".repeat(64);
                                    }

                                    if (m.getName().equals("addPhoneProvider")) {
                                        called.incrementAndGet();

                                        return null;
                                    }

                                    throw new AssertionError("unexpected operation " + m);
                                });

        if (!Recipes.hasRegisteredCoverage(name, data)) {
            throw new AssertionError("missing registered source coverage");
        }

        Recipes.apply(name, data, valid);

        if (called.get() != 1) {
            throw new AssertionError();
        }

        System.out.println("PASS production recipe metadata/coverage/dispatch");
    }
}
