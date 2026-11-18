package com.qinggan.tencent.wecarflow;

import com.qinggan.media.StubMediaService;

/**
 * The OEM launcher widget connects to this service (inside com.qinggan.media) for the WeCarFlow
 * source and retries 5 times before giving up; the failed MediaBrowserHelper blocked subsequent
 * source switches. Just being connectable is enough.
 */
public class WecarFlowBrowserService extends StubMediaService {
    public WecarFlowBrowserService() {
        super(
                "WecarFlowBrowserService",
                "Emulator WeCarFlow",
                3,
                "com.qinggan.vehicle.WeCarVehicleAudioManager");
    }
}
