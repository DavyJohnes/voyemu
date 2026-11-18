package com.qinggan.music.audiocom;

public enum External {
    USB,
    INTERNAL;
    public java.util.List<?> entries;
    public final com.qinggan.music.bean.MusicPlayList playlist =
            new com.qinggan.music.bean.MusicPlayList();

    public void setPlayListEntry(java.util.List<?> tracks) {
        entries = tracks;
    }

    public com.qinggan.music.bean.MusicPlayList getPlayList() {
        return playlist;
    }
}
