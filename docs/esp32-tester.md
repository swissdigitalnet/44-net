# A purpose-built target for the 44net segment

[docs/test-procedure.md](test-procedure.md) needs one thing it does not supply:

> *"Something on the 44net segment to aim at. Before your real node moves there, any spare host with an address in the range will do."*

"Any spare host" in practice means a Linux box with `tcpdump`, `nc` and a
console — sitting on public address space, for as long as you are testing. This
is the small, cheap alternative: an ESP32 that holds an address on the segment,
answers, and tells you what arrived.

Source: [`ESP32-44net-Tester/`](../ESP32-44net-Tester). Binaries come from
GitHub Actions; the repository carries no build output.

## What it does and does not do

**It is passive.** It answers; it never probes. It will not tell you that your
isolation rules hold — only a probe from the segment can do that, and this
device deliberately does not make them. What it gives you is the other half:
proof that a packet arrived, and **what source address it carried**.

That last part is the reason it exists. A reachability failure and a
source-translation failure look identical from outside — both are silence. The
device reports the caller's address as it saw it, which separates them.

## Hardware

Any ESP32 with 2.4 GHz WiFi. The reference target is a plain **ESP32
WROOM-32**; 4 MB flash, no PSRAM, USB power. Nothing here stresses the part.

## Getting it onto the network

There is no WiFi configuration in the firmware. On first boot, or when it
cannot join the network it has stored, it raises an **open** access point
called `44net-tester-setup` and serves a captive portal. Pick your network
from the scan list, or type an SSID that is hidden or out of range, and it
stores the credentials and reboots.

**To re-provision it, move it out of range of its current network.** It tries
three times at ten seconds each, so roughly forty seconds after power-on it
gives up and opens the portal. That is the intended path, and it is the one
that works — carrying the device to a new site is exactly the case it handles.

The portal is open on purpose: it exists only in short recovery windows, and
its only capability is joining a WiFi network. If that trade is wrong for your
site, set a passphrase in `menuconfig` under **44net Tester**.

**The portal window is bounded.** After `CONFIG_TESTER_PORTAL_TIMEOUT_S`
(default 600 s) it reboots and retries whatever is stored. This matters at a
remote site: a transient AP outage — an access point rebooting, a radio being
reconfigured — must not leave the device parked in AP mode where nothing on
44net can reach it. Once it is connected it reconnects with backoff and never
falls back to the portal, for the same reason.

The consequence to know: if the passphrase is genuinely wrong, it cycles
portal → reboot → portal every ten minutes, so you have to catch it during an
AP window. You will see it never associating on your access point.

### Forgetting the network

**Hold the BOOT button for three seconds while it is running.** It erases the
stored credentials and reboots straight into the provisioning portal. Progress
is logged at one and two seconds, and releasing before three cancels it.

**Do not hold it during reset.** The ROM bootloader samples GPIO0 at reset and
enters serial download mode when it is low, so a button read at reset can never
be seen by the application at all. That is why this is watched continuously
during operation instead — a task polls the pin every 100 ms for the lifetime
of the device, in every mode.

The other way to re-provision is simply to move it out of range: it tries three
times at ten seconds each, so about forty seconds later it opens the portal on
its own.

## Addressing

**DHCP.** The firmware carries no addresses. Give it a fixed address with a
static lease on the router, keyed to its MAC, so the address, the reservation
and the firewall rule all live in one place. A static lease may sit inside or
outside the pool.

This is the same argument [enabling-44net.md](enabling-44net.md) makes for
AREDN nodes: nothing in the device's own configuration is site-specific, and
there is nothing for a firmware update to discard.

## What it serves

```
GET  /      text/plain, for curl and for reading
GET  /ui    the same content as HTML with a meta-refresh
POST /ota   firmware update command
```

```
you=203.0.113.9:41022
id=esp32-44net-tester
version=v0.3.0
ota=valid
uptime=1234s
ip=<dhcp address>
rssi=-52
heap=214032
udp_port=5000

arrivals (last 16):
  udp 203.0.113.9:41022 -> :5000 x3 12s ago
  tcp 198.51.100.7:52637 -> :80 x1 48s ago
```

It also listens on **UDP** (`CONFIG_TESTER_UDP_PORT`, default 5000), because
test 4 of the procedure sends a UDP probe and a UDP probe never reaches an HTTP
server. Without that listener you would still need `tcpdump` on the segment.

The arrivals ring is fixed at 16 entries and deduplicated by source. Your
allocation is scanned continuously — an unbounded log is a memory-exhaustion
bug waiting to be triggered by someone else. Only packet headers are recorded;
payload is attacker-controlled and is discarded unread. Output is plaintext
throughout, so there is nothing to escape.

**No TLS.** No certificate can be valid for a bare address, so every visit
would train you to click through a browser warning; mbedTLS costs flash and
RAM on a device that is scanned continuously, and TLS handshakes are a cheap
way to exhaust an ESP32. The content is a reachability echo with no secrets in
it. Most importantly, TLS would put handshake debugging on top of the
reachability debugging this device exists to make unambiguous.

## Updating it

The device offers the update; something outside initiates it. That keeps the
44net segment free of any outbound internet permission — see
[test-container.md](test-container.md), which fetches the latest release and
pushes it.

```
POST /ota
Authorization: Bearer <CONFIG_TESTER_OTA_TOKEN>
Content-Type: application/octet-stream
<firmware.bin>
```

**Scope the firewall rule for this port to the source address of your update
host.** That is the access control. The token crosses plaintext HTTP and only
protects against a mistake — not against anyone who can observe or spoof.

**Rollback is enabled and is the reason this is safe to do remotely.** A pushed
image marks itself valid only after it has joined WiFi, bound its listener and
held for 30 seconds. If it cannot, the bootloader restores the previous image
on the next boot and the old firmware — still reachable — is waiting for you to
try again. Without this, one bad push means a visit to the site.

`ota=` reports that state, so `pending` means a push has not yet settled.

## Firewall rules it needs

Two, both scoped as narrowly as the procedure recommends — placed above your
tunnel's catch-all drop:

```
accept  in-interface=<tunnel>  dst-address=<device>  proto=tcp  dst-port=80
accept  in-interface=<tunnel>  dst-address=<device>  proto=udp  dst-port=<udp-port>
```

and, only if you use OTA:

```
accept  in-interface=<tunnel>  dst-address=<device>  proto=tcp  dst-port=80  src-address=<update-host>
```

Never a subnet-wide accept, and do not add the segment to your LAN interface
list.

## Building

GitHub Actions builds every push and attaches the binary to a tag as a release.
There is no local toolchain to install and no build output in the repository.

```
git tag v0.3.0 && git push --tags     # produces a release with esp32-44net-tester.bin
```

Configuration lives in `menuconfig` under **44net Tester**: AP SSID, portal
timeout, join attempts, BOOT GPIO, UDP port, OTA enable and token.

## What has been verified, and what has not

Confirmed working on real hardware, on an ESP32-WROOM-32:

```
you=192.0.2.6:1189      source echo, caller's real address
id=esp32-44net-tester
version=v0.1.0            git describe, injected at build time
ota=valid                 rollback confirmation completed
uptime=42s  rssi=-42  heap=207796  udp_port=5000

arrivals (last 16):
  tcp 192.0.2.6:1189  -> :80    x1
  udp 192.0.2.160:49862 -> :5000  x1
```

The full update chain has been exercised end to end: CI built a tagged release,
the container fetched it, pushed it over the air, and the device came back
reporting the new version and moved from `ota=pending` to `ota=valid` after its
settle period. The rollback path is therefore live, not theoretical.

**Not yet verified: the device has only ever been tested from inside a LAN.**
Everything above proves the firmware. None of it proves the path across a
gateway tunnel — the `you=` echo showing an internet-sourced address, the
inbound firewall rule, or the POP's source filtering. That needs the device on
a real 44net segment with a machine outside your network probing it.

## A word on leaving it running

In a twenty-second capture on a quiet `/28`, the address earmarked for this
device was already being probed on `:443` and `:23` — before the device
existed. Once it is up, the firewall lets exactly one or two ports through and
the firmware answers every request identically, but an ESP32 TCP stack has a
small connection table and sustained scanning can saturate it.

Fine as a deliberate test. Not something to leave on public space for months.
