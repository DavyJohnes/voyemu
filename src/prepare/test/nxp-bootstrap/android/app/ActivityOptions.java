package android.app;

import android.os.Bundle;

public class ActivityOptions {
    private int displayId;

    public static ActivityOptions makeBasic() {
        return new ActivityOptions();
    }

    public ActivityOptions setLaunchDisplayId(int id) {
        displayId = id;

        return this;
    }

    public Bundle toBundle() {
        Bundle result = new Bundle();

        result.displayId = displayId;

        return result;
    }
}
