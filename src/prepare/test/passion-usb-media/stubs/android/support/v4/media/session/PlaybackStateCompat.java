package android.support.v4.media.session;

public class PlaybackStateCompat {
    public int state;
    public long actions;

    public PlaybackStateCompat(int state) {
        this.state = state;
    }

    public static class Builder {
        PlaybackStateCompat value;

        public Builder(PlaybackStateCompat source) {
            value = new PlaybackStateCompat(source.state);
        }

        public Builder setActions(long actions) {
            value.actions = actions;

            return this;
        }

        public PlaybackStateCompat build() {
            return value;
        }
    }
}
