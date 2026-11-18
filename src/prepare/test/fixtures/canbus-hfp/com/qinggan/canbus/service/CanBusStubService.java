package com.qinggan.canbus.service;

public class CanBusStubService {
    public int count;
    public boolean reject;
    public com.qinggan.canbus.PhoneInfo last;

    public void updatePhoneInfo(com.qinggan.canbus.PhoneInfo v) {
        if (reject) {
            throw new IllegalStateException("no PII in diagnostic");
        }

        count++;
        last = v;
    }
}
