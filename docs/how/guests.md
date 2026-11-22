# Guests and the interguest link

A session runs up to two guests: Android for the multimedia and QNX for the cluster. Both are the vehicle's own firmware with the adaptations of the profile; the host connects them as the vehicle does and draws their screens in one [window](window.md). In the code the two guests are `GuestAndroid` and `GuestQnx`. Both guests are arm64, so on an x64 host they run in software emulation.

## GuestAndroid

The multimedia runs in the pinned Android Emulator on an arm64 system image of the profile's Android version, with the OEM system built by [preparation](preparation.md). It has two screens of 1920×720: the primary and the secondary one. The supervisor reaches the guest through [embedded ADB](embedded-adb.md) and reads its UI tree through an observer installed in the guest.

`multimedia.reboot` with `mode: warm` reboots Android and keeps the session and its ownership. Every guest start, including a reboot, is a new generation, `android_generation`. A start restores the Android checkpoint when it is valid and otherwise performs a cold boot, see [launch and sessions](launch.md).

The phone of the multimedia is a synthetic phone scenario, driven by the `multimedia.phone.*` operations of the [Voyemu API](api.md): Russian contacts, incoming and outgoing calls to `+7` numbers and the OEM answer and hangup input, without real calls. The QNX call panels can be brief and truncate names. Host microphone input, real calls and OEM cloud services are not part of Voyemu, so the secondary screen can show a connection error.

## GuestQnx

The cluster runs the OEM QNX images of the firmware in the pinned QEMU for aarch64, see `src/app/qnx-host.json`. A hardware adapter, named in `profile-qnx.json`, replaces the vehicle hardware that QNX expects. The cluster frames go to a graphics worker that draws them with ANGLE into the window and also serves them over gRPC. The Free cluster starts the OEM gauges as soon as the guest services are ready. Starting or restarting QNX delivers the [current ignition](vehicle.md#ignition-at-cluster-start).

The cluster shows the values of the [vehicle model](vehicle.md) through an adapter of the OEM MCU protocol. An adapter supports a set of MCU fields per profile; the cluster receives the other fields with their initial values, while the model keeps the written values. `cluster.reboot` restarts QNX within the session and starts a new generation, `qnx_generation`. The cluster is observed through frames and `cluster.system`; on the Free 6.0.3, 6.11.1 and 7.3.1 profiles `session.ui` also reads its Kanzi node tree, see [the cluster UI tree](api.md#cluster-ui-tree).

## Interguest link

In the vehicle the multimedia and the cluster share a network and talk over QNET. The interguest link recreates it: the supervisor relays Ethernet frames between the Android Emulator and QEMU over loopback sockets that only the session owns. The link has no host networking, NAT or forwarding to other hosts.

The link also has a mailbox in the QNX RAM. Through it the host carries the host UTC time to QNX and sends the `flush` request: before the host pauses or stops QNX, the guest syncs and remounts its storage and answers with an ACK and a status. A `flush` that was sent but not answered has an unknown outcome, and the host does not repeat it automatically, see [errors](errors.md).

On Passion the multimedia side of the link is the vehicle's `eth0`. Once the emulator has moved its own network adapter into its router namespace, the link adapter takes the name `eth0`, and the OEM network setup runs unchanged: the OEM `config_vlan.sh` creates the 802.1Q interfaces `eth0.N`, the OEM product overlay gives EthernetService the vehicle's addresses, `172.30.100.3` on `eth0` and `172.16.11N.40` on `eth0.1N`, and five OEM `someipd` serve SOME/IP on `eth0.10` to `eth0.14`. Voyemu only checks that `eth0` has `172.30.100.3`.
