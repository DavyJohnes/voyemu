package fixture;

/** Own test boundary, no OEM implementation: implicit Handler requires this thread's Looper. */
public class HfpConstructorBoundary {
    public HfpConstructorBoundary() {
        new android.os.Handler(new android.os.Handler.Callback() {});
    }
}
