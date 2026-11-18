package android.app;

import android.os.Bundle;

public class ActivityOptions {
    int display = -1;

    public static ActivityOptions makeBasic() {
        return new ActivityOptions();
    }

    public ActivityOptions setLaunchDisplayId(int id) {
        display = id;

        return this;
    }

    public Bundle toBundle() {
        return new Bundle(display);
    }
}
