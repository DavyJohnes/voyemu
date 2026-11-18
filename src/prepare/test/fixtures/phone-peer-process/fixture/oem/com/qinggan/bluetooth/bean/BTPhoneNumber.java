package com.qinggan.bluetooth.bean;

/** Test double of the OEM phone number of a contact, in the A9 and NXP constructor shapes. */
public class BTPhoneNumber {
    public String number;
    public int type;

    public BTPhoneNumber() {}

    public BTPhoneNumber(int type, String number) {
        this.type = type;
        this.number = number;
    }
}
