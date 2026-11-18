package com.voyemu.prepare;

import java.util.Set;

/** Exact owner-supplied NXP callback ABI; generated donor bytes remain outside source. */
final class NxpMediaLibrary {
    static final String PROFILE = "free-09-chinese-20221230-nxp-rc45";
    static final String PIN = "93f4bea56530bf2de8634b7898fd4275a3c8985ff0392fdc58607ef8bc5b2441";
    static final Set<String> TYPES =
            Set.of(
                    "Lcom/qinggan/bus/QGBus;",
                    "Lcom/qinggan/bus/QGBusEvent;",
                    "Lcom/qinggan/bus/QGBusEvent$1;",
                    "Lcom/qinggan/bus/QGBusEventFilter;",
                    "Lcom/qinggan/bus/QGBusEventHandler;",
                    "Lcom/qinggan/bus/impl/ProtocolMessage;",
                    "Lcom/qinggan/bus/impl/QGBusImpl;",
                    "Lcom/qinggan/bus/impl/QGBusImpl$1;",
                    "Lcom/qinggan/bus/impl/QGBusImpl$2;",
                    "Lcom/qinggan/bus/impl/QGBusImpl$3;",
                    "Lcom/qinggan/bus/impl/SubscribeInfo;",
                    "Lcom/qinggan/media/MediaInfo;",
                    "Lcom/qinggan/media/MediaInfoEventHelper;",
                    "Lcom/qinggan/media/MediaInfoEventHelper$1;",
                    "Lcom/qinggan/media/MediaInfoEventHelper$MediaEventHandler;",
                    "Lcom/qinggan/media/MediaInfoEventHelper$MediaEventHandlerExt;",
                    "Lcom/qinggan/media/MediaInfoEventHelper$MediaEventHandlerExt2;",
                    "Lcom/qinggan/media/MediaInfoEventHelper$MediaEventHandlerV2;",
                    "Lcom/qinggan/media/MediaInfoEventHelper$MediaInfoEventHandler;",
                    "Lcom/qinggan/media/MusicInfo;",
                    "Lcom/qinggan/media/RadioInfo;");

    static boolean selected(String profile, int api, String pin, String output, Set<String> types) {
        return PROFILE.equals(profile)
                && api == 28
                && PIN.equals(pin)
                && output.equals("VoyemuNxpMediaCallbacks.jar")
                && TYPES.equals(types);
    }
}
