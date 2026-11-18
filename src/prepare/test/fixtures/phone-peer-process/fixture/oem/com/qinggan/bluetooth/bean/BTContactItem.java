package com.qinggan.bluetooth.bean;

/** Test double of the OEM contact: a name and its numbers, in the A9 and NXP constructor shapes. */
public class BTContactItem {
    public String name;
    public java.util.List<BTPhoneNumber> numbers = new java.util.ArrayList<>();

    public BTContactItem() {}

    public BTContactItem(String name) {
        this.name = name;
    }
}
