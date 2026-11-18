package android.graphics;

public class Matrix {
    public void getValues(float[] values) {
        float[] source = {1.0f, 0.1f, 20.0f, 0.2f, 2.0f, 30.0f, 0.0f, 0.0f, 1.0f};

        System.arraycopy(source, 0, values, 0, 9);
    }
}
