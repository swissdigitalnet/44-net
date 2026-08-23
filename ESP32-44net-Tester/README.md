# ESP32 44net tester

A passive target for a 44net segment: it holds an address, answers on port 80
and a UDP port, and reports what arrived — including the source address each
packet carried.

Full description, provisioning, firewall rules and the OTA contract:
**[../docs/esp32-tester.md](../docs/esp32-tester.md)**

Source only. Firmware is built by GitHub Actions; tag `v*` publishes a release
with `esp32-44net-tester.bin` attached.

```
main/main.c      state machine: portal or station, then serve
main/netcfg.c    NVS credentials, WiFi bring-up, BOOT button
main/portal.c    open provisioning AP, DNS hijack, scan + save
main/tester.c    status server, /ui, POST /ota, UDP listener, arrivals ring
```

Configuration lives in `menuconfig` under **44net Tester**.
