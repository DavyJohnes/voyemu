package android.content;

public class Intent {
    public static final String EXTRA_KEY_EVENT = "android.intent.extra.KEY_EVENT";
    private Object event;

    public Intent(Object event) {
        this.event = event;
    }

    public Object getParcelableExtra(String key) {
        return event;
    }
}
