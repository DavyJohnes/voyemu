package mega.car.hardware;

public class CarPropertyValue<T> {
    private final int property, area;
    private final T value;

    public CarPropertyValue(int p, int a, T v) {
        property = p;
        area = a;
        value = v;
    }

    public int getPropertyId() {
        return property;
    }

    public int getAreaId() {
        return area;
    }

    public T getValue() {
        return value;
    }
}
