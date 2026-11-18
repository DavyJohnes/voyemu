package mega.car.hardware.property;

public class CarPropertyEvent implements android.os.Parcelable {
    public CarPropertyEvent(int p0, mega.car.hardware.CarPropertyValue<?> p1) {}

    @Override
    public int describeContents() {
        return 0;
    }

    public mega.car.hardware.CarPropertyValue<?> getCarPropertyValue() {
        return null;
    }

    @Override
    public void writeToParcel(android.os.Parcel p0, int p1) {}
}
