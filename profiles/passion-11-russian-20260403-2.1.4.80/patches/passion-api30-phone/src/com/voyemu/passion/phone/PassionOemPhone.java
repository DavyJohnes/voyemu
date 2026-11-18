package com.voyemu.passion.phone;

import android.content.Context;
import android.database.ContentObserver;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.TextView;

import java.lang.ref.WeakReference;
import java.lang.reflect.Field;

/** Injected only into the pinned emulator OEM phone; Bluetooth state remains OEM-owned. */
public final class PassionOemPhone {
    private static final Uri URI = Uri.parse("content://com.voyemu.input.phone");
    private static Context context;
    private static ContentObserver observer;
    private static WeakReference<Object> panel = new WeakReference<>(null);
    private static WeakReference<Object> presentedPanel = new WeakReference<>(null);

    private PassionOemPhone() {}

    public static synchronized void start(Context value) {
        if (context != null) {
            return;
        }

        context = value.getApplicationContext();
        observer =
                new ContentObserver(new Handler(Looper.getMainLooper())) {
                    @Override
                    public void onChange(boolean selfChange) {
                        refreshContacts();
                        refreshPanel();
                    }
                };
        context.getContentResolver().registerContentObserver(URI, false, observer);
        refreshContacts();
    }

    public static synchronized void stop() {
        if (context == null) {
            return;
        }

        context.getContentResolver().unregisterContentObserver(observer);
        observer = null;
        context = null;
        panel.clear();
        presentedPanel.clear();
    }

    public static boolean available(Context value) {
        try (Cursor row = value.getContentResolver().query(URI, null, null, null, null)) {
            return row != null && row.moveToFirst() && row.getInt(0) == 1;
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: OEM reflection and provider failures keep synthetic
            // phone delivery fail-closed.
            return false;
        }
    }

    public static boolean syntheticAvailable() {
        Context value = context;

        return value != null && available(value);
    }

    public static int syntheticContactSyncState() {
        // The local ContactsContract batch commits before the peer becomes available.
        return syntheticAvailable() ? 2 : 0;
    }

    public static boolean showSyntheticPanel(Object fragment) {
        try {
            Class<?> type = fragment.getClass();
            Context viewContext = (Context) type.getMethod("getContext").invoke(fragment);

            if (viewContext == null) {
                return false;
            }

            start(viewContext);
            panel = new WeakReference<>(fragment);

            if (!available(viewContext)) {
                clearSyntheticPanel(fragment);

                return false;
            }

            View book = (View) field(fragment, "btBook");
            View record = (View) field(fragment, "btRecord");
            View bookBanner = (View) field(fragment, "bookBanner");
            View recordBanner = (View) field(fragment, "recordBanner");
            View btSwitch = (View) field(fragment, "mBtSwitchLayout");
            View hfpSwitch = (View) field(fragment, "rlHfpSwitchLayout");
            View connectFrame = (View) field(fragment, "mBtConnectFrameLayout");
            TextView label = (TextView) field(fragment, "btConnectAddress");
            Object keypad = field(fragment, "mKeyBoardFragment");

            if (book == null || record == null || label == null || keypad == null) {
                return false;
            }

            keypad.getClass().getMethod("setKeyBoardState", boolean.class).invoke(keypad, true);
            show(btSwitch, View.INVISIBLE);
            show(hfpSwitch, View.GONE);
            show(connectFrame, View.INVISIBLE);
            book.setVisibility(View.VISIBLE);
            record.setVisibility(View.VISIBLE);
            show(bookBanner, book.isSelected() ? View.VISIBLE : View.INVISIBLE);
            show(recordBanner, record.isSelected() ? View.VISIBLE : View.INVISIBLE);
            label.setText("Voyemu Phone");
            label.setVisibility(View.VISIBLE);
            presentedPanel = new WeakReference<>(fragment);

            return true;
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: OEM reflection and provider failures keep synthetic
            // phone delivery fail-closed.
            android.util.Log.w("VoyemuPassionPhone", "Synthetic phone panel unavailable");

            return false;
        }
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    private static void refreshPanel() {
        Object fragment = panel.get();

        if (fragment == null) {
            return;
        }

        try {
            Class<?> type = fragment.getClass();

            if (!Boolean.TRUE.equals(type.getMethod("isResumed").invoke(fragment))) {
                return;
            }

            boolean wasPresented = presentedPanel.get() == fragment;

            if (showSyntheticPanel(fragment) || !wasPresented) {
                return;
            }

            java.lang.reflect.Method restore =
                    type.getDeclaredMethod("updateHfpStateView", int.class);
            restore.setAccessible(true);
            restore.invoke(fragment, 0);
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: OEM reflection and provider failures keep synthetic
            // phone delivery fail-closed.
            android.util.Log.w("VoyemuPassionPhone", "Synthetic phone panel refresh unavailable");
        }
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    private static void clearSyntheticPanel(Object fragment) throws Exception {
        if (presentedPanel.get() != fragment) {
            return;
        }

        presentedPanel.clear();
        Object keypad = field(fragment, "mKeyBoardFragment");

        if (keypad != null) {
            keypad.getClass().getMethod("setKeyBoardState", boolean.class).invoke(keypad, false);
            keypad.getClass()
                    .getMethod("setImgPlaceCallState", boolean.class)
                    .invoke(keypad, false);
        }

        TextView label = (TextView) field(fragment, "btConnectAddress");

        if (label != null) {
            label.setText("");
        }
    }

    private static Object field(Object fragment, String name) throws Exception {
        Field field = fragment.getClass().getDeclaredField(name);

        field.setAccessible(true);

        return field.get(fragment);
    }

    private static void show(View view, int visibility) {
        if (view != null) {
            view.setVisibility(visibility);
        }
    }

    public static void placeCall(Context value, String number) {
        // Never fall through to another Telecom account or to an arbitrary number.
        Bundle extras = new Bundle();

        extras.putString("number", number);

        try {
            value.getContentResolver().call(URI, "oemOutgoing", null, extras);
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: OEM reflection and provider failures keep synthetic
            // phone delivery fail-closed.
            android.util.Log.w("VoyemuPassionPhone", "Synthetic outgoing refused");
        }
    }

    public static void placeCall(String number) {
        Context value = context;

        if (value != null) {
            placeCall(value, number);
        }
    }

    public static void placeCall(Uri address) {
        if (address != null && "tel".equals(address.getScheme())) {
            placeCall(address.getSchemeSpecificPart());
        }
    }

    public static void refreshContacts() {
        Context value = context;

        if (value == null) {
            return;
        }

        try {
            Class<?> type =
                    Class.forName(
                            "com.voyah.btphone.data.ContactLiveData", true, value.getClassLoader());
            Object live = type.getMethod("getInstance").invoke(null);

            if (available(value)) {
                type.getMethod("getContact").invoke(live);
            } else {
                type.getMethod("postValue", Object.class).invoke(live, new Object[] {null});
            }
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: OEM reflection and provider failures keep synthetic
            // phone delivery fail-closed.
            android.util.Log.w("VoyemuPassionPhone", "OEM contact delivery unavailable");
        }
    }
}
