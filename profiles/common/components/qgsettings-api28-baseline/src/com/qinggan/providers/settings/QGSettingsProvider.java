package com.qinggan.providers.settings;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.UriMatcher;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import android.net.Uri;
import android.util.Log;

/**
 * QGSettings ContentProvider — emulates the OEM qinggan.settings provider. Stores name/value pairs
 * for system, secure, global tables. Updates version system properties on write and notifies
 * observers.
 */
public class QGSettingsProvider extends ContentProvider {

    private static final String TAG = "QGSettingsProvider";
    private static final String AUTHORITY = "qinggan.settings";

    private static final int SYSTEM = 1;
    private static final int SYSTEM_NAME = 2;
    private static final int SECURE = 3;
    private static final int SECURE_NAME = 4;
    private static final int GLOBAL = 5;
    private static final int GLOBAL_NAME = 6;

    private static final String TABLE_SYSTEM = "system";
    private static final String TABLE_SECURE = "secure";
    private static final String TABLE_GLOBAL = "global";

    private static final String PROP_SYSTEM_VERSION = "sys.qg.settings_system_version";
    private static final String PROP_SECURE_VERSION = "sys.qg.settings_secure_version";
    private static final String PROP_GLOBAL_VERSION = "sys.qg.settings_global_version";

    private UriMatcher uriMatcher;
    private SettingsDbHelper dbHelper;

    @Override
    public boolean onCreate() {
        Context ctx = getContext();

        uriMatcher = new UriMatcher(UriMatcher.NO_MATCH);
        uriMatcher.addURI(AUTHORITY, "system", SYSTEM);
        uriMatcher.addURI(AUTHORITY, "system/*", SYSTEM_NAME);
        uriMatcher.addURI(AUTHORITY, "secure", SECURE);
        uriMatcher.addURI(AUTHORITY, "secure/*", SECURE_NAME);
        uriMatcher.addURI(AUTHORITY, "global", GLOBAL);
        uriMatcher.addURI(AUTHORITY, "global/*", GLOBAL_NAME);
        dbHelper = new SettingsDbHelper(ctx);

        return true;
    }

    /** An item URI (<table>/<name>) must only ever touch one row: the name
     * restriction is combined with the caller selection, so a null selection
     * cannot mean the whole table. */
    private static String[] combinedArgs(String name, String[] selectionArgs) {
        String[] combined = new String[(selectionArgs == null ? 0 : selectionArgs.length) + 1];

        combined[0] = name;

        if (selectionArgs != null) {
            System.arraycopy(selectionArgs, 0, combined, 1, selectionArgs.length);
        }

        return combined;
    }

    @Override
    public Cursor query(
            Uri uri,
            String[] projection,
            String selection,
            String[] selectionArgs,
            String sortOrder) {
        String table = tableForUri(uri);

        if (table == null) {
            Log.w(TAG, "query: unknown uri " + uri);

            return null;
        }

        String nameFilter = nameForUri(uri);
        SQLiteDatabase db = dbHelper.getReadableDatabase();
        String sel = selection;
        String[] args = selectionArgs;

        if (nameFilter != null) {
            sel = selection == null ? "name=?" : "name=? AND (" + selection + ")";
            args = combinedArgs(nameFilter, selectionArgs);
        }

        Cursor c =
                db.query(
                        table,
                        projection != null ? projection : new String[] {"name", "value"},
                        sel,
                        args,
                        null,
                        null,
                        sortOrder);
        c.setNotificationUri(getContext().getContentResolver(), uri);

        return c;
    }

    @Override
    public String getType(Uri uri) {
        return "vnd.android.cursor.dir/vnd.qinggan.settings";
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        String table = tableForUri(uri);

        if (table == null) {
            Log.w(TAG, "insert: unknown uri " + uri);

            return null;
        }

        SQLiteDatabase db = dbHelper.getWritableDatabase();
        long rowId = db.insertWithOnConflict(table, null, values, SQLiteDatabase.CONFLICT_REPLACE);

        if (rowId > 0) {
            bumpVersion(table);
            String name = values.getAsString("name");

            if (name != null) {
                Uri notifyUri = Uri.parse("content://" + AUTHORITY + "/" + table + "/" + name);
                getContext().getContentResolver().notifyChange(notifyUri, null);
            }

            getContext().getContentResolver().notifyChange(uri, null);
        }

        return rowId > 0 ? uri : null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        String table = tableForUri(uri);

        if (table == null) {
            return 0;
        }

        String name = nameForUri(uri);
        String sel = selection;
        String[] args = selectionArgs;

        if (name != null) {
            // The name placeholder comes first: combinedArgs binds name before
            // the caller's selection args, and a wrong order would let an item
            // update/delete touch a different row.
            sel = selection == null ? "name=?" : "name=? AND (" + selection + ")";
            args = combinedArgs(name, selectionArgs);
        }

        SQLiteDatabase db = dbHelper.getWritableDatabase();
        int count = db.delete(table, sel, args);

        if (count > 0) {
            bumpVersion(table);
            getContext().getContentResolver().notifyChange(uri, null);
        }

        return count;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        String table = tableForUri(uri);

        if (table == null) {
            return 0;
        }

        String name = nameForUri(uri);
        String sel = selection;
        String[] args = selectionArgs;

        if (name != null) {
            // The name placeholder comes first: combinedArgs binds name before
            // the caller's selection args, and a wrong order would let an item
            // update/delete touch a different row.
            sel = selection == null ? "name=?" : "name=? AND (" + selection + ")";
            args = combinedArgs(name, selectionArgs);
        }

        SQLiteDatabase db = dbHelper.getWritableDatabase();
        int count = db.update(table, values, sel, args);

        if (count > 0) {
            bumpVersion(table);
            getContext().getContentResolver().notifyChange(uri, null);
        }

        return count;
    }

    private String tableForUri(Uri uri) {
        switch (uriMatcher.match(uri)) {
            case SYSTEM:
            case SYSTEM_NAME:
                return TABLE_SYSTEM;
            case SECURE:
            case SECURE_NAME:
                return TABLE_SECURE;
            case GLOBAL:
            case GLOBAL_NAME:
                return TABLE_GLOBAL;
            default:
                return null;
        }
    }

    private String nameForUri(Uri uri) {
        switch (uriMatcher.match(uri)) {
            case SYSTEM_NAME:
            case SECURE_NAME:
            case GLOBAL_NAME:
                return uri.getLastPathSegment();
            default:
                return null;
        }
    }

    private synchronized void bumpVersion(String table) {
        String prop;

        switch (table) {
            case TABLE_SYSTEM:
                prop = PROP_SYSTEM_VERSION;
                break;
            case TABLE_SECURE:
                prop = PROP_SECURE_VERSION;
                break;
            case TABLE_GLOBAL:
                prop = PROP_GLOBAL_VERSION;
                break;
            default:
                return;
        }

        try {
            Class<?> sp = Class.forName("android.os.SystemProperties");
            java.lang.reflect.Method getLong = sp.getMethod("getLong", String.class, long.class);
            java.lang.reflect.Method set = sp.getMethod("set", String.class, String.class);
            long current = (Long) getLong.invoke(null, prop, 0L);
            set.invoke(null, prop, Long.toString(current + 1));
        } catch (Exception e) {
            // checkstyle-allow IllegalCatch: Hidden property failures cannot undo DB commit.
            Log.w(TAG, "bumpVersion: cannot set " + prop, e);
        }
    }

    private static class SettingsDbHelper extends SQLiteOpenHelper {
        private static final String DB_NAME = "qgsettings.db";
        private static final int DB_VERSION = 2;

        SettingsDbHelper(Context context) {
            super(context, DB_NAME, null, DB_VERSION);
        }

        @Override
        public void onCreate(SQLiteDatabase db) {
            for (String table : new String[] {TABLE_SYSTEM, TABLE_SECURE, TABLE_GLOBAL}) {
                db.execSQL(
                        "CREATE TABLE "
                                + table
                                + " ("
                                + "_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                + "name TEXT UNIQUE NOT NULL, "
                                + "value TEXT)");
                db.execSQL("CREATE UNIQUE INDEX idx_" + table + "_name ON " + table + "(name)");
            }

            // OEM defaults the apps expect to exist. Without a numeric
            // sync_bright_switcher_value the Setting app crashes on its
            // Display tab (Integer.parseInt(null) in DisplayPresenter).
            db.execSQL(
                    "INSERT OR IGNORE INTO "
                            + TABLE_SYSTEM
                            + "(name, value) VALUES ('sync_bright_switcher_value', '1')");
            // NetworkMgr's observer treats a missing ap_show as 0, even though
            // its initial read defaults to 1. Keep both paths consistent.
            db.execSQL(
                    "INSERT OR IGNORE INTO "
                            + TABLE_SYSTEM
                            + "(name, value) VALUES ('ap_show', '1')");
        }

        @Override
        public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
            if (oldVersion < 2) {
                db.execSQL(
                        "INSERT OR IGNORE INTO "
                                + TABLE_SYSTEM
                                + "(name, value) VALUES ('ap_show', '1')");
            }
        }
    }
}
