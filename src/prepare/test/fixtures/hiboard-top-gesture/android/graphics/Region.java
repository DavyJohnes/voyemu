package android.graphics;

public class Region {
    public enum Op {
        INTERSECT
    }

    public Rect frame = new Rect();

    public boolean set(Rect r) {
        frame = new Rect(r.left, r.top, r.right, r.bottom);

        return frame.left < frame.right && frame.top < frame.bottom;
    }

    public boolean op(Rect r, Op op) {
        frame =
                new Rect(
                        Math.max(frame.left, r.left),
                        Math.max(frame.top, r.top),
                        Math.min(frame.right, r.right),
                        Math.min(frame.bottom, r.bottom));

        return true;
    }

    public boolean contains(int x, int y) {
        return x >= frame.left && y >= frame.top && x < frame.right && y < frame.bottom;
    }
}
