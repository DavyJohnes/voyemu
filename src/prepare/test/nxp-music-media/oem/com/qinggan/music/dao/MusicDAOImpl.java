package com.qinggan.music.dao;

public class MusicDAOImpl {
    public static int queries;
    public static RuntimeException failure;
    public static final java.util.List<String> TRACKS =
            java.util.Arrays.asList("track1.wav", "track2.wav");

    public MusicDAOImpl(android.content.Context context) {}

    public java.util.List<String> getAllMusicList(com.qinggan.music.audiocom.External external) {
        queries++;

        if (external != com.qinggan.music.audiocom.External.USB) {
            throw new AssertionError("wrong volume");
        }

        if (failure != null) {
            throw failure;
        }

        return TRACKS;
    }
}
