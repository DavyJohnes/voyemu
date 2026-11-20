package android.text;

public interface InputType {
    // checkstyle-allow InterfaceIsType: mirrors the constant interface of the original API
    int TYPE_MASK_CLASS = 15,
            TYPE_MASK_VARIATION = 4080,
            TYPE_CLASS_TEXT = 1,
            TYPE_CLASS_NUMBER = 2,
            TYPE_TEXT_VARIATION_PASSWORD = 128,
            TYPE_TEXT_VARIATION_WEB_PASSWORD = 224,
            TYPE_TEXT_VARIATION_VISIBLE_PASSWORD = 144,
            TYPE_NUMBER_VARIATION_PASSWORD = 16;
}
