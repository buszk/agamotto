#!/bin/bash

## This file is used as the guest agent for reported PCI experiments

ip link
echo $1
ip link
if ip link | grep "eth0"; then
ip link set dev eth0 address 02:ca:fe:f0:0d:01
ip link set dev eth0 up
elif ip link | grep "wlan0"; then
ip link set dev wlan0 up
fi
#ifconfig
