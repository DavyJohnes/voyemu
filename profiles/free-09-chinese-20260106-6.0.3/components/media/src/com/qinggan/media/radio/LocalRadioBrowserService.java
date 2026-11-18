package com.qinggan.media.radio;

import com.qinggan.media.StubMediaService;

public class LocalRadioBrowserService extends StubMediaService {
    public static void start(android.content.Context context) {
        context.startService(new android.content.Intent(context, LocalRadioBrowserService.class));
    }

    public LocalRadioBrowserService() {
        super("LocalRadioBrowserService", "Emulator Radio", 3, "com.qinggan.media.radio");
    }
}
