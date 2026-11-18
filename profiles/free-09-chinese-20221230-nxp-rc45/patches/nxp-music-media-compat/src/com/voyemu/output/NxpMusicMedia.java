package com.voyemu.output;

import android.content.ContentResolver;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;

import java.util.LinkedHashSet;

/** Adapts the NXP Music metadata projection to the API28 MediaProvider. */
public final class NxpMusicMedia {
    private NxpMusicMedia() {}

    public static Cursor query(
            ContentResolver resolver,
            Uri uri,
            String[] projection,
            String selection,
            String[] args,
            String order) {
        if (projection == null) {
            return resolver.query(uri, null, selection, args, order);
        }

        String[] source = new String[projection.length];
        LinkedHashSet<String> columns = new LinkedHashSet<String>();

        for (int i = 0; i < projection.length; i++) {
            String name = projection[i];

            if (name.equals("title_pinyin")) {
                name = "title";
            } else if (name.equals("album_pinyin")) {
                name = "album";
            } else if (name.equals("artist_pinyin")) {
                name = "artist";
            } else if (name.equals("storage_uuid")
                    || (name.equals("album_art") && uri.getPath().endsWith("/audio/media"))) {
                name = null;
            }

            source[i] = name;

            if (name != null) {
                columns.add(name);
            }
        }

        MatrixCursor result = new MatrixCursor(projection);
        Cursor cursor =
                resolver.query(
                        uri, columns.toArray(new String[columns.size()]), selection, args, order);

        if (cursor == null) {
            return null;
        }

        try {
            while (cursor.moveToNext()) {
                Object[] row = new Object[projection.length];

                for (int i = 0; i < row.length; i++) {
                    if (source[i] == null) {
                        continue;
                    }

                    int index = cursor.getColumnIndexOrThrow(source[i]);

                    switch (cursor.getType(index)) {
                        case Cursor.FIELD_TYPE_INTEGER:
                            row[i] = cursor.getLong(index);
                            break;
                        case Cursor.FIELD_TYPE_FLOAT:
                            row[i] = cursor.getDouble(index);
                            break;
                        case Cursor.FIELD_TYPE_BLOB:
                            row[i] = cursor.getBlob(index);
                            break;
                        case Cursor.FIELD_TYPE_STRING:
                            row[i] = cursor.getString(index);
                            break;
                        default:
                            break;
                    }
                }

                result.addRow(row);
            }
        } finally {
            cursor.close();
        }

        return result;
    }
}
