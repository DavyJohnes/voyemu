# Camera and DVR

The Free profiles open the Camera and DVR tiles of the application list in the OEM Camera application `com.qinggan.camera`, as the vehicle does. The vehicle cameras are replaced by the virtual BACK camera of the Android emulator.

## Camera

The Camera tile opens the ordinary camera: preview, photos and video recording. The virtual BACK camera is the default camera of the application.

## DVR

The DVR tile opens the OEM DVR view: a full-screen preview of the DVR camera without the photo and video bar. The virtual BACK camera is the DVR camera.

As in the vehicle, the view requests its display from the DVR unit with the CAN signal `DVR_DISPLAY_REQ` and forwards each touch as `DVR_TOUCH_MOTION`, `DVR_COORDINATE_X` and `DVR_COORDINATE_Y`. The CAN service `qg.canbus` of the machine accepts these signals, and `vehicle.can.get` reads them by name, see [CAN model](vehicle.md#can-model). The machine has no DVR unit, so the view shows the camera picture without the menus of the unit.

The view reports a DVR error only when `qg.canbus` reports the error state of the DVR system; the machine reports no error.

Without a virtual BACK camera with JPEG output, both tiles behave as in a vehicle without these cameras.
