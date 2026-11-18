package android.database;

public interface Cursor {
    int FIELD_TYPE_NULL = 0,
            FIELD_TYPE_INTEGER = 1,
            FIELD_TYPE_FLOAT = 2,
            FIELD_TYPE_STRING = 3,
            FIELD_TYPE_BLOB = 4;

    boolean moveToNext();

    int getColumnIndexOrThrow(String s);

    int getType(int i);

    long getLong(int i);

    double getDouble(int i);

    byte[] getBlob(int i);

    String getString(int i);

    void close();
}
