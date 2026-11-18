package mega.car.hardware.property;

public class CarPropertyEvent {
    private final mega.car.hardware.CarPropertyValue value;

    public CarPropertyEvent(int type, mega.car.hardware.CarPropertyValue v) {
        if (type != 0) {
            throw new IllegalArgumentException();
        }

        value = v;
    }

    public mega.car.hardware.CarPropertyValue getCarPropertyValue() {
        return value;
    }
}
