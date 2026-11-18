package fixture;

/** Deliberately child-loader-owned instance exposing only the finite snapshot bridge. */
public final class LoaderOwnedAdapter extends HfpConstructorBoundary {
    public static int controls;

    public LoaderOwnedAdapter() {
        super();
    }

    public String control(String method, String payload) {
        controls++;

        if (!method.equals("snapshot") || !payload.equals("{}")) {
            throw new IllegalArgumentException();
        }

        return "{\"ok\":true,\"connected\":false,\"pbap_connected\":false}";
    }
}
