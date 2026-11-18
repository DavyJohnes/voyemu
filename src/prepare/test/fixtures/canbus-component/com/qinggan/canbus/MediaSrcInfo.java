package com.qinggan.canbus;

public class MediaSrcInfo {
    public MediaSrcType source;
    public MediaState state;
    public float frequency;
    public String song, artist, album;

    public MediaSrcType getSrcType() {
        return source;
    }

    public MediaState getMediaState() {
        return state;
    }

    public float getFrequency() {
        return frequency;
    }

    public String getSongName() {
        return song;
    }

    public String getArtist() {
        return artist;
    }

    public String getAlbumName() {
        return album;
    }
}
