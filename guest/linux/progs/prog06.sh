#!/bin/bash

## This file is used as the guest agent for reported PCI experiments

echo $1
sleep 1
if ip link | grep "eth0"; then                                                                                                                                                                                                                                                                                                                                                                                   
ip link set dev eth0 up                                                                                                                                                                                                                                                                                                                                                                                                                 
elif ip link | grep "wlan0"; then                                                                                                                                                                                                                                                                                                                                                                                
ip link set dev wlan0 up                                                                                                                                                                                                                                                                                                                                                                                                                
fi
#ifconfig
