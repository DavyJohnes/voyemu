package dalvik.system;

/** Delegates to its parent; the test classes provide the QGVehicle types. */
public class PathClassLoader extends ClassLoader {
    public PathClassLoader(String path, ClassLoader parent) {
        super(parent);
    }
}
