#!/bin/bash
echo "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"21e1\", ATTRS{idProduct}==\"001A\",  ACTION==\"add\", ENV{CAEN_DEVICE}=\"V4718\"" > /etc/udev/rules.d/90-CAEN-V4718.rules
echo "IMPORT{parent}=\"CAEN_DEVICE\"" >> /etc/udev/rules.d/90-CAEN-V4718.rules
echo "SUBSYSTEM==\"net\", ACTION==\"add\", ENV{CAEN_DEVICE}==\"V4718\", RUN+=\"/sbin/ip link set mtu 15000 dev %k\"" >> /etc/udev/rules.d/90-CAEN-V4718.rules
echo "SUBSYSTEM==\"net\", ACTION==\"move\", ENV{CAEN_DEVICE}==\"V4718\", RUN+=\"/sbin/ip link set mtu 15000 dev %k\"" >> /etc/udev/rules.d/90-CAEN-V4718.rules
udevadm control --reload-rules
start="hosts:"
search="mdns4_minimal"
replace="mdns_minimal"
sed -i -e "/^$start/s/$search/$replace/g" /etc/nsswitch.conf
echo "Driver V4718-USB installed!"
