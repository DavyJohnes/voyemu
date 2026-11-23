# USB drive

The multimedia of every profile has a USB port with a virtual USB drive, as the vehicle has a port for the user's stick. Plugged in, the drive reaches Android as a real stick does: the OEM Music, Video and Gallery applications find its files, and unplugging it returns them to their state without a USB drive.

## Plugging in and out

The drive is plugged in and out while the multimedia runs:

- in [the window](window.md#controls-next-to-the-wheel), by the USB button right of the multimedia power button; its icon is white while the drive is plugged in and gray while it is out;
- through the [Voyemu API](api.md#usb-drive), by `multimedia.usb.attach` and `multimedia.usb.detach`.

Every multimedia start begins with the drive out: a new session, `multimedia.power` and a reboot. Unplugging is like pulling a stick out of the vehicle without ejecting it first. Before the session saves the multimedia [checkpoint](../GLOSSARY.md#sessions-and-api), it syncs the guest and unplugs the drive, so the next start restores the checkpoint without it.

## How Android sees the drive

The multimedia has a USB 2.0 host controller from power-on. Plugging in adds a USB mass-storage device with removable media to it, so the Android kernel finds a new SCSI disk, as with a stick in the USB port of the vehicle. The storage service `vold` mounts its FAT32 partition at `/storage/2026-1122` and announces it to the applications; the media indexers of the firmware then scan the files. A machine has a `vold` entry for USB drives with the label of the vehicle: `usbotg` on Free 6.0.3, 6.11.1, 7.3.1, Free Sport+ RC1 and Passion, `usb` on Free NXP RC45, as in the firmware of these vehicles.

Free 6.0.3 and Free NXP RC45 boot the Android 9 [development kernel](components.md), which includes SCSI disks and USB mass storage; the Android 11 kernel of the other profiles has them already.

## Test files

The drive holds test files that Voyemu generates itself, without third-party media. The first plugging of a machine writes the 64 MiB drive image `usb-drive.img` into the Android directory of the machine; the guest writes to it like to a real stick, and the files stay until the image is deleted. The image has one FAT32 partition labeled `VOYEMU` and the folders of a user's stick:

| Folder | Files |
| --- | --- |
| `Music/Voyemu Test Tones/` | Three FLAC tracks with tags and a cover, one track with a Cyrillic name, one WAV track and `cover.jpg`; 20-second tones at 44.1 kHz, stereo |
| `Video/` | Two MP4 clips with H.264 video of 640×360 at 25 frames per second with a moving square and an AAC sound track, 10 seconds each |
| `Photos/` | Three JPEG photos of 1920×1080 and two PNG images of 1280×720 |
