package com.mega.car.hal;

import android.util.SparseArray;

import mega.car.hardware.CarPropertyConfig;
import mega.car.hardware.CarPropertyValue;
import mega.car.hardware.property.CarPropertyEvent;

public interface IProperty {
    interface Receiver {
        void onPropertyChange(CarPropertyEvent event);

        default void onPropertyChange(java.util.List<CarPropertyEvent> events) {
            for (var e : events) {
                onPropertyChange(e);
            }
        }

        void onPropertySetError(int property, int area);
    }

    CarPropertyValue getProperty(int property, int area);

    SparseArray<CarPropertyConfig<?>> getPropertyConfigMap();

    void setListener(Receiver receiver);

    void subscribeProperty(int property, float rate);

    void unsubscribeProperty(int property);

    void setProperty(CarPropertyValue value);

    String getPermission(int property);

    default float getSampleRate(int property) {
        return -1;
    }

    default boolean enableDispatchInitValue() {
        return true;
    }

    default void init() {}

    default void release() {}
}
