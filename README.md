# 44net for amateur radio

![AREDN](https://img.shields.io/badge/AREDN-tunnel%20server%20%7C%20supernode-blue)
![44net](https://img.shields.io/badge/44net-44.0.0.0%2F8-orange)
![Router](https://img.shields.io/badge/router-RouterOS%20%7C%20OpenWrt-lightgrey)

Getting 44net address space to your station and actually using it. Written by operators who
have done it, for operators about to.

## 📻 First — you may need none of this

**If you only want to link to a node whose tunnel endpoint is a 44 address, there is nothing to
do.**

44net is globally routed public address space. Your node reaches `44.x.y.z` exactly as it
reaches any other internet address. Set the tunnel up normally. No allocation, no gateway, no
router changes.

This catches people out regularly. A peer having a 44 address does not mean you need 44
anything.

---

## 🧭 Which of these are you?

Three documents, each complete on its own. Read the one that matches what you are doing — you
should not need the others.

### [→ AREDN tunnel links over 44net](docs/aredn.md)

**You run an AREDN tunnel server or supernode**, and you want peers to reach it at a 44net
address instead of a port forward on your ISP connection.

Covers the allocation and gateway tunnel, router and firewall configuration, and the two things
that make this harder than it looks: **MTU**, because your peers' tunnels end up running inside
your gateway tunnel, and **per-peer migration**, because you can fix your own MTU in a minute
and theirs not at all. Includes a letter you can send them.

### [→ A remote station on 44net](docs/remote-station.md)

**Your station is somewhere you cannot reach** — a hilltop site, a repeater controller, a node
in a holiday house — because the connection is behind CGNAT and there is no port to forward.

Covers giving that station genuine public addresses of its own: the gateway tunnel, a segment
for the exposed devices, DHCP, firewall and DMZ isolation. Two chapters earn their place —
**`AllowedIPs` is a source filter**, which silently blocks every non-44net caller if you get it
wrong, and **the return path**, which is guaranteed to bite you on CGNAT and looks exactly like
a closed port.

### [→ Proving it works](docs/testing.md)

**You have built one of the above and want evidence**, not a configuration that reads
correctly.

Sends real traffic from outside your network and checks where it lands, including the checks
that must **fail**. Also covers opening a port for a test and having it close itself again,
because the step everyone skips is the last one.

---

## 🔧 Two tools in this repository

| | |
|---|---|
| [`ESP32-44net-Tester/`](ESP32-44net-Tester/README.md) | A small device that holds an address on your segment, answers, and reports **what source address each packet carried** — which is what separates a reachability failure from a translation failure |
| [`test-container/`](test-container/README.md) | The outside-in half of the test procedure, packaged to re-run after any change. Refuses to run if its own route to the target leaves via a tunnel, so it cannot quietly test from the wrong side |

## 🌐 About 44Net Connect

[connect.44net.cloud](https://connect.44net.cloud) is ARDC's service for delivering 44net
address space to people who have no other way to receive it — a WireGuard tunnel from your
router to a point of presence, with your allocation routed down it. Free, for licensed
amateurs, and it works from behind NAT and CGNAT.

One thing to know before you start, because it is the most common way to lose an evening:

> The official [Quick Start](https://wiki.ampr.org/wiki/44Net_Connect/Quick_Start) ends at
> "confirm handshake", which leaves you with a tunnel carrying a **single address**. Getting
> your **allocation** routed to you is a second step — *Networks* in the portal — that no
> official page describes. Skip it and the tunnel handshakes perfectly while your entire
> allocation stays unreachable. It looks like a broken tunnel and is not one.

Both operating documents above cover the portal in full.

## 🔗 References

- [AREDN](https://www.arednmesh.org/) — Amateur Radio Emergency Data Network
- [ARDC](https://www.ampr.org/) — allocates 44net space
- [44Net Connect](https://wiki.ampr.org/wiki/44Net_Connect) — the WireGuard gateway service
- [44Net Wiki](https://wiki.ampr.org/wiki/Main_Page)
- [AREDN issue #2594](https://github.com/aredn/aredn/issues/2594) — the MTU behaviour, still open

## Contributing

Corrections welcome, particularly on the 44Net Connect portal — parts of it are undocumented
publicly and are described here from observed behaviour rather than from a specification.
