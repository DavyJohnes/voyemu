package android.view;

import android.content.Context;

public class View {
    final Context c;
    Object tag;
    public Object parent;

    public View(Context c) {
        this.c = c;
    }

    public Object getParent() {
        return parent;
    }

    public Context getContext() {
        return c;
    }

    public Object getTag() {
        return tag;
    }

    public void setTag(Object t) {
        tag = t;
    }
}
