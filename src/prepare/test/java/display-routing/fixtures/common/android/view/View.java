package android.view;

import android.content.Context;

public class View {
    final Context context;
    public WindowManager parent;

    public View(Context c) {
        context = c;
    }

    public Context getContext() {
        return context;
    }
}
