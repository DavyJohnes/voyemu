package android.content;

public final class ComponentName {
    public final String pkg, name;

    public ComponentName(String p, String n) {
        pkg = p;
        name = n;
    }

    @Override
    public boolean equals(Object o) {
        return o instanceof ComponentName
                && pkg.equals(((ComponentName) o).pkg)
                && name.equals(((ComponentName) o).name);
    }

    @Override
    public int hashCode() {
        return 31 * pkg.hashCode() + name.hashCode();
    }
}
