import android.content.ContentResolver;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;

import com.voyemu.output.NxpMusicMedia;

public final class NxpMusicMediaTest {
    static void need(boolean b, String m) {
        if (!b) {
            throw new AssertionError(m);
        }
    }

    static final String[] PROJECTION = {
        "album",
        "artist",
        "_data",
        "title",
        "mime_type",
        "_id",
        "duration",
        "album_id",
        "artist_id",
        "_size",
        "_display_name",
        "date_modified",
        "title_pinyin",
        "album_pinyin",
        "artist_pinyin",
        "album_art",
        "storage_uuid"
    };

    public static void main(String[] args) {
        final MatrixCursor[] used = {null};
        final int[] calls = {0};
        ContentResolver provider =
                new ContentResolver() {
                    public Cursor query(
                            Uri uri, String[] p, String selection, String[] values, String order) {
                        calls[0]++;
                        need(
                                uri.toString().equals("content://media/external/audio/media"),
                                "volume remapped");
                        need(
                                "_id > ?".equals(selection)
                                        && values[0].equals("0")
                                        && order.equals("title"),
                                "query semantics changed");

                        for (String c : p) {
                            if (c.endsWith("_pinyin")
                                    || c.equals("album_art")
                                    || c.equals("storage_uuid")) {
                                throw new IllegalArgumentException("no such column: " + c);
                            }
                        }

                        MatrixCursor c = new MatrixCursor(p);

                        for (int id = 1; id <= 2; id++) {
                            Object[] row = new Object[p.length];

                            for (int i = 0; i < p.length; i++) {
                                String n = p[i];
                                row[i] =
                                        n.equals("_id")
                                                ? (long) id
                                                : n.equals("duration")
                                                        ? 4000L
                                                        : n.equals("_data")
                                                                ? "/storage/emulated/0/Music/track"
                                                                        + id
                                                                        + ".wav"
                                                                : n.equals("title")
                                                                        ? "track" + id
                                                                        : n.equals("album")
                                                                                ? "album"
                                                                                : n.equals("artist")
                                                                                        ? "artist"
                                                                                        : n.equals(
                                                                                                        "_size")
                                                                                                ? 705644L
                                                                                                : "metadata";
                            }

                            c.addRow(row);
                        }

                        used[0] = c;

                        return c;
                    }
                };
        Uri uri = Uri.parse("content://media/external/audio/media");
        boolean red = false;

        try {
            provider.query(uri, PROJECTION, "_id > ?", new String[] {"0"}, "title");
        } catch (IllegalArgumentException e) {
            red = e.getMessage().contains("no such column");
        }

        need(red, "unadapted OEM query should fail");
        Cursor c =
                NxpMusicMedia.query(
                        provider, uri, PROJECTION, "_id > ?", new String[] {"0"}, "title");
        need(used[0].closed, "provider cursor leaked");

        for (int id = 1; id <= 2; id++) {
            need(c.moveToNext(), "track dropped");
            need(c.getLong(c.getColumnIndexOrThrow("_id")) == id, "track identity changed");
            need(c.getLong(c.getColumnIndexOrThrow("duration")) == 4000, "duration changed");
            need(
                    c.getString(c.getColumnIndexOrThrow("title_pinyin")).equals("track" + id),
                    "name fallback");
            need(
                    c.getType(c.getColumnIndexOrThrow("album_art")) == 0
                            && c.getType(c.getColumnIndexOrThrow("storage_uuid")) == 0,
                    "invented OEM metadata");
        }

        need(!c.moveToNext(), "invented track");
        ContentResolver absent =
                new ContentResolver() {
                    public Cursor query(Uri u, String[] p, String s, String[] a, String o) {
                        return null;
                    }
                };
        need(
                NxpMusicMedia.query(absent, uri, PROJECTION, null, null, null) == null,
                "provider unavailable masked");

        for (String volume : new String[] {"internal", "external"}) {
            for (String kind : new String[] {"albums", "artists"}) {
                final Uri target = Uri.parse("content://media/" + volume + "/audio/" + kind);
                String[] projection =
                        kind.equals("albums")
                                ? new String[] {"_id", "album", "album_art", "album_pinyin"}
                                : new String[] {"_id", "artist", "artist_pinyin"};
                ContentResolver metadata =
                        new ContentResolver() {
                            public Cursor query(
                                    Uri u, String[] cols, String sel, String[] vals, String sort) {
                                need(u == target, "metadata volume changed");
                                MatrixCursor result = new MatrixCursor(cols);
                                Object[] row = new Object[cols.length];

                                for (int i = 0; i < cols.length; i++) {
                                    need(
                                            !cols[i].endsWith("_pinyin"),
                                            "OEM metadata column leaked");
                                    row[i] =
                                            cols[i].equals("_id")
                                                    ? 7L
                                                    : cols[i].equals("album_art")
                                                            ? "/actual/album-art.jpg"
                                                            : null;
                                }

                                result.addRow(row);

                                return result;
                            }
                        };
                Cursor result = NxpMusicMedia.query(metadata, target, projection, null, null, null);
                need(result.moveToNext(), "metadata missing");
                need(
                        result.getLong(result.getColumnIndexOrThrow("_id")) == 7,
                        "metadata id changed");
                need(
                        result.getType(
                                        result.getColumnIndexOrThrow(
                                                kind.equals("albums")
                                                        ? "album_pinyin"
                                                        : "artist_pinyin"))
                                == 0,
                        "null metadata invented");

                if (kind.equals("albums")) {
                    need(
                            result.getString(result.getColumnIndexOrThrow("album_art"))
                                    .equals("/actual/album-art.jpg"),
                            "real art lost");
                }
            }
        }

        c = NxpMusicMedia.query(provider, uri, PROJECTION, "_id > ?", new String[] {"0"}, "title");

        for (int id = 1; id <= 2; id++) {
            need(c.moveToNext(), "track missing during path check");
            need(
                    c.getString(c.getColumnIndexOrThrow("_data"))
                            .equals("/storage/emulated/0/Music/track" + id + ".wav"),
                    "track path changed");
        }

        System.out.println(
                "PASS: unadapted query RED; 2 provider fixture rows, metadata/identity/path/volume"
                        + " preserved; no cursor leak");
    }
}
