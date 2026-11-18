package android.content;

public class Context {
    public ClassLoader getClassLoader() {
        return getClass().getClassLoader();
    }
}
