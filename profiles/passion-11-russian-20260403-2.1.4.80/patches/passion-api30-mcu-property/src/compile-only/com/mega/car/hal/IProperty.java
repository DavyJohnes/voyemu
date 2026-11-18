package com.mega.car.hal;

public interface IProperty {
    interface Receiver {
        void onPropertyChange(java.util.List<mega.car.hardware.property.CarPropertyEvent> p0);

        void onPropertyChange(mega.car.hardware.property.CarPropertyEvent p0);

        void onPropertySetError(int p0, int p1);
    }

    boolean enableDispatchInitValue();

    String getPermission(int p0);

    mega.car.hardware.CarPropertyValue getProperty(int p0, int p1);

    android.util.SparseArray<mega.car.hardware.CarPropertyConfig<?>> getPropertyConfigMap();

    float getSampleRate(int p0);

    void init();

    void release();

    void setListener(IProperty.Receiver p0);

    void setProperty(mega.car.hardware.CarPropertyValue p0);

    void subscribeProperty(int p0, float p1);

    void unsubscribeProperty(int p0);
}
