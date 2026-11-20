package com.voyemu.api;

import android.graphics.drawable.Drawable;
import android.graphics.drawable.LevelListDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Looper;
import android.text.method.PasswordTransformationMethod;
import android.text.method.SingleLineTransformationMethod;
import android.text.method.TransformationMethod;
import android.view.View;
import android.view.ViewGroup;
import android.widget.CompoundButton;
import android.widget.ImageView;
import android.widget.TextView;

import java.io.BufferedWriter;
import java.io.IOException;
import java.io.StringWriter;

public final class ViewDumpBridgeTest {
    static void check(boolean yes) {
        if (!yes) {
            throw new AssertionError();
        }
    }

    static String dump(Object view) throws Exception {
        StringWriter output = new StringWriter();
        BufferedWriter writer = new BufferedWriter(output);

        ViewDumpBridge.dumpViewProperties(null, view, writer);
        writer.flush();

        return output.toString();
    }

    static void redacted(TextView view) throws Exception {
        String result = dump(view);

        check(view.reads == 0 && view.layoutReads == 0);
        check(
                !result.contains("voyemu:text_layout_line_")
                        && !result.contains("voyemu:text_layout_text_length"));
        check(result.contains("voyemu:sensitive=4,true "));
        check(!result.contains("text:mText=") && !result.contains("PRIVATE_VALUE"));
        check(result.contains("voyemu:current_text_color_argb=10,4279383126 "));
    }

    static void failedWriter(TextView view) throws Exception {
        StringWriter output = new StringWriter();
        BufferedWriter writer = new BufferedWriter(output);
        long start = System.nanoTime();

        try {
            ViewDumpBridge.dumpViewProperties(null, view, writer);
            throw new AssertionError();
        } catch (IOException expected) {
            // The call must reject this input.
        }

        int posts = view.handler.posts, removals = view.handler.removals;

        for (int i = 0; i < 1000; i++) {
            try {
                ViewDumpBridge.dumpViewProperties(null, view, writer);
                throw new AssertionError();
            } catch (IOException expected) {
                // The call must reject this input.
            }
        }

        check(
                view.handler.posts == posts
                        && posts == 1
                        && removals == 1
                        && view.handler.removals == 1);
        check(System.nanoTime() - start < 2000000000L);
        check(view.reads == 0);
        view.handler.pending.run();
        check(view.reads == 0);
    }

    static void imageLevels() throws Exception {
        ImageView card = new ImageView();

        card.drawable = new LevelListDrawable();
        card.setImageLevel(1);
        String on = dump(card);
        check(
                on.contains("voyemu:drawable_status=9,available ")
                        && on.contains("voyemu:drawable_level=1,1 ")
                        && on.contains("isSelected()=4,true "));
        card.setImageLevel(0);
        String off = dump(card);
        check(off.contains("voyemu:drawable_level=1,0 "));
        // Only the live level differs; selected and every other property stay the same.
        check(on.replace("voyemu:drawable_level=1,1 ", "voyemu:drawable_level=1,0 ").equals(off));
        card.drawable = null;
        String absent = dump(card);
        check(
                absent.contains("voyemu:drawable_status=4,none ")
                        && !absent.contains("voyemu:drawable_level")
                        && !absent.contains("voyemu:drawable_state"));
        ImageView failing =
                new ImageView() {
                    @Override
                    public Drawable getDrawable() {
                        throw new IllegalStateException("app override");
                    }
                };
        String unavailable = dump(failing);
        check(
                unavailable.contains("voyemu:drawable_status=11,unavailable ")
                        && !unavailable.contains("voyemu:drawable_level")
                        && !unavailable.contains("voyemu:drawable_intrinsic_"));
    }

    static void stateSets() throws Exception {
        ImageView image = new ImageView();

        image.drawable = new StateListDrawable();
        String empty = dump(image);
        check(empty.contains("voyemu:drawable_state=0, "));
        image.drawable.setState(new int[] {16842913, 0, 16842910, 16842913});
        String state = dump(image);
        check(state.contains("voyemu:drawable_state=17,16842910,16842913 "));
        image.drawable.setState(new int[] {16842910, 16842913});
        check(dump(image).equals(state));
        image.drawable.setState(null);
        check(dump(image).contains("voyemu:drawable_state=11,unavailable "));
        image.drawable =
                new StateListDrawable() {
                    @Override
                    public int[] getState() {
                        throw new IllegalStateException("app override");
                    }
                };
        image.drawable.setLevel(2);
        String failed = dump(image);
        check(
                failed.contains("voyemu:drawable_state=11,unavailable ")
                        && failed.contains("voyemu:drawable_status=9,available ")
                        && failed.contains("voyemu:drawable_level=1,2 "));
    }

    static void backgrounds() throws Exception {
        View plain = new View();
        String none = dump(plain);

        check(
                none.contains("voyemu:background_status=4,none ")
                        && !none.contains("voyemu:background_level"));
        plain.background = new LevelListDrawable();
        plain.background.setLevel(3);
        plain.background.setState(new int[] {16842919});
        String level = dump(plain);
        check(
                level.contains("voyemu:background_status=9,available ")
                        && level.contains("voyemu:background_level=1,3 ")
                        && level.contains("voyemu:background_state=8,16842919 "));
        plain.background.setLevel(0);
        check(dump(plain).contains("voyemu:background_level=1,0 "));
        View failing =
                new View() {
                    @Override
                    public Drawable getBackground() {
                        throw new IllegalStateException("app override");
                    }
                };
        String unavailable = dump(failing);
        check(
                unavailable.contains("voyemu:background_status=11,unavailable ")
                        && !unavailable.contains("voyemu:background_level"));
        ImageView image = new ImageView();
        image.background = new StateListDrawable();
        String both = dump(image);
        check(
                both.contains("voyemu:drawable_status=9,available ")
                        && both.contains("voyemu:background_status=9,available "));
    }

    static void textCases() throws Exception {
        TextView plain = new TextView();
        String result = dump(plain);

        check(plain.reads == 1);
        check(result.contains("voyemu:sensitive=5,false "));
        String label = plain.label.toString().replace("\n", "\\n");
        check(result.contains("text:mText=" + label.length() + "," + label + " "));
        check(result.indexOf("voyemu:sensitive") < result.indexOf("text:mText"));
        check(result.contains("voyemu:resource_name=13,test:id/label "));
        check(
                result.contains("voyemu:text_layout_status=9,available ")
                        && result.contains("voyemu:text_layout_baseline=2,25 ")
                        && result.contains("voyemu:text_layout_font_style=1,0 ")
                        && result.contains("voyemu:text_layout_line_0="));
        TextView overflowing = new TextView();
        overflowing.textLayout = new android.text.Layout(overflowing.label);
        overflowing.textLayout.count = 33;
        String overflow = dump(overflowing);
        check(
                overflow.contains("voyemu:text_layout_status=8,overflow ")
                        && !overflow.contains("voyemu:text_layout_line_"));
        check(result.contains("voyemu:current_text_color_argb=10,4279383126 "));
        check(result.contains("voyemu:text_size_px=4,23.5 "));
        check(result.contains("voyemu:text_scale_x=4,1.25 "));
        check(result.contains("voyemu:letter_spacing=5,0.125 "));
        check(
                result.contains("voyemu:measured_width=3,100 ")
                        && result.contains("voyemu:min_width=2,17 ")
                        && result.contains("voyemu:scroll_x=2,-5 ")
                        && result.contains("voyemu:padding_bottom=1,4 "));
    }

    static void layoutCases() throws Exception {
        View margins = new View();

        margins.layout = new ViewGroup.MarginLayoutParams();
        String layout = dump(margins);
        check(layout.contains("voyemu:layout_params_type=6,margin "));
        check(
                layout.contains("voyemu:layout_width=2,-1 ")
                        && layout.contains("voyemu:layout_height=2,-2 ")
                        && layout.contains("voyemu:margin_left=1,7 ")
                        && layout.contains("voyemu:margin_bottom=2,10 "));
        View basic = new View();
        basic.layout = new ViewGroup.LayoutParams();
        check(!dump(basic).contains("voyemu:margin_"));
        check(!dump(new View()).contains("voyemu:layout_width"));
    }

    static void imageCases() throws Exception {
        ImageView image = new ImageView();
        String icon = dump(image);

        check(icon.contains("voyemu:image_scale_type=6,MATRIX "));
        String matrix = "1.0,0.1,20.0,0.2,2.0,30.0,0.0,0.0,1.0";
        check(icon.contains("voyemu:image_matrix=" + matrix.length() + "," + matrix + " "));
        check(
                icon.contains("voyemu:drawable_intrinsic_width=2,-1 ")
                        && icon.contains("voyemu:drawable_bounds_right=2,24 "));
        image.drawable = null;
        check(!dump(image).contains("voyemu:drawable_intrinsic_"));
        imageLevels();
        stateSets();
        backgrounds();
    }

    static void privacyCases() throws Exception {
        for (int type : new int[] {129, 225, 145, 18}) {
            TextView password = new TextView();
            password.inputType = type;
            redacted(password);
        }

        TextView password = new TextView();
        password.transformation = new PasswordTransformationMethod();
        redacted(password);

        for (int mode = 0; mode < 3; mode++) {
            TextView editor = new TextView();
            editor.enabled = false;
            editor.editable = mode == 0;
            editor.key = mode == 1;
            editor.editor = mode == 2;
            redacted(editor);
        }

        TextView custom = new TextView();
        custom.transformation = new TransformationMethod() {};
        redacted(custom);
        TextView single = new TextView();
        single.transformation = new SingleLineTransformationMethod();
        check(dump(single).contains("text:mText="));
        check(dump(new CompoundButton()).contains("isChecked()=4,true "));
        check(!dump(new View()).contains("text:mText="));
        TextView failed = new TextView();
        failed.fail = true;

        try {
            dump(failed);
            throw new AssertionError();
        } catch (IOException expected) {
            check(!expected.toString().contains("PRIVATE_VALUE"));
            check(failed.reads == 0);
        }

        View unattached = new View();
        unattached.handler = null;

        try {
            dump(unattached);
            throw new AssertionError();
        } catch (IOException expected) {
            // The call must reject this input.
        }
    }

    static void threadCases() throws Exception {
        Looper.current = null;
        TextView stuck = new TextView();
        stuck.handler.run = false;
        failedWriter(stuck);
        TextView rejected = new TextView();
        rejected.handler.accept = false;
        failedWriter(rejected);
        TextView gradual = new TextView();
        gradual.handler.delayMillis = 75;
        StringWriter totalOutput = new StringWriter();
        BufferedWriter totalWriter = new BufferedWriter(totalOutput);
        long totalStart = System.nanoTime();
        boolean expired = false;

        for (int i = 0; i < 30; i++) {
            try {
                ViewDumpBridge.dumpViewProperties(null, gradual, totalWriter);
            } catch (IOException expected) {
                expired = true;
                break;
            }
        }

        check(
                expired
                        && gradual.handler.posts <= 15
                        && System.nanoTime() - totalStart < 2000000000L);
        int totalPosts = gradual.handler.posts;

        for (int i = 0; i < 1000; i++) {
            try {
                ViewDumpBridge.dumpViewProperties(null, gradual, totalWriter);
                throw new AssertionError();
            } catch (IOException expected) {
                // The call must reject this input.
            }
        }

        check(gradual.handler.posts == totalPosts);
        TextView posted = new TextView();
        dump(posted);
        check(posted.handler.posts == 1);
        View refused = new View();
        refused.handler.accept = false;

        try {
            dump(refused);
            throw new AssertionError();
        } catch (IOException expected) {
            // The call must reject this input.
        }

        TextView timeout = new TextView();
        timeout.handler.run = false;

        try {
            dump(timeout);
            throw new AssertionError();
        } catch (IOException expected) {
            // The call must reject this input.
        }

        timeout.handler.pending.run();
        check(timeout.reads == 0);
        TextView interrupted = new TextView();
        interrupted.handler.run = false;
        Thread.currentThread().interrupt();

        try {
            dump(interrupted);
            throw new AssertionError();
        } catch (IOException expected) {
            check(Thread.currentThread().isInterrupted());
        } finally {
            Thread.interrupted();
        }

        interrupted.handler.pending.run();
        check(interrupted.reads == 0);
        Looper.current = Looper.MAIN;
        TextView huge = new TextView();
        huge.label = new String(new char[4097]);

        try {
            dump(huge);
            throw new AssertionError();
        } catch (IOException expected) {
            // The call must reject this input.
        }
    }

    public static void main(String[] args) throws Exception {
        textCases();
        layoutCases();
        imageCases();
        privacyCases();
        threadCases();
        System.out.println(
                "ViewDumpBridge host privacy/thread/framing cases passed; runtime unverified");
    }
}
