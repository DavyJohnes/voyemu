/* Guest shim for libemulator_multidisplay_jni.so on Android 9:
 * libutils there lacks android::sp_report_stack_pointer (added in R).
 * The jni library DT_NEEDEDs this shim to resolve the symbol. */
void _ZN7android23sp_report_stack_pointerEv(void) {}
