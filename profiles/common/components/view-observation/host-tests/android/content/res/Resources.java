package android.content.res;

public class Resources {
    public static class NotFoundException extends RuntimeException {}

    public String getResourceName(int id) {
        return "test:id/label";
    }

    public String getResourceTypeName(int id) {
        return "id";
    }

    public String getResourceEntryName(int id) {
        return "label";
    }
}
