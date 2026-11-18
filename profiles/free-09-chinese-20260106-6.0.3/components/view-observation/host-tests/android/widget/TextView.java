package android.widget;

import android.text.method.TransformationMethod;
import android.view.View;

public class TextView extends View {
    public int inputType, reads, layoutReads;
    public android.text.Layout textLayout;
    public boolean missingLayout;

    public android.text.Layout getLayout() {
        layoutReads++;

        return missingLayout
                ? null
                : textLayout == null ? new android.text.Layout(label) : textLayout;
    }

    public android.graphics.Paint getPaint() {
        return new android.graphics.Paint();
    }

    public int getBaseline() {
        return 25;
    }

    public int getGravity() {
        return 17;
    }

    public int getCompoundPaddingLeft() {
        return 1;
    }

    public int getCompoundPaddingRight() {
        return 3;
    }

    public int getExtendedPaddingTop() {
        return 2;
    }

    public int getExtendedPaddingBottom() {
        return 4;
    }

    public boolean getIncludeFontPadding() {
        return true;
    }

    public int getBreakStrategy() {
        return 0;
    }

    public int getHyphenationFrequency() {
        return 0;
    }

    public boolean editable, editor, key, fail;
    public TransformationMethod transformation;
    public CharSequence label = "Привет, 世界 😀\nNext";

    public int getCurrentTextColor() {
        return 0xff123456;
    }

    public float getTextSize() {
        return 23.5f;
    }

    public float getTextScaleX() {
        return 1.25f;
    }

    public float getLetterSpacing() {
        return 0.125f;
    }

    public int getInputType() {
        if (fail) {
            throw new IllegalStateException("PRIVATE_VALUE");
        }

        return inputType;
    }

    public TransformationMethod getTransformationMethod() {
        return transformation;
    }

    public CharSequence getEditableText() {
        return editable ? "PRIVATE_VALUE" : null;
    }

    public Object getKeyListener() {
        return key ? this : null;
    }

    public boolean onCheckIsTextEditor() {
        return editor;
    }

    public CharSequence getText() {
        reads++;

        return label;
    }
}
