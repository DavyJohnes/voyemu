# Guests and the interguest link

A session runs up to two guests: Android for the multimedia and QNX for the cluster. Both are the vehicle's own firmware with the adaptations of the profile; the host connects them as the vehicle does and draws their screens in one [window](window.md). In the code the two guests are `GuestAndroid` and `GuestQnx`. Both guests are arm64, so on an x64 host they run in software emulation.

## GuestAndroid

The multimedia runs in the pinned Android Emulator on an arm64 system image of the profile's Android version, with the OEM system built by [preparation](preparation.md). It has two screens of 1920×720: the primary and the secondary one. The supervisor reaches the guest through [embedded ADB](embedded-adb.md) and reads its UI tree through an observer installed in the guest.

`multimedia.reboot` with `mode: warm` reboots Android and keeps the session and its ownership. Every guest start, including a reboot, is a new generation, `android_generation`. A start restores the Android checkpoint when it is valid and otherwise performs a cold boot, see [launch and sessions](launch.md).

The phone of the multimedia is a synthetic phone scenario, driven by the `multimedia.phone.*` operations of the [Voyemu API](api.md): Russian contacts, incoming and outgoing calls to `+7` numbers and the OEM answer and hangup input, without real calls. The QNX call panels can be brief and truncate names. Host microphone input, real calls and OEM cloud services are not part of Voyemu, so the secondary screen can show a connection error.

## GuestQnx

The cluster runs the OEM QNX images of the firmware in the pinned QEMU for aarch64, see `src/app/qnx-host.json`. A hardware adapter, named in `profile-qnx.json`, replaces the vehicle hardware that QNX expects. The cluster frames go to a graphics worker that draws them with ANGLE into the window and also serves them over gRPC. On the Free 6.0.3, 6.11.1 and 7.3.1 profiles the cluster plays the OEM startup sequence when it starts with the session or with its power button, as when the driver turns the vehicle on: the Voyah logo with its halo and the dots, then the gauges appear, every telltale lights up, the speedometer and the power gauge sweep to their maximum and back, the telltales go out and the cluster reaches its normal state. QNX starts with the [ignition](vehicle.md#ignition-at-cluster-start) off. When the gauges and the OEM `animation` application have loaded, the QNX startup script sends the animation-ready signal that the multimedia sends in the vehicle, and the OEM power manager starts the sequence if the driver door is closed and the key is in the car. Once the gauges have appeared, the QNX startup script asks the session to switch the ignition on; the sequence ends by itself. `cluster.reboot`, the restart button and a reboot of QNX itself reset the cluster of a running vehicle: the ignition stays on, and the cluster returns to its normal state without the sequence, as the OEM power manager does with the ignition on.

The cluster shows the values of the [vehicle model](vehicle.md) through an adapter of the OEM MCU protocol. An adapter supports a set of MCU fields per profile and rejects changes to the others. `cluster.reboot` restarts QNX within the session and starts a new generation, `qnx_generation`. The cluster is observed through frames and `cluster.system`; on the Free 6.0.3, 6.11.1 and 7.3.1 profiles `session.ui` also reads its Kanzi node tree, see [the cluster UI tree](api.md#cluster-ui-tree).

## Interguest link

In the vehicle the multimedia and the cluster share a network and talk over QNET. The interguest link recreates it: the supervisor relays Ethernet frames between the Android Emulator and QEMU over loopback sockets that only the session owns. The link has no host networking, NAT or forwarding to other hosts.

The link also has a mailbox in the QNX RAM. Through it the host carries the host UTC time to QNX and sends the `flush` request: before the host pauses or stops QNX, the guest syncs and remounts its storage and answers with an ACK and a status. A `flush` that was sent but not answered has an unknown outcome, and the host does not repeat it automatically, see [errors](errors.md).
