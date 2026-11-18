package android.media;

public class CamcorderProfile {
    public static final int QUALITY_1080P = 6, QUALITY_720P = 5;
    public static boolean fhd, hd;

    public static boolean hasProfile(int id, int q) {
        return q == 6 ? fhd : q == 5 && hd;
    }
}
