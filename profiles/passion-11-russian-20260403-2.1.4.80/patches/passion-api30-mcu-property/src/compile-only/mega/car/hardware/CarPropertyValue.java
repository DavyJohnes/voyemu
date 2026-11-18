package mega.car.hardware;

public class CarPropertyValue<T> implements android.os.Parcelable {
    public CarPropertyValue(int p0, int p1, T p2) {}

    @Override
    public int describeContents() {
        return 0;
    }

    public int getPropertyId() {
        return 0;
    }

    public T getValue() {
        return null;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
