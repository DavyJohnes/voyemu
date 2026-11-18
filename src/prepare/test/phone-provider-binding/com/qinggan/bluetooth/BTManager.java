package com.qinggan.bluetooth;

/** Own reflective factory boundary; production never compiles or packages this class. */
public final class BTManager {
    private static BTManager instance;

    // Read reflectively by the production code under test.
    @SuppressWarnings("UnusedVariable")
    private Object mAdapter;

    public static ClassLoader factoryLoader;
    public static int factoryFailures, factoryAttempts;

    // Tests inject the failures that getInstance throws.
    public static RuntimeException runtimeFailure;
    public static Error errorFailure;

    private BTManager() {
        factoryAttempts++;

        try {
            mAdapter =
                    Class.forName("fixture.LoaderOwnedAdapter", true, factoryLoader)
                            .getConstructor()
                            .newInstance();
        } catch (ReflectiveOperationException failure) {
            factoryFailures++;
            mAdapter = null;
        }
    }

    public static synchronized BTManager getInstance(android.content.Context context) {
        if (errorFailure != null) {
            throw errorFailure;
        }

        if (runtimeFailure != null) {
            throw runtimeFailure;
        }

        if (instance == null) {
            instance = new BTManager();
        }

        return instance;
    }

    // Clears the failures that tests injected.
    @SuppressWarnings("StaticAssignmentOfThrowable")
    public static synchronized void freshProcessBoundary() {
        instance = null;
        factoryFailures = 0;
        factoryAttempts = 0;
        runtimeFailure = null;
        errorFailure = null;
    }
}
