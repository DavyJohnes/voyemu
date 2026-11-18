package com.android.car.settings.common;

import android.content.Context;

import androidx.preference.Preference;

public class SeekBarPreference extends Preference {
    public int min, max, value;

    public SeekBarPreference(Context c) {}

    public void setContinuousUpdate(boolean b) {}

    public void setShowSeekBarValue(boolean b) {}

    public void setMin(int x) {
        min = x;
    }

    public void setMax(int x) {
        max = x;
    }

    public void setValue(int x) {
        value = x;
    }
}
