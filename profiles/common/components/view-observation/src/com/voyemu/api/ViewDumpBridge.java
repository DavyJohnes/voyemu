package com.voyemu.api;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.text.method.PasswordTransformationMethod;
import android.text.method.TransformationMethod;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CompoundButton;
import android.widget.ImageView;
import android.widget.TextView;

import java.io.BufferedWriter;
import java.io.IOException;
import java.util.Arrays;
import java.util.Map;
import java.util.WeakHashMap;
import java.util.concurrent.Callable;
import java.util.concurrent.FutureTask;
import java.util.concurrent.TimeUnit;

/** Read-only replacement for API28 ViewDebug's three-argument property writer. */
public final class ViewDumpBridge {
    private static final int MAX_TEXT = 4096;
    private static final int MAX_SAMPLE = 16384;
    private static final int MAX_STATES = 64;

    private ViewDumpBridge() {}

    // ViewDebug can catch IOException and continue siblings: one failed writer stays failed.
    private static final Map<BufferedWriter, Budget> BUDGETS =
            new WeakHashMap<BufferedWriter, Budget>();
    private static final long REQUEST_NANOS = TimeUnit.SECONDS.toNanos(1);
    private static final long NODE_NANOS = TimeUnit.MILLISECONDS.toNanos(200);

    private static final class Budget {
        final long started = System.nanoTime();
        boolean failed;

        long remaining() {
            return REQUEST_NANOS - (System.nanoTime() - started);
        }
    }

    private static Budget budget(BufferedWriter writer) {
        synchronized (BUDGETS) {
            Budget budget = BUDGETS.get(writer);

            if (budget == null) {
                budget = new Budget();
                BUDGETS.put(writer, budget);
            }

            return budget;
        }
    }

    public static void dumpViewProperties(Context context, Object object, BufferedWriter out)
            throws IOException {
        Budget budget = budget(out);

        synchronized (budget) {
            if (budget.failed || budget.remaining() <= 0) {
                budget.failed = true;
                throw new IOException("VIEW_BRIDGE_REQUEST");
            }

            if (!(object instanceof View)) {
                budget.failed = true;
                throw new IOException("VIEW_BRIDGE_TYPE");
            }

            final View view = (View) object;
            Handler handler = view.getHandler();

            if (handler == null) {
                budget.failed = true;
                throw new IOException("VIEW_BRIDGE_UNATTACHED");
            }

            final FutureTask<String> task =
                    new FutureTask<String>(
                            new Callable<String>() {
                                @Override
                                public String call() {
                                    return sample(view);
                                }
                            });

            try {
                if (Looper.myLooper() == handler.getLooper()) {
                    task.run();
                } else if (!handler.post(task)) {
                    throw new IOException("VIEW_BRIDGE_POST");
                }

                long remaining = budget.remaining();

                if (remaining <= 0) {
                    throw new IOException("VIEW_BRIDGE_REQUEST");
                }

                String properties = task.get(Math.min(NODE_NANOS, remaining), TimeUnit.NANOSECONDS);

                if (budget.remaining() <= 0) {
                    throw new IOException("VIEW_BRIDGE_REQUEST");
                }

                out.write(properties);
            } catch (InterruptedException error) {
                budget.failed = true;
                Thread.currentThread().interrupt();
                throw new IOException("VIEW_BRIDGE_INTERRUPTED");
            } catch (IOException error) {
                budget.failed = true;
                throw error;
            } catch (Exception error) {
                // checkstyle-allow IllegalCatch: any sample failure fails this writer, not the app.
                budget.failed = true;
                throw new IOException("VIEW_BRIDGE_SAMPLE");
            } finally {
                task.cancel(false);
                handler.removeCallbacks(task);
            }
        }
    }

    private static boolean passwordType(int inputType) {
        int kind = inputType & (InputType.TYPE_MASK_CLASS | InputType.TYPE_MASK_VARIATION);

        return kind == (InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD)
                || kind == (InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD)
                || kind
                        == (InputType.TYPE_CLASS_TEXT
                                | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD)
                || kind == (InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_VARIATION_PASSWORD);
    }

    private static String sample(View view) {
        StringBuilder result = new StringBuilder();
        TextView text = view instanceof TextView ? (TextView) view : null;
        boolean[] privacy = privacy(text);
        boolean password = privacy[0], editable = privacy[1], sensitive = privacy[2];

        entry(result, "voyemu:bridge_version", "1");
        entry(result, "voyemu:sensitive", String.valueOf(sensitive));
        entry(result, "voyemu:editable", String.valueOf(editable));
        entry(result, "voyemu:password", String.valueOf(password));
        entry(
                result,
                "voyemu:input_type",
                text == null ? "unknown" : String.valueOf(text.getInputType()));
        entry(
                result,
                "voyemu:text_status",
                text == null ? "none" : sensitive ? "redacted" : "available");
        resource(result, view);
        geometry(result, view);
        drawing(result, view);

        if (text != null) {
            entry(
                    result,
                    "voyemu:current_text_color_argb",
                    String.valueOf(text.getCurrentTextColor() & 0xffffffffL));
            entry(result, "voyemu:text_size_px", finite(text.getTextSize()));
            entry(result, "voyemu:text_scale_x", finite(text.getTextScaleX()));
            entry(result, "voyemu:letter_spacing", finite(text.getLetterSpacing()));
        }

        textLayout(result, text, sensitive);

        if (view instanceof ImageView) {
            image(result, (ImageView) view);
        }

        background(result, view);

        if (text != null && !sensitive) {
            CharSequence value = text.getText();

            if (value != null) {
                if (value.length() > MAX_TEXT) {
                    throw new IllegalStateException("VIEW_BRIDGE_TEXT_LIMIT");
                }

                entry(result, "text:mText", value.toString());
            }
        }

        if (result.length() > MAX_SAMPLE) {
            throw new IllegalStateException("VIEW_BRIDGE_SAMPLE_LIMIT");
        }

        return result.toString();
    }

    /** Returns password, editable and sensitive verdicts; a view without text has none. */
    private static boolean[] privacy(TextView text) {
        if (text == null) {
            return new boolean[] {false, false, false};
        }

        TransformationMethod transformation = text.getTransformationMethod();
        boolean password =
                passwordType(text.getInputType())
                        || transformation instanceof PasswordTransformationMethod;
        // Disabled editors are still sensitive; do not use isTextEditable/isEnabled as proof.
        boolean editable =
                text.getEditableText() != null
                        || text.getKeyListener() != null
                        || text.onCheckIsTextEditor();
        String transform = transformation == null ? "" : transformation.getClass().getName();
        boolean knownTransform =
                transformation == null
                        || transform.equals("android.text.method.SingleLineTransformationMethod")
                        || transform.equals("android.text.method.AllCapsTransformationMethod");

        return new boolean[] {password, editable, password || editable || !knownTransform};
    }

    private static void resource(StringBuilder result, View view) {
        String resource = "unknown", shortResource = "NO_ID";

        if (view.getId() != View.NO_ID) {
            try {
                resource = view.getResources().getResourceName(view.getId());
                shortResource =
                        view.getResources().getResourceTypeName(view.getId())
                                + "/"
                                + view.getResources().getResourceEntryName(view.getId());
            } catch (Resources.NotFoundException missing) {
                /* Resource absence remains unknown. */
            }
        }

        entry(result, "voyemu:resource_name", resource);
        entry(result, "mID", shortResource);
    }

    private static void geometry(StringBuilder result, View view) {
        entry(result, "layout:mLeft", String.valueOf(view.getLeft()));
        entry(result, "layout:mTop", String.valueOf(view.getTop()));
        entry(result, "layout:mRight", String.valueOf(view.getRight()));
        entry(result, "layout:mBottom", String.valueOf(view.getBottom()));
        entry(result, "voyemu:measured_width", String.valueOf(view.getMeasuredWidth()));
        entry(result, "voyemu:measured_height", String.valueOf(view.getMeasuredHeight()));
        entry(result, "voyemu:min_width", String.valueOf(view.getMinimumWidth()));
        entry(result, "voyemu:min_height", String.valueOf(view.getMinimumHeight()));
        entry(result, "voyemu:padding_left", String.valueOf(view.getPaddingLeft()));
        entry(result, "voyemu:padding_top", String.valueOf(view.getPaddingTop()));
        entry(result, "voyemu:padding_right", String.valueOf(view.getPaddingRight()));
        entry(result, "voyemu:padding_bottom", String.valueOf(view.getPaddingBottom()));
        entry(result, "voyemu:scroll_x", String.valueOf(view.getScrollX()));
        entry(result, "voyemu:scroll_y", String.valueOf(view.getScrollY()));
        ViewGroup.LayoutParams layout = view.getLayoutParams();
        entry(
                result,
                "voyemu:layout_params_type",
                layout == null
                        ? "none"
                        : layout instanceof ViewGroup.MarginLayoutParams ? "margin" : "basic");

        if (layout != null) {
            // Preserve MATCH_PARENT=-1 and WRAP_CONTENT=-2, not fabricated pixel bounds.
            entry(result, "voyemu:layout_width", String.valueOf(layout.width));
            entry(result, "voyemu:layout_height", String.valueOf(layout.height));

            if (layout instanceof ViewGroup.MarginLayoutParams) {
                ViewGroup.MarginLayoutParams margin = (ViewGroup.MarginLayoutParams) layout;
                entry(result, "voyemu:margin_left", String.valueOf(margin.leftMargin));
                entry(result, "voyemu:margin_top", String.valueOf(margin.topMargin));
                entry(result, "voyemu:margin_right", String.valueOf(margin.rightMargin));
                entry(result, "voyemu:margin_bottom", String.valueOf(margin.bottomMargin));
            }
        }
    }

    private static void drawing(StringBuilder result, View view) {
        int[] position = new int[2];

        view.getLocationOnScreen(position);
        entry(result, "layout:getLocationOnScreen_x()", String.valueOf(position[0]));
        entry(result, "layout:getLocationOnScreen_y()", String.valueOf(position[1]));
        entry(result, "drawing:getScaleX()", String.valueOf(view.getScaleX()));
        entry(result, "drawing:getScaleY()", String.valueOf(view.getScaleY()));
        entry(result, "drawing:getRotation()", String.valueOf(view.getRotation()));
        entry(result, "drawing:getRotationX()", String.valueOf(view.getRotationX()));
        entry(result, "drawing:getRotationY()", String.valueOf(view.getRotationY()));
        entry(result, "drawing:getAlpha()", String.valueOf(view.getAlpha()));
        entry(result, "getVisibility()", String.valueOf(view.getVisibility()));
        entry(result, "isEnabled()", String.valueOf(view.isEnabled()));
        entry(result, "isClickable()", String.valueOf(view.isClickable()));
        entry(result, "isLongClickable()", String.valueOf(view.isLongClickable()));
        entry(result, "isFocusable()", String.valueOf(view.isFocusable()));
        entry(result, "isFocused()", String.valueOf(view.isFocused()));
        entry(result, "isSelected()", String.valueOf(view.isSelected()));

        if (view instanceof CompoundButton) {
            entry(result, "isChecked()", String.valueOf(((CompoundButton) view).isChecked()));
        }
    }

    private static void image(StringBuilder result, ImageView image) {
        ImageView.ScaleType scale = image.getScaleType();

        entry(
                result,
                "voyemu:image_scale_type",
                scale == null ? "unknown" : ((Enum<?>) scale).name());
        float[] matrix = new float[9];
        image.getImageMatrix().getValues(matrix);
        StringBuilder values = new StringBuilder();

        for (int i = 0; i < matrix.length; i++) {
            if (i != 0) {
                values.append(',');
            }

            values.append(finite(matrix[i]));
        }

        entry(result, "voyemu:image_matrix", values.toString());
        StringBuilder read = new StringBuilder();
        Drawable drawable;

        try {
            drawable = image.getDrawable();

            if (drawable != null) {
                entry(
                        read,
                        "voyemu:drawable_intrinsic_width",
                        String.valueOf(drawable.getIntrinsicWidth()));
                entry(
                        read,
                        "voyemu:drawable_intrinsic_height",
                        String.valueOf(drawable.getIntrinsicHeight()));
                Rect bounds = drawable.getBounds();
                entry(read, "voyemu:drawable_bounds_left", String.valueOf(bounds.left));
                entry(read, "voyemu:drawable_bounds_top", String.valueOf(bounds.top));
                entry(read, "voyemu:drawable_bounds_right", String.valueOf(bounds.right));
                entry(read, "voyemu:drawable_bounds_bottom", String.valueOf(bounds.bottom));
                entry(read, "voyemu:drawable_level", String.valueOf(drawable.getLevel()));
            }
        } catch (RuntimeException unavailable) {
            // checkstyle-allow IllegalCatch: an app getDrawable or Drawable override may throw.
            entry(result, "voyemu:drawable_status", "unavailable");

            return;
        }

        entry(result, "voyemu:drawable_status", drawable == null ? "none" : "available");
        result.append(read);

        if (drawable != null) {
            drawableState(result, "voyemu:drawable_state", drawable);
        }
    }

    /** The live View.getBackground(): presence, level and state set, read like an image. */
    private static void background(StringBuilder result, View view) {
        Drawable background;
        int level;

        try {
            background = view.getBackground();
            level = background == null ? 0 : background.getLevel();
        } catch (RuntimeException unavailable) {
            // checkstyle-allow IllegalCatch: an app getBackground override may throw.
            entry(result, "voyemu:background_status", "unavailable");

            return;
        }

        entry(result, "voyemu:background_status", background == null ? "none" : "available");

        if (background != null) {
            entry(result, "voyemu:background_level", String.valueOf(level));
            drawableState(result, "voyemu:background_state", background);
        }
    }

    /** Ascending distinct nonzero state attributes; an app getState override may fail alone. */
    private static void drawableState(StringBuilder result, String key, Drawable drawable) {
        int[] state;

        try {
            state = drawable.getState();
        } catch (RuntimeException unavailable) {
            // checkstyle-allow IllegalCatch: an app Drawable.getState override may throw.
            state = null;
        }

        if (state == null || state.length > MAX_STATES) {
            entry(result, key, "unavailable");

            return;
        }

        int[] sorted = state.clone();
        Arrays.sort(sorted);
        StringBuilder value = new StringBuilder();
        int previous = 0;

        for (int attribute : sorted) {
            if (attribute == 0 || (value.length() != 0 && attribute == previous)) {
                continue;
            }

            if (value.length() != 0) {
                value.append(',');
            }

            value.append(attribute);
            previous = attribute;
        }

        entry(result, key, value.toString());
    }

    private static void textLayout(StringBuilder output, TextView text, boolean sensitive) {
        if (text == null) {
            entry(output, "voyemu:text_layout_status", "none");

            return;
        }

        if (sensitive) {
            entry(output, "voyemu:text_layout_status", "redacted");

            return;
        }

        android.text.Layout layout = text.getLayout();

        if (layout == null) {
            entry(output, "voyemu:text_layout_status", "unavailable");

            return;
        }

        int count = layout.getLineCount();
        CharSequence content = layout.getText();

        if (count < 1 || count > 32 || content == null || content.length() > MAX_TEXT) {
            entry(output, "voyemu:text_layout_status", "overflow");

            return;
        }

        android.graphics.Paint paint = text.getPaint();
        android.graphics.Typeface face = paint.getTypeface();
        entry(output, "voyemu:text_layout_version", "1");
        entry(output, "voyemu:text_layout_status", "available");
        entry(output, "voyemu:text_layout_text_length", String.valueOf(content.length()));
        entry(
                output,
                "voyemu:text_layout_spanned",
                String.valueOf(content instanceof android.text.Spanned));
        entry(output, "voyemu:text_layout_width", String.valueOf(layout.getWidth()));
        entry(output, "voyemu:text_layout_height", String.valueOf(layout.getHeight()));
        entry(output, "voyemu:text_layout_baseline", String.valueOf(text.getBaseline()));
        entry(output, "voyemu:text_layout_gravity", String.valueOf(text.getGravity()));
        entry(
                output,
                "voyemu:text_layout_padding_left",
                String.valueOf(text.getCompoundPaddingLeft()));
        entry(
                output,
                "voyemu:text_layout_padding_right",
                String.valueOf(text.getCompoundPaddingRight()));
        entry(
                output,
                "voyemu:text_layout_padding_top",
                String.valueOf(text.getExtendedPaddingTop()));
        entry(
                output,
                "voyemu:text_layout_padding_bottom",
                String.valueOf(text.getExtendedPaddingBottom()));
        entry(
                output,
                "voyemu:text_layout_include_font_padding",
                String.valueOf(text.getIncludeFontPadding()));
        entry(
                output,
                "voyemu:text_layout_font_style",
                face == null ? "unknown" : String.valueOf(face.getStyle()));
        entry(output, "voyemu:text_layout_fake_bold", String.valueOf(paint.isFakeBoldText()));
        entry(output, "voyemu:text_layout_skew_x", finite(paint.getTextSkewX()));
        entry(output, "voyemu:text_layout_paint_flags", String.valueOf(paint.getFlags()));
        entry(output, "voyemu:text_layout_break_strategy", String.valueOf(text.getBreakStrategy()));
        entry(
                output,
                "voyemu:text_layout_hyphenation",
                String.valueOf(text.getHyphenationFrequency()));
        entry(output, "voyemu:text_layout_alignment", layout.getAlignment().name());
        entry(output, "voyemu:text_layout_line_count", String.valueOf(count));

        for (int i = 0; i < count; i++) {
            String value =
                    layout.getLineStart(i)
                            + ","
                            + layout.getLineEnd(i)
                            + ","
                            + finite(layout.getLineLeft(i))
                            + ","
                            + finite(layout.getLineRight(i))
                            + ","
                            + layout.getLineTop(i)
                            + ","
                            + layout.getLineBaseline(i)
                            + ","
                            + layout.getLineBottom(i)
                            + ","
                            + layout.getEllipsisStart(i)
                            + ","
                            + layout.getEllipsisCount(i)
                            + ","
                            + layout.getParagraphDirection(i);
            entry(output, "voyemu:text_layout_line_" + i, value);
        }
    }

    private static String finite(float value) {
        return Float.isNaN(value) || Float.isInfinite(value) ? "unknown" : String.valueOf(value);
    }

    private static void entry(StringBuilder output, String key, String value) {
        // Preserve legacy UTF16 length-prefixed framing, including its newline escaping.
        String escaped = value.replace("\n", "\\n").replace("\r", "\\r");

        output.append(key)
                .append('=')
                .append(escaped.length())
                .append(',')
                .append(escaped)
                .append(' ');
    }
}
