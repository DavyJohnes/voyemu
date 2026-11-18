package android.database;

public class MatrixCursor implements Cursor {
    public final String[] columns;
    public final java.util.List<Object[]> rows = new java.util.ArrayList<>();
    public boolean closed;
    private int pos = -1;

    public MatrixCursor(String[] c) {
        columns = c;
    }

    public void addRow(Object[] row) {
        rows.add(row);
    }

    public boolean moveToNext() {
        return ++pos < rows.size();
    }

    public int getColumnIndexOrThrow(String s) {
        for (int i = 0; i < columns.length; i++) {
            if (columns[i].equals(s)) {
                return i;
            }
        }

        throw new IllegalArgumentException(s);
    }

    private Object value(int i) {
        return rows.get(pos)[i];
    }

    public int getType(int i) {
        Object o = value(i);

        return o == null
                ? 0
                : o instanceof byte[]
                        ? 4
                        : o instanceof Float || o instanceof Double
                                ? 2
                                : o instanceof Number ? 1 : 3;
    }

    public long getLong(int i) {
        return value(i) == null ? 0 : ((Number) value(i)).longValue();
    }

    public double getDouble(int i) {
        return ((Number) value(i)).doubleValue();
    }

    public byte[] getBlob(int i) {
        return (byte[]) value(i);
    }

    public String getString(int i) {
        return (String) value(i);
    }

    public void close() {
        closed = true;
    }
}
