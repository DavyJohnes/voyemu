package com.qinggan.app.music.model;

public class ModelManager {
    public static final ModelManager INSTANCE = new ModelManager();
    public com.qinggan.music.bean.MusicPlayList selected;

    public static ModelManager getInstance(android.content.Context context) {
        return INSTANCE;
    }

    public void selectPlayList(com.qinggan.music.bean.MusicPlayList value) {
        selected = value;
    }
}
