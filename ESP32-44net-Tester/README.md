# ESP32 44net tester

A purpose-built target to put on your 44net segment and aim at.

[docs/testing.md](../docs/testing.md) needs one thing it does not supply:

> *"Something on the 44net segment to aim at. Before your real node moves there, any spare host with an address in the range will do."*

"Any spare host" in practice means a Linux box with `tcpdump`, `nc` and a
console — sitting on public address space, for as long as you are testing. This
is the small, cheap alternative: an ESP32 that holds an address on the segment,
answers, and tells you what arrived.

Binaries come from
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

**DHCP, and usually nothing more.** The firmware carries no addresses.

It is tempting to pin it with a static lease on the router, and for most things
on a 44net segment that is right — see [which devices need a fixed
address](../docs/remote-station.md#which-devices-need-a-fixed-address-and-which-do-not).
**This device is the exception.** Nothing outside names its address: you find
it by sweeping the range and recognising the `id=` it reports about itself, and
the accept rules on a small allocation are normally scoped to the segment
rather than to one host.

A reservation would buy you nothing and cost you something real — it is the
only reason swapping the board would need router access as well as physical
access, and a replacement that lands on an unexpected address is exactly the
failure the reservation was supposed to prevent.

Pin it only if a per-host firewall rule names it. Then the rule and the
reservation must move together.

## What it serves

```
GET  /         text/plain, for curl and for reading
GET  /ui       the same data, phone-shaped, refreshing every 10 s
GET  /run      run the active checks now and show the verdicts
GET  /update   fetch the latest release and install it
```

All four are on **`CONFIG_TESTER_HTTP_PORT`, default 8044** — deliberately not
80. Port 80 is the most heavily scanned port on the internet and a 44net
allocation is public space. This is not concealment and it is not a substitute
for keeping the port closed except during a test; see
[the test-window section of testing.md](../docs/testing.md#opening-a-port-for-the-test-and-closing-it-again).

`/ui` exists to be read on a handset joined to the same segment, standing next
to the device: dark, large type, no horizontal scroll, and the caller's address
called out at the top because that is the field worth looking at.

**It is not a captive portal.** The automatic sheet depends on being the DHCP
server and hijacking DNS, which the device only is while running its own AP. As
a station it is neither, so you reach `/ui` by typing its address. Its own
address is on the status page, and the runner in
[the test container](../test-container/README.md) will find it for you.

**Client isolation must be off** on the SSID, or a phone on the same network
cannot reach it at all — the frames are dropped at the access point before they
reach the air. That is a deliberate trade: isolation protects an
internet-exposed device from its neighbours, and turning it off is what lets
you read the page from a phone.

```
you=203.0.113.9:41022
id=esp32-44net-tester
version=v0.5.2
ota=valid
update=not attempted
uptime=1234s
ip=<dhcp address>
rssi=-52
heap=214032
udp_port=5000
http_port=8044

arrivals (last 16):
  udp 203.0.113.9:41022 -> :5000 x3 12s ago
  tcp 198.51.100.7:52637 -> :8044 x1 48s ago
```

It also listens on **UDP** (`CONFIG_TESTER_UDP_PORT`, default 5000), because
test 4 of the procedure sends a UDP probe and a UDP probe never reaches an HTTP
server. Without that listener you would still need `tcpdump` on the segment.

The arrivals ring is fixed at 16 entries and deduplicated by source. Your
allocation is scanned continuously — an unbounded log is a memory-exhaustion
bug waiting to be triggered by someone else. Only packet headers are recorded;
payload is attacker-controlled and is discarded unread. Output is plaintext
throughout, so there is nothing to escape.

**No TLS on what it serves.** (It is a TLS *client* when it pulls firmware —
that is the direction where a certificate can actually be validated.) No
certificate can be valid for a bare address, so every visit
would train you to click through a browser warning; mbedTLS costs flash and
RAM on a device that is scanned continuously, and TLS handshakes are a cheap
way to exhaust an ESP32. The content is a reachability echo with no secrets in
it. Most importantly, TLS would put handshake debugging on top of the
reachability debugging this device exists to make unambiguous.

## Updating it

**Prove the update path works before you remove the one you already have.**
This project did the opposite: the push endpoint was deleted in the same
release that introduced pulling, the pull turned out to be broken, and the
deployed device was left with no remote update path at all. It had to be
recovered with a cable. Deploy the new mechanism, confirm a real update lands,
and only then take the old one away.

**The device pulls; nothing is pushed to it.** `GET /update` makes it fetch
`CONFIG_TESTER_UPDATE_URL` — by default the latest GitHub release — over TLS,
write it, and reboot.

That direction is the whole point. A push endpoint is an inbound firmware-write
port on public address space, and a bearer token crossing plaintext HTTP does
not change what it is. Pulling needs **no inbound port at all**, so updating is
not a reason to open one.

`/update` is refused unless the caller is on the same segment, so even with a
[test window](../docs/testing.md#opening-a-port-for-the-test-and-closing-it-again) open, an update cannot be triggered from the
internet. What it does need is **outbound** internet from the segment. A site
whose 44net segment reaches only 44net cannot use this — flash over serial.

Two implementation details that cost real time to find:

**The transmit buffer has to be enlarged.** A release download redirects to a
signed storage URL well over a kilobyte long, and `esp_http_client`'s default
512-byte TX buffer cannot write that request line. The symptom is not an error:
the client completes TLS, reads a few kilobytes, closes the connection and
returns failure — which looks exactly like a network problem and is not.

**`update=` can report failure but never success.** The result string lives in
RAM and a successful update ends in a reboot, so after one it reads
`not attempted` again. That asymmetry is the right way round: the case you need
to diagnose remotely is the one where it did not work, and there the device is
still up to tell you. To confirm a success, watch `ota=` instead.

**Rollback is enabled and is the reason this is safe to do remotely.** A new
image marks itself valid only after it has joined WiFi, bound its listener and
held for 30 seconds. If it cannot, the bootloader restores the previous image
on the next boot and the old firmware — still reachable — is waiting for you to
try again.

`ota=` reports that state: `pending` means a freshly written image has booted
and not yet settled, and it is the only positive evidence that an update
actually landed.

## Firewall rules it needs

Two, both scoped as narrowly as the procedure recommends — placed above your
tunnel's catch-all drop:

```
accept  in-interface=<tunnel>  dst-address=<device>  proto=tcp  dst-port=8044
accept  in-interface=<tunnel>  dst-address=<device>  proto=udp  dst-port=<udp-port>
```

**And only while you are testing.** These are not permanent rules. Add them for
a session, remove them after, and confirm from outside that they are gone —
[the test-window section of testing.md](../docs/testing.md#opening-a-port-for-the-test-and-closing-it-again) is that discipline written down. Updating the
device needs no rule at all, because it pulls.

Never a subnet-wide accept, and do not add the segment to your LAN interface
list.

## Building

GitHub Actions builds every push and attaches the binary to a tag as a release.
There is no local toolchain to install and no build output in the repository.

```
git tag v0.5.2 && git push --tags     # produces a release with esp32-44net-tester.bin
```

Configuration lives in `menuconfig` under **44net Tester**: AP SSID, portal
timeout, join attempts, BOOT GPIO, UDP port, HTTP port, update URL, and the
three optional hosts the active checks aim at.

## What has been verified, and what has not

Confirmed working on real hardware, on an ESP32-WROOM-32:

```
you=192.0.2.10:1189      source echo, caller's real address
id=esp32-44net-tester
version=v0.5.2            git describe, injected at build time
ota=valid                 rollback confirmation completed
uptime=42s  rssi=-42  heap=207796  udp_port=5000

arrivals (last 16):
  tcp 192.0.2.10:1189  -> :8044  x1
  udp 192.0.2.20:49862 -> :5000  x1
```

The full update chain has been exercised end to end. `GET /update` on a device
running the current release made it fetch roughly a megabyte from GitHub
through the redirect chain over TLS, write it, and reboot:

```
t+12s   version=v0.5.2  ota=valid       before
t+24s   version=v0.5.2  ota=pending     a freshly written image booted
t+60s   version=v0.5.2  ota=valid       settled and confirmed
```

`ota=pending` appears only when a newly written image boots, which is what
makes this evidence rather than inference. The version did not change because
the device fetched the release it was already running — the point was the
transport, not the payload. The rollback path is therefore live, not
theoretical.

**Also verified across a real gateway tunnel.** The device has held an address
on a 44net segment and answered a machine on the open internet, with the `you=`
echo showing that caller's real address — which is what separates a
reachability failure from a translation failure, and what the POP's source
filtering would otherwise hide.

## A word on leaving it running

In a twenty-second capture on a quiet `/28`, the address earmarked for this
device was already being probed on `:443` and `:23` — before the device
existed. Once it is up, the firewall lets exactly one or two ports through and
the firmware answers every request identically, but an ESP32 TCP stack has a
small connection table and sustained scanning can saturate it.

Fine as a deliberate test. Not something to leave on public space for months.
