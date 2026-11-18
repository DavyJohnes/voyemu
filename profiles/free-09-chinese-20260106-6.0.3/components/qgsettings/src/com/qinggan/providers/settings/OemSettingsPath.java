package com.qinggan.providers.settings;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.attribute.BasicFileAttributes;
import java.util.LinkedHashSet;
import java.util.Set;

/** Existing guest OEM authority only; uncertain paths and changed properties fail closed. */
final class OemSettingsPath {
    static File resolve(String user, String multi, File ce, File de) throws IOException {
        if (!user.isEmpty() || !multi.equals("0")) {
            throw new IOException("unsupported guest properties");
        }

        Set<Path> found = new LinkedHashSet<>();

        for (File root : new File[] {ce, de}) {
            Path candidate = new File(root, "qinggan_settings.db").toPath();

            try {
                BasicFileAttributes metadata =
                        Files.readAttributes(candidate, BasicFileAttributes.class);

                if (metadata.isRegularFile()) {
                    found.add(candidate.toRealPath());
                }
            } catch (NoSuchFileException absent) {
                /* Only a confirmed absence is skipped. */
            }
        }

        if (found.size() != 1
                || !found.contains(new File(de, "qinggan_settings.db").toPath().toRealPath())) {
            throw new IOException("OEM DE settings authority unavailable or ambiguous");
        }

        return found.iterator().next().toFile();
    }

    static String current() {
        try {
            Class<?> properties = Class.forName("android.os.SystemProperties");
            java.lang.reflect.Method get = properties.getMethod("get", String.class);

            return resolve(
                            (String) get.invoke(null, "persist.sys.qg.userid"),
                            (String) get.invoke(null, "persist.sys.qg.multiuser"),
                            new File("/data/user/0/com.qinggan.providers.settings/files/databases"),
                            new File(
                                    "/data/user_de/0/com.qinggan.providers.settings/files/databases"))
                    .getPath();
        } catch (ReflectiveOperationException | IOException e) {
            throw new IllegalStateException("OEM settings authority unavailable", e);
        }
    }
}
