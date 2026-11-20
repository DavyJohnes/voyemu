package com.qinggan.canbus;

public final class MediaSrcInfo implements android.os.Parcelable {
    @Override
    public int describeContents() {
        return 0;
    }

    public String getAlbumName() {
        return null;
    }

    public String getArtist() {
        return null;
    }

    public float getFrequency() {
        return 0;
    }

    public MediaState getMediaState() {
        return null;
    }

    public String getSongName() {
        return null;
    }

    public MediaSrcType getSrcType() {
        return null;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
