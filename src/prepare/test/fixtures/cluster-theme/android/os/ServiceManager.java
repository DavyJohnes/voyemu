package android.os;

import java.util.HashMap;
import java.util.Map;

/** Keeps the binders registered by name, as the platform service manager does. */
public final class ServiceManager {
    public static final Map<String, IBinder> SERVICES = new HashMap<>();

    private ServiceManager() {}

    public static void addService(String name, IBinder service) {
        SERVICES.put(name, service);
    }
}
