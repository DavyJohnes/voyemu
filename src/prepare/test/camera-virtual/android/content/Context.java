package android.content;

public class Context {
    public static final String CAMERA_SERVICE = "camera";
    public Object service;

    public Object getSystemService(String name) {
        return service;
    }
}
