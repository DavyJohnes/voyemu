package android.media;

public class AudioAttributes {
    public static class Builder {
        public Builder setUsage(int v) {
            return this;
        }

        public Builder setContentType(int v) {
            return this;
        }

        public AudioAttributes build() {
            return new AudioAttributes();
        }
    }
}
