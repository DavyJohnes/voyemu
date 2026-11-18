package com.qinggan.canbus.service;

/** OEM transport of the CAN box and MCU devices that a CAN bus component sends commands through. */
public interface ICanBusOperator {
    int getCanBusState();

    int sendCanCmd(int fd, int command, byte[] data);

    byte[] sendCanCmdBytes(int fd, int command, byte[] data);

    int sendMuCmd(int fd, int command, byte[] data);

    int sendSocketCmd(int command, byte[] data);
}
