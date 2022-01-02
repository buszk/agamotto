#!/bin/bash

id=$(/root/agent-get-prog)

progs=(
    "agent-aqc100-debug.sh" # 0
    "agent-aqc100-prog01.sh" # 1
    "agent-aqc100-prog02.sh" # 2
    "agent-aqc100-prog03.sh" # 3
    "agent-aqc100-prog04.sh" # 4
    "agent-aqc100-prog05.sh" # 5
    "agent-aqc100-prog06.sh" # 6
    "agent-aqc100-prog80.sh" # 7
    "agent-aqc100-prog81.sh" # 8
    "agent-aqc100-prog96.sh" # 9
    "agent-aqc100-prog97.sh" # 10
    "agent-aqc100-prog98.sh" # 11
    "agent-aqc100-prog99.sh" # 12
    "agent-quark-debug.sh" # 13
    "agent-quark-prog01.sh" # 14
    "agent-quark-prog02.sh" # 15
    "agent-quark-prog03.sh" # 16
    "agent-quark-prog04.sh" # 17
    "agent-quark-prog05.sh" # 18
    "agent-quark-prog06.sh" # 19
    "agent-quark-prog80.sh" # 20
    "agent-quark-prog81.sh" # 21
    "agent-quark-prog96.sh" # 22
    "agent-quark-prog97.sh" # 23
    "agent-quark-prog98.sh" # 24
    "agent-quark-prog99.sh" # 25
    "agent-rtl8139-debug.sh" # 26
    "agent-rtl8139-prog01.sh" # 27
    "agent-rtl8139-prog02.sh" # 28
    "agent-rtl8139-prog03.sh" # 29
    "agent-rtl8139-prog04.sh" # 30
    "agent-rtl8139-prog05.sh" # 31
    "agent-rtl8139-prog06.sh" # 32
    "agent-rtl8139-prog80.sh" # 33
    "agent-rtl8139-prog81.sh" # 34
    "agent-rtl8139-prog96.sh" # 35
    "agent-rtl8139-prog97.sh" # 36
    "agent-rtl8139-prog98.sh" # 37
    "agent-rtl8139-prog99.sh" # 38
    "agent-snic-debug.sh" # 39
    "agent-snic-prog01.sh" # 40
    "agent-snic-prog02.sh" # 41
    "agent-snic-prog03.sh" # 42
    "agent-snic-prog04.sh" # 43
    "agent-snic-prog05.sh" # 44
    "agent-snic-prog06.sh" # 45
    "agent-snic-prog80.sh" # 46
    "agent-snic-prog81.sh" # 47
    "agent-snic-prog96.sh" # 48
    "agent-snic-prog97.sh" # 49
    "agent-snic-prog98.sh" # 50
    "agent-snic-prog99.sh" # 51
    "agent-usb-debug.sh" # 52
    "agent-usb-prog01.sh" # 53
    "agent-usb-prog02.sh" # 54
    "agent-usb-prog03.sh" # 55
    "agent-usb-prog04.sh" # 56
    "agent-usb-prog05.sh" # 57
    "agent-usb-prog06.sh" # 58
    "agent-usb-prog80.sh" # 59
    "agent-usb-prog81.sh" # 60
    "agent-usb-prog96.sh" # 61
    "agent-usb-prog97.sh" # 62
    "agent-usb-prog98.sh" # 63
    "agent-usb-prog99.sh" # 64
    "agent-usb.ar5523-debug.sh" # 65
    "agent-usb.ar5523-prog01.sh" # 66
    "agent-usb.ar5523-prog02.sh" # 67
    "agent-usb.ar5523-prog03.sh" # 68
    "agent-usb.ar5523-prog04.sh" # 69
    "agent-usb.ar5523-prog05.sh" # 70
    "agent-usb.ar5523-prog06.sh" # 71
    "agent-usb.ar5523-prog80.sh" # 72
    "agent-usb.ar5523-prog81.sh" # 73
    "agent-usb.ar5523-prog96.sh" # 74
    "agent-usb.ar5523-prog97.sh" # 75
    "agent-usb.ar5523-prog98.sh" # 76
    "agent-usb.ar5523-prog99.sh" # 77
    "agent-usb.btusb-debug.sh" # 78
    "agent-usb.btusb-prog01.sh" # 79
    "agent-usb.btusb-prog02.sh" # 80
    "agent-usb.btusb-prog03.sh" # 81
    "agent-usb.btusb-prog04.sh" # 82
    "agent-usb.btusb-prog05.sh" # 83
    "agent-usb.btusb-prog06.sh" # 84
    "agent-usb.btusb-prog80.sh" # 85
    "agent-usb.btusb-prog81.sh" # 86
    "agent-usb.btusb-prog96.sh" # 87
    "agent-usb.btusb-prog97.sh" # 88
    "agent-usb.btusb-prog98.sh" # 89
    "agent-usb.btusb-prog99.sh" # 90
    "agent-usb.go7007-debug.sh" # 91
    "agent-usb.go7007-prog01.sh" # 92
    "agent-usb.go7007-prog02.sh" # 93
    "agent-usb.go7007-prog03.sh" # 94
    "agent-usb.go7007-prog04.sh" # 95
    "agent-usb.go7007-prog05.sh" # 96
    "agent-usb.go7007-prog06.sh" # 97
    "agent-usb.go7007-prog80.sh" # 98
    "agent-usb.go7007-prog81.sh" # 99
    "agent-usb.go7007-prog96.sh" # 100
    "agent-usb.go7007-prog97.sh" # 101
    "agent-usb.go7007-prog98.sh" # 102
    "agent-usb.go7007-prog99.sh" # 103
    "agent-usb.mwifiex-debug.sh" # 104
    "agent-usb.mwifiex-prog01.sh" # 105
    "agent-usb.mwifiex-prog02.sh" # 106
    "agent-usb.mwifiex-prog03.sh" # 107
    "agent-usb.mwifiex-prog04.sh" # 108
    "agent-usb.mwifiex-prog05.sh" # 109
    "agent-usb.mwifiex-prog06.sh" # 110
    "agent-usb.mwifiex-prog80.sh" # 111
    "agent-usb.mwifiex-prog81.sh" # 112
    "agent-usb.mwifiex-prog96.sh" # 113
    "agent-usb.mwifiex-prog97.sh" # 114
    "agent-usb.mwifiex-prog98.sh" # 115
    "agent-usb.mwifiex-prog99.sh" # 116
    "agent-usb.pn533-debug.sh" # 117
    "agent-usb.pn533-prog01.sh" # 118
    "agent-usb.pn533-prog02.sh" # 119
    "agent-usb.pn533-prog03.sh" # 120
    "agent-usb.pn533-prog04.sh" # 121
    "agent-usb.pn533-prog05.sh" # 122
    "agent-usb.pn533-prog06.sh" # 123
    "agent-usb.pn533-prog80.sh" # 124
    "agent-usb.pn533-prog81.sh" # 125
    "agent-usb.pn533-prog96.sh" # 126
    "agent-usb.pn533-prog97.sh" # 127
    "agent-usb.pn533-prog98.sh" # 128
    "agent-usb.pn533-prog99.sh" # 129
    "agent-usb.rsi-debug.sh" # 130
    "agent-usb.rsi-prog01.sh" # 131
    "agent-usb.rsi-prog02.sh" # 132
    "agent-usb.rsi-prog03.sh" # 133
    "agent-usb.rsi-prog04.sh" # 134
    "agent-usb.rsi-prog05.sh" # 135
    "agent-usb.rsi-prog06.sh" # 136
    "agent-usb.rsi-prog80.sh" # 137
    "agent-usb.rsi-prog81.sh" # 138
    "agent-usb.rsi-prog96.sh" # 139
    "agent-usb.rsi-prog97.sh" # 140
    "agent-usb.rsi-prog98.sh" # 141
    "agent-usb.rsi-prog99.sh" # 142
    "agent-usb.si470x-debug.sh" # 143
    "agent-usb.si470x-prog01.sh" # 144
    "agent-usb.si470x-prog02.sh" # 145
    "agent-usb.si470x-prog03.sh" # 146
    "agent-usb.si470x-prog04.sh" # 147
    "agent-usb.si470x-prog05.sh" # 148
    "agent-usb.si470x-prog06.sh" # 149
    "agent-usb.si470x-prog80.sh" # 150
    "agent-usb.si470x-prog81.sh" # 151
    "agent-usb.si470x-prog96.sh" # 152
    "agent-usb.si470x-prog97.sh" # 153
    "agent-usb.si470x-prog98.sh" # 154
    "agent-usb.si470x-prog99.sh" # 155
    "agent-usb.usx2y-debug.sh" # 156
    "agent-usb.usx2y-prog01.sh" # 157
    "agent-usb.usx2y-prog02.sh" # 158
    "agent-usb.usx2y-prog03.sh" # 159
    "agent-usb.usx2y-prog04.sh" # 160
    "agent-usb.usx2y-prog05.sh" # 161
    "agent-usb.usx2y-prog06.sh" # 162
    "agent-usb.usx2y-prog80.sh" # 163
    "agent-usb.usx2y-prog81.sh" # 164
    "agent-usb.usx2y-prog96.sh" # 165
    "agent-usb.usx2y-prog97.sh" # 166
    "agent-usb.usx2y-prog98.sh" # 167
    "agent-usb.usx2y-prog99.sh" # 168
)

sleep 5

if [ $id -lt ${#progs[@]} ]; then
    /root/${progs[$id]}
else
    echo "Failed to find guest agent $id"
    /root/agent-exit AGENT_START_FAILURE
    exit 1
fi

/root/agent-exit
