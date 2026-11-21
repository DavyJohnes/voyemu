# Vehicle model and CAN

The guests expect a vehicle around them: speed, gear, doors, lights, temperatures, charge and driver-assistance states. Voyemu supplies these values from a vehicle model on the host. The model is the single source for the window controls, the `patch` command and the Voyemu API.

## Vehicle model

The model is a fixed catalog of 579 fields in the layout of the OEM MCU protocol. Each field has an index, a name, a bit width and a default value. A profile can override defaults in `initial_state` of `profile.json`, for example a gear or an outside temperature.

`vehicle.fields` returns the catalog with each field's range, unit and write mode. Values are raw OEM values except `speed` in km/h, `speed10x` in tenths of km/h and `soc` in percent. A change is atomic: it fits each value into its OEM width, keeps coupled fields consistent, limits `soc` to 100 and accepts whole km/h. Steering-wheel fields, those starting with `swm_`, change only through wheel input.

A change of one field also sets its coupled fields unless the same change names them itself: `speed` sets `speed10x`, and a whole `speed10x` sets `speed`; `actual_power` and `allowed_power` set `actual_power10x` and `allowed_power10x` to ten times their value; `remain_mileage_e` sets `vcu_remainingMileageReal` and `vcu_remainingMileageCLTCWLTC`; `remain_mileage_f` sets `remain_mileage_f_nedc` and `remain_mileage_f_wltc`; a tyre pressure or temperature makes the reading of its corner valid in `bcm_tpms*Sts` and `can_v1_5_bcm_tpms*Sts`. The window, `patch` and `vehicle.set` get the same result.

```sh
./voyemu patch machines/free-09-chinese-20260106-6.0.3 --values '{"speed":80,"ambient_temp":120}'
```

`patch` and the `vehicle.set` operation change the model; `vehicle.get` reads it. A session starts from default values, and `--resume` or `scenario: resume` starts from the saved scenario of the machine instead.

## Power state

The power state of the vehicle is one of `off`, `acc`, `on` and `ready`, and a change of the single field `power` selects it: `patch` or `vehicle.set` with `{"power":"ready"}`, without other fields. The state sets `ign` and `ready_lamp` for the cluster and the ACC of the CAN model for the multimedia: `off` switches all three off, `acc` keeps `ign` and `ready_lamp` at 0 with the multimedia on, `on` sets `ign` to 1, and `ready` also sets `ready_lamp` to 1. The state also sets the 12 V battery voltage `ic_bat_voltage` in units of 0.01 V: 13.80 V at `ready`, when the DC-DC converter charges the battery, and 12.40 V in the other states; the OEM power manager of the cluster treats 9.00–16.00 V as normal. `ready` requires the gear in P or N, and a vehicle with a nonzero speed cannot go below `on`. A rejected change leaves the model as it is; `vehicle.get` shows the state, the outcome and the reason in `deliveries.power_ecu`. A direct write of `ign` or `ready_lamp` stays available for firmware tests, and the power state follows it. The state and its rules are a policy of the emulator, not a recovered OEM protocol.

## Ignition at cluster start

On the Free 6.0.3, 6.11.1 and 7.3.1 profiles a cold boot of the cluster with the session, with `cluster.power` or with the cluster power button of the window turns the vehicle on as the driver does: before QNX starts the [power state](#power-state) `on` or `ready` waits at `acc`: the model sets `ign` to 0, and after the OEM startup sequence of the cluster, at the request of the QNX startup script, the state returns, see [GuestQnx](guests.md#guestqnx). `vehicle.get` and the window show both values. Without the request the session switches `ign` back to 1 by the guest timeline: at once when the cluster frames start without any marker of the QNX startup script, 5 seconds after the script sends the animation-ready signal, 25 seconds after the gauge window, 90 seconds after the first frames, 300 seconds after the last marker while no frames appear, or when the cluster stops; with the TCG engine, which runs the guest slower, every interval except the 5 seconds grows by the same factor. `vehicle.get` names the reason in `deliveries.startup_ignition`. A value of `ign` or `power` that a client writes meanwhile stays and ends the sequence, and a model that starts with `ign` at 0 keeps it. `cluster.reboot`, the restart button and a reboot of QNX itself keep `ign` as it is.

## Delivery to the guests

The cluster receives the model through the adapter of the OEM MCU protocol, see [guests and the interguest link](guests.md). Each profile's adapter supports its own set of MCU fields; a field it does not support reaches the cluster with its initial value, while the model keeps the written value. The multimedia receives vehicle state through the simulated CAN bus of its guest.

On Free 6.11.1 and 7.3.1 the adapter writes every model field that has a field of the same signal in the OEM MCU packet of the firmware, the same set on both firmwares, as a vehicle sends it: also the fields that no part of the firmware reads, such as the steering wheel buttons without a cluster function, and the fields of the cluster power manager. Fields of the MCU packet without a model field keep the value of the OEM packet. The language that the multimedia selects becomes the model field `ivi_languageSet`; the cluster receives this one value in the MCU packet and in the OEM language command.

With a `session`, `vehicle.fields` adds `delivery` to each field: the list of guests of the running machine that receive it, `cluster`, `multimedia` or both. The cluster receives the fields of the MCU adapter of its profile: all of them on Free 6.0.3, the fields of the adapter layout on Free 6.11.1 and 7.3.1, the speed and the charge on Passion. The multimedia receives the fields that its CanBus component reads: speed, charge, gear, outside temperature, odometer, fuel, ranges, doors, hood, tailgate, lights and tyre pressure. On Passion the multimedia receives only the charge, and on Free NXP RC45 no field. An empty list means that the field stays in the model and no guest shows it. `session.status` reports the same in `field_delivery`. The window grays out the controls of such fields as a profile limit.

`session.status` shows the delivery of the current model revision to the cluster in `deliveries.qnx.vehicle`: `queued` until the QNX process that owns the OEM MCU input confirms it, then `guest_acknowledged` with the revision in `source_sequence`. Only a receipt of the current revision from the current receiver counts: its channel address (`producer_address`), guest process, incarnation (`producer_incarnation`) and cluster generation must match. When the receiver exits, the status is `unavailable` with `reason: receiver_retired`, the exited `receiver` and `last_acknowledged`, the last confirmed revision or `null`; a new receiver returns the status to `queued` until its own receipt. Running cluster frames (`graphics_running`) do not mean that the MCU path is ready.

## CAN model

The CAN model is the simulated CAN bus inside the multimedia guest. Three read-only operations observe it:

| Operation | What it returns |
| --- | --- |
| `vehicle.can.get` | Selected CAN states as an Android component inside the guest reports them, with a cursor for incremental reads |
| `vehicle.can.model` | The state of the simulated CAN model, with a cursor |
| `vehicle.can.history` | The history of changes at the CAN boundary of the current session, or the reason it is unavailable |

These reads go to Android components inside the guest through the session's own emulator transport. A read that times out or loses its transport is marked retryable, see [errors](errors.md).

On the Free 6.0.3, 6.11.1 and 7.3.1 profiles a model process in the guest, the `can-simulator` component, plays the vehicle's side of the CAN bus. It writes to the driver `cis_can_dev` of the [development kernel](components.md#development-kernels), and the OEM CAN service reads it as it reads the vehicle bus. The driver accepts from the model 10-byte CAN frames and service datagrams of up to 65,536 bytes. As in the vehicle, the model delivers the ACC power state and its charge feedback only when the value changes and sends a service datagram periodically. The Binder service `qg.canbus` reports through `getAccStatus` and `onAccStateChanged` only the ACC state the model has reported: OFF is 0, ON is 2. Before the first report, after the model connection is lost and while a power change has an unknown outcome, `getAccStatus` returns -1, the value of the OEM service before its first CAN observation.

The service `qg.canbus` keeps the OEM component API: as in the OEM service, each Binder call goes to the component `DongfengCanBusComponentImpl` and its base class `BaseCanBusComponent`, which declare the public methods of the OEM component of each firmware. The component keeps the registered callbacks and the vehicle state: the PW, FEV and ACC states of the model, the vehicle inputs delivered to the guest, the settings written through the service, the cluster theme and the climate. Getters read this state, and a change callback goes to the registered callbacks only when the values behind it change. Some OEM methods need hardware the emulator does not have:

| Methods | In the emulator |
| --- | --- |
| `sendCanIOCtl`, `sendCanIOCtlBytes`, `sendMuIoCtl`, `sendSocketCtl` | Send through the `ICanBusOperator` registered with `registerOperator`; without one they return -1 or null, as the OEM component does. Without an operator, command 0 of `sendSocketCtl` writes a complete IVC frame over the [cluster data channel](#cluster-data-channel) and returns 0 |
| `notify`, `socketNotify`, `dispatchCanData`, `dispatchData`, `onCANBusState` | The guest has no CAN box frames to decode; they return false or do nothing |
| `getCanRawData`, `setCanRawData` | No raw frame is received: the getter returns null, and a write is not sent |
| `getIpkSyncComponent`, `sendIPKNavideath` | The OEM service of navigation presentation readiness has no counterpart: the getter returns null, and `sendIPKNavideath` only sets `IVI_AVM_NAVI_STATE` to 2, as the OEM component does |
| `updateMediaSrcInfo`, `updatePhoneInfo`, `updateNaviInfo`, `updateNaviLaneInfo` | Go to the cluster over the [cluster data channel](#cluster-data-channel) |
| `updateMediaPlayInfo`, `updateMediaPlayProgressInfo`, `updateMediaPlayMode`, `updateMediaSeqType`, `updateMediaVolume`, `updateCallListInfo` | Send nothing, as the OEM component does |
| `initVehicleLastStatus` | Reads the last value from QGSettings, as the OEM component does; without QGSettings it keeps the passed default |
| `isSupportFunction`, `requestTravellingInfo`, `requestVehicleCenterControlEnabledInfo`, `setVehicleCenterControlEnabled` | The CAN box answers them; without one they return false or -1, as the OEM component does |
| `getEngineSpeed`, `getHandBrakeStatus`, `getSeatBeltStatus` | The OEM component fills them only from the generic CAN box messages 0x15, 0x10 and 0x33, which neither the Dongfeng protocol of the multimedia nor the CAN model sends; they keep the OEM values `Integer.MIN_VALUE`, -1 and null, also when the vehicle inputs carry `rpm`, `epb_parking` or the seat belts |
| `getEngineStatus`, `getVehicleKeyState`, `getBrakeFluidStatus`, `getEngineFluidStatus`, `getHevSysMode`, `getEngineTemperature` | The OEM component never fills them; they keep -1, and 0 for the last two |
| Getters of values the vehicle inputs do not carry | Return what the OEM component returns before its first data: -1 for `getEngineStatus`, `getVehicleKeyState` and the other statuses, 0 for `getHevSysMode` and `getEngineTemperature`, null for objects such as `getVehicleIO` and `getCanBoxVersion`; data objects keep the values of their OEM constructors. Before the first vehicle input the speed and the charge are -1 and the outside temperature is `Integer.MIN_VALUE` |

The OEM service `qg.carsignal` reads the MCU of the multimedia: power and wake-up states, temperatures, light sensors, versions and the vehicle configuration. The emulator has no such MCU, and on the Free 6.0.3, 6.11.1 and 7.3.1 profiles one implementation answers its Binder calls as a parked vehicle: number and flag getters return 0 or false, text getters an empty string, `getVehicleConfig` an empty array, `getRTCTime` the guest clock in seconds and `getMCUHardwareVersion` `V3.0` on Free 6.0.3 and `V1.0` on Free 6.11.1 and 7.3.1. Setters change nothing, and registered callbacks are accepted and never called. Without the service the OEM applications would wait for it for as long as they run.

## Cluster data channel

On the Free 6.0.3, 6.11.1 and 7.3.1 profiles the multimedia sends the cluster the media title, the phone call and turn-by-turn guidance over the cluster data channel. It is one persistent TCP connection from the CanBus component to `ivcd` of the cluster at `172.16.100.22:80` over the [interguest link](guests.md), in the IVC frames of the OEM firmware. The component sends the OEM heartbeat every second; when the cluster stops answering for 3.5 s, the component closes the connection and connects again with pauses growing up to 8 s.

A frame that could not be written is reported and never sent again, and nothing is repeated after a new connection. When the cluster opens its media or phone card, the component answers with the frames it last sent for that card. Requests of the cluster to open or close a presentation go to the callbacks of `qg.cluster.service` on Free 6.11.1 and 7.3.1.

The data enters through the OEM APIs: on Free 6.0.3 the `update*` methods of `qg.canbus`, on Free 6.11.1 and 7.3.1 the `qg.cluster.service` transactions for media, phone, navigation, lanes and presentation readiness. Both write the frames of the OEM encoders of their firmware; text keeps the OEM byte limits and is cut on a character boundary. As in the vehicle, the OEM service `SystemPolicy` collects this data and passes it to `qg.canbus` on Free 6.0.3 and to `qg.cluster.service` on Free 6.11.1 and 7.3.1, whose profiles set the OEM property `ro.qg.presentation=VirtualDisplay`: the media title from the OEM media applications over the OEM bus `QGBus`, calls from the OEM phone application and guidance from the navigation service when the navigation application publishes it. `SystemPolicy` and `QGBus` are both imported from the firmware. When the machine has no Bluetooth adapter, `SystemPolicy` skips only its Bluetooth policy and charging service. The synthetic phone also sends its calls to the channel directly, and the navigation application on Free 6.11.1 and 7.3.1 also calls `qg.cluster.service`.

`cluster.ipk.history` returns the frames the component sent and received with their command, kind, size and outcome, and the state of the connection, without the transmitted text. It accepts `cursor` from the previous `next_cursor`, `limit` from 1 to 256 and `timeout_ms` from 1 to 10000.
