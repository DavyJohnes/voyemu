package com.qinggan.canbus.service;

import com.qinggan.canbus.service.protocol.dongfeng.DongfengCanBusComponentImpl;

public final class CanBusService extends RegistrationService {
    private final BaseCanBusComponent mCanBusComponent = new DongfengCanBusComponentImpl();

    @Override
    protected BaseCanBusComponent canBusComponent() {
        return mCanBusComponent;
    }
}
