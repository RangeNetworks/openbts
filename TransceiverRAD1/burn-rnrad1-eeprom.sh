#!/bin/sh

# comments tomr 12_14_11

# General procedure for a virgin eeprom (new SDR board or a corrupted one), note all LEDs will be off 
#
# first use lsusb to find the RAD1 USB device, it should display the Cypress FX2 development string
# this comes up when the FX2 boot but sees no valid data from the eeprom 
# Bus 001 Device 102: ID 04b4:8613 Cypress Semiconductor Corp. CY7C68013 EZ-USB FX2 USB 2.0 Development Kit
#
# In the above example the usb bus is --> 001 and the device --> 102 
# you need to provide those args in the path below for -D arg
#                       used here    vvv vvv
# sudo fxload -t fx2 -D /dev/bus/usb/001/102 -I /usr/local/share/usrp/rev4/std.ihx
# once you have successfully loaded the firmware the LED on the SDR board will be blinking rapidly 
# now you can execute this programming script
# we had planned to make this more so this is more automatic

# note on the programming data string, the first byte is the starting offset, the next 8 bytes are data

lsusb | grep Cypress
# echo "sudo fxload -t fx2 -D /dev/bus/usb/xxx/xxx -I ezusb.ihx"
# exit 0
#./RAD1Cmd -x load_firmware ezusb.ihx
sleep 1
./RAD1Cmd i2c_write 0x50 00c2feff0200040000
sleep 1
./RAD1Cmd i2c_write 0x50 0800c300000200b312
sleep 1
./RAD1Cmd i2c_write 0x50 10002d80feaa825380
sleep 1
./RAD1Cmd i2c_write 0x50 18cf8a8212001c8508
sleep 1
./RAD1Cmd i2c_write 0x50 208212001c43803022
sleep 1
./RAD1Cmd i2c_write 0x50 28aa827b08ea23fa13
sleep 1
./RAD1Cmd i2c_write 0x50 309281d280c280dbf4
sleep 1
./RAD1Cmd i2c_write 0x50 382275803875b23b75
sleep 1
./RAD1Cmd i2c_write 0x50 40a0c075b4cf75b1f0
sleep 1
./RAD1Cmd i2c_write 0x50 4875b6f890e68ae4f0
sleep 1
./RAD1Cmd i2c_write 0x50 500090e680e4f053a0
sleep 1
./RAD1Cmd i2c_write 0x50 58fe43a001c2817508
sleep 1
./RAD1Cmd i2c_write 0x50 600175820112000875
sleep 1
./RAD1Cmd i2c_write 0x50 68080f758208120008
sleep 1
./RAD1Cmd i2c_write 0x50 707508007582141200
sleep 1
./RAD1Cmd i2c_write 0x50 78087a00ea24e0f582
sleep 1
./RAD1Cmd i2c_write 0x50 80e434e1f583e4f00a
sleep 1
./RAD1Cmd i2c_write 0x50 88ba10f07a007b000a
sleep 1
./RAD1Cmd i2c_write 0x50 90ba00010beb30e7f7
sleep 1 
./RAD1Cmd i2c_write 0x50 9863a04080f27800e8
sleep 1
./RAD1Cmd i2c_write 0x50 a04400600c79009018
sleep 1
./RAD1Cmd i2c_write 0x50 a800e4f0a3d8fcd9fa
sleep 1
./RAD1Cmd i2c_write 0x50 b0d083d082f6d8fdc0
sleep 1
./RAD1Cmd i2c_write 0x50 b882c0837582002275
sleep 1
./RAD1Cmd i2c_write 0x50 c08108120091e58260
sleep 1
./RAD1Cmd i2c_write 0x50 c80302000302000380
sleep 1
./RAD1Cmd i2c_write 0x50 d001e60000
sleep 1
