package com.voyemu.output;

import android.content.Context;
import android.os.Environment;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.widget.TextView;

import java.lang.reflect.InvocationTargetException;
import java.util.List;

/** Exposes API28 shared storage through the original NXP playlist controller. */
public final class NxpMusicStorage {
    private NxpMusicStorage() {}

    public static void attach(final View root) {
        if (!Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState())) {
            return;
        }

        final Context context = root.getContext();
        final int tabId =
                context.getResources().getIdentifier("tv_tab_my", "id", context.getPackageName());
        final class Entry
                implements ViewTreeObserver.OnGlobalLayoutListener,
                        View.OnAttachStateChangeListener {
            private void remove() {
                if (root.getViewTreeObserver().isAlive()) {
                    root.getViewTreeObserver().removeOnGlobalLayoutListener(this);
                }

                root.removeOnAttachStateChangeListener(this);
            }

            @Override
            public void onViewAttachedToWindow(View view) {}

            @Override
            public void onViewDetachedFromWindow(View view) {
                remove();
            }

            @Override
            public void onGlobalLayout() {
                final View tab = root.findViewById(tabId);

                if (tab == null || tab.getWidth() == 0) {
                    return;
                }

                remove();

                if (!Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState())) {
                    return;
                }

                final TextView entry = new TextView(context);
                entry.setText("External storage");
                entry.setContentDescription(
                        "External storage: "
                                + Environment.getExternalStorageDirectory().getAbsolutePath());
                entry.setTextColor(0xffffffff);
                entry.setTextSize(22);
                entry.setGravity(Gravity.CENTER);
                entry.setBackgroundColor(0xff303030);
                entry.setOnClickListener(
                        new View.OnClickListener() {
                            @Override
                            public void onClick(View view) {
                                select(context);
                            }
                        });
                ViewGroup parent = (ViewGroup) tab.getParent();
                parent.addView(
                        entry, new ViewGroup.LayoutParams(tab.getWidth() + 40, tab.getHeight()));
                entry.setX(tab.getX() + tab.getWidth() + 10);
                entry.setY(tab.getY());
            }
        }
        Entry listener = new Entry();
        root.addOnAttachStateChangeListener(listener);
        root.getViewTreeObserver().addOnGlobalLayoutListener(listener);
    }

    public static void select(Context context) {
        if (!Environment.MEDIA_MOUNTED.equals(Environment.getExternalStorageState())) {
            return;
        }

        try {
            Class<?> externalType = Class.forName("com.qinggan.music.audiocom.External");
            Object external = externalType.getField("USB").get(null);
            Class<?> daoType = Class.forName("com.qinggan.music.dao.MusicDAOImpl");
            Object dao = daoType.getConstructor(Context.class).newInstance(context);
            List<?> tracks =
                    (List<?>)
                            daoType.getMethod("getAllMusicList", externalType)
                                    .invoke(dao, external);
            externalType.getMethod("setPlayListEntry", List.class).invoke(external, tracks);
            Object playlist = externalType.getMethod("getPlayList").invoke(external);
            Class<?> moduleType = Class.forName("com.qinggan.music.bean.Module");
            Object module = moduleType.getField("LABLE_USB").get(null);
            java.lang.reflect.Field child = module.getClass().getDeclaredField("childModule");
            child.setAccessible(true);
            child.set(module, null);
            Class<?> playlistType = Class.forName("com.qinggan.music.bean.MusicPlayList");
            playlistType.getMethod("setModule", moduleType).invoke(playlist, module);
            Class<?> managerType = Class.forName("com.qinggan.app.music.model.ModelManager");
            Object manager =
                    managerType.getMethod("getInstance", Context.class).invoke(null, context);
            managerType.getMethod("selectPlayList", playlistType).invoke(manager, playlist);
        } catch (InvocationTargetException failure) {
            throw new IllegalStateException("NXP external playlist failed", failure.getCause());
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("NXP playlist contract changed", failure);
        }
    }
}
