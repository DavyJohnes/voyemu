package android.text;

public class Layout {
    public enum Alignment {
        ALIGN_NORMAL,
        ALIGN_CENTER,
        ALIGN_OPPOSITE
    }

    public CharSequence text;
    public int count = 1;

    public Layout(CharSequence text) {
        this.text = text;
    }

    public int getLineCount() {
        return count;
    }

    public CharSequence getText() {
        return text;
    }

    public int getWidth() {
        return 96;
    }

    public int getHeight() {
        return count * 30;
    }

    public Alignment getAlignment() {
        return Alignment.ALIGN_NORMAL;
    }

    public int getLineStart(int i) {
        return 0;
    }

    public int getLineEnd(int i) {
        return text.length();
    }

    public float getLineLeft(int i) {
        return 0;
    }

    public float getLineRight(int i) {
        return 90;
    }

    public int getLineTop(int i) {
        return i * 30;
    }

    public int getLineBaseline(int i) {
        return i * 30 + 22;
    }

    public int getLineBottom(int i) {
        return (i + 1) * 30;
    }

    public int getEllipsisStart(int i) {
        return 0;
    }

    public int getEllipsisCount(int i) {
        return 0;
    }

    public int getParagraphDirection(int i) {
        return 1;
    }
}
