# Camera and DVR

The Free profiles open the Camera tile of the application list in the OEM Camera application `com.qinggan.camera` and the DVR tile in the DVR application of their vehicle type, as the vehicle does. The vehicle cameras are replaced by the virtual BACK camera of the Android emulator.

## Camera

The Camera tile opens the ordinary camera: preview, photos and video recording. The virtual BACK camera is the default camera of the application.

## DVR

In the vehicle type H97C of Free 6.11.1 and 7.3.1, the DVR tile opens the OEM DVR application `com.qinggan.dvr` with the preview of the virtual BACK camera; preparation adds to the application pinned ARM64 libraries of ijkplayer and CameraX. Free Sport+ RC1 shows no DVR tile, as its vehicle has no DVR.

In the other Free profiles, the DVR tile opens the OEM DVR view of the Camera application: a full-screen preview of the DVR camera without the photo and video bar. The virtual BACK camera is the DVR camera.

As in the vehicle, the view requests its display from the DVR unit with the CAN signal `DVR_DISPLAY_REQ` and forwards each touch as `DVR_TOUCH_MOTION`, `DVR_COORDINATE_X` and `DVR_COORDINATE_Y`. The CAN service `qg.canbus` of the machine accepts these signals, and `vehicle.can.get` reads them by name, see [CAN model](vehicle.md#can-model). The machine has no DVR unit, so the view shows the camera picture without the menus of the unit.

The view reports a DVR error only when `qg.canbus` reports the error state of the DVR system; the machine reports no error.

Without a virtual BACK camera with JPEG output, both tiles behave as in a vehicle without these cameras.
