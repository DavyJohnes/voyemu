package android.content.pm;

public class SigningInfo {
    public boolean multiple;
    public Signature[] current, history;

    public boolean hasMultipleSigners() {
        return multiple;
    }

    public Signature[] getApkContentsSigners() {
        return current;
    }

    public Signature[] getSigningCertificateHistory() {
        return history;
    }
}
