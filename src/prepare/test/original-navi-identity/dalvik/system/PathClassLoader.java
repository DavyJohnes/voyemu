package dalvik.system;

import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Path;

public class PathClassLoader extends URLClassLoader {
    public static String fixture;

    public PathClassLoader(String path, ClassLoader parent) {
        super(urls(), parent);

        if (!"/system/framework/QGVehicle.jar".equals(path)) {
            throw new AssertionError("wrong path");
        }
    }

    private static URL[] urls() {
        try {
            return new URL[] {Path.of(fixture).toUri().toURL()};
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }
}
