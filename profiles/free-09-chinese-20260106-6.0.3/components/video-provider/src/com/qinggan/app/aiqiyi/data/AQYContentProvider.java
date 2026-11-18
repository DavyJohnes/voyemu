package com.qinggan.app.aiqiyi.data;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;

/**
 * Stub provider matching the OEM Video 6.0.3 AQYContentProvider. Returns empty cursors; no real
 * video data on emulator.
 */
public class AQYContentProvider extends ContentProvider {
    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] p, String s, String[] a, String so) {
        return new MatrixCursor(p != null ? p : new String[0], 0);
    }

    @Override
    public String getType(Uri uri) {
        return "vnd.android.cursor.dir/vnd.qinggan.video";
    }

    @Override
    public Uri insert(Uri uri, ContentValues v) {
        return null;
    }

    @Override
    public int delete(Uri uri, String s, String[] a) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues v, String s, String[] a) {
        return 0;
    }
}
