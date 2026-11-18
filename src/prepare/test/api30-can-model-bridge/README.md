# API30 shared CAN model regression

Run `bash src/prepare/test/api30-can-model-bridge/run.sh [ABSOLUTE_JOB_DIRECTORY]` from the repository. The default uses a unique project-local temporary directory and removes it after the run; an explicit directory retains compiler outputs for diagnosis. Java is selected by JAVA_BIN, JAVA_HOME or the existing javac in PATH. No emulator or downloads are used.

The test compiles the real CanModelClient and CanModelBridge of the shared CanBus component against a small Android socket/clock seam. The socket seam transports the actual CAN2 request/reply bytes to a native child process running production model_protocol.c, model_queue.c and mcu_model.c. It covers native-model raw PW/FEV changes, Java setter/getter consistency, OEM energy modes1–5, stale epoch rejection, ACC reset with `getAccStatus` and source lease loss. Repeated native packet delivery and the actual Android Binder/SELinux/kernel route remain VM acceptance gates; this host test does not claim ioctl admission or native receiver delivery.

The [canbus](../canbus/README.md) component regression additionally produces the signed APK, checks actual DEX classes/service hierarchy, reads OEM VehicleState initializers for665/957 and verifies runtime shared-library closure. Its supplied patched QGAPI path must be a verified existing artifact, not an invented test library.

Source-number evidence was obtained from the pinned API30 MusicConcate donor: HUM_VSP_FUNCTION_SW=665, IVI_SOC_MODESET=957. VehicleSetting classes2.dex binding callbacks send power modes1–4 to setPowerMode, which writes IVI_SOC_MODESET via the real OEM bundle API. Native raw0x68 validation remains separate from the semantic energy-mode command path.
