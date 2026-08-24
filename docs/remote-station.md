# A remote station on 44net

You have a station somewhere you cannot reach: a remote HF setup at a hilltop
site, a repeater controller, a node in a holiday house. It has internet. You
still cannot connect to it from home, because the connection gives you no
public address to connect *to*.

This document is about fixing that with 44net — giving the station **real
public addresses of its own**, independent of whatever the carrier does.

## Is this you?

The tell is that port forwarding is not an option. Not "difficult" — absent.
There is no port to forward, because the address on your router's WAN is not
the address the world sees.

| What you have | Why there is no way in |
|---|---|
| Cellular / LTE / 5G | The carrier hands you a private address and shares one public address across many subscribers |
| Cable or DSL with CGNAT | Same, increasingly the default for new connections |
| A landlord's or club's shared connection | Someone else's router, and you do not administer it |
| Starlink | CGNAT by default |

Check it in thirty seconds — compare what your router thinks its address is
with what the internet sees:

```sh
# on the router
ip addr show <wan-interface> | grep 'inet '
# from the same router, or anything behind it
curl -s https://api.ipify.org; echo
```

Different answers, or a WAN address starting `10.`, `192.168.`, or in
`100.64.0.0/10`, means you are behind carrier NAT. `100.64` is the giveaway —
that range exists specifically for this.

## Why 44net rather than a tunnel to a VPS

The usual answers work, and it is worth being honest about why you might still
prefer them:

| Approach | What it gives you | What it costs |
|---|---|---|
| Tailscale, ZeroTier | Easy, encrypted, works today | Every device that wants in needs the client and your account. The station is not *addressable* — only reachable by members |
| Reverse SSH / VPN to a VPS | Full control | You run and pay for the VPS; it is a single point of failure; forwarding more than a couple of ports gets tedious |
| **44net** | The station holds genuine public addresses. Anything on the internet can reach it, with no client software and nobody's permission | An allocation, a gateway tunnel, and real router work — including the return-path problem below |

The distinction that matters: the first two make the station *reachable by
you*. 44net makes it **addressable by anyone** — which is what you want for a
node other operators link to, and overkill for a box only you log into.

It also means the station sits on **public address space that is scanned
continuously**. That is not a reason to avoid it; it is a reason to read the
firewall section rather than skim it.

## What you need

| | |
|---|---|
| An allocation | Requested from [ARDC](https://www.ampr.org/). A `/28` gives 13 usable addresses |
| A 44Net Portal account | [portal.ampr.org](https://portal.ampr.org), callsign verified |
| A router at the station | Must do WireGuard **and** policy routing. OpenWrt or RutOS (Teltonika), RouterOS, pfSense, or a Linux box |

That second requirement is not negotiable and is the reason a consumer router
usually will not do. See [the return path](#the-return-path-the-part-that-will-defeat-you).

## In the 44Net Connect portal

[connect.44net.cloud](https://connect.44net.cloud) is ARDC's service for
delivering 44net address space to people who have no other way to receive it.
It builds a WireGuard tunnel from your router to a point of presence and routes
your allocation down it — and **it is designed to work from behind NAT and
CGNAT**, which is exactly your situation. Free, for licensed amateurs,
best-effort.

What it is not, in ARDC's own words: *"The WireGuard tunnel is transport, not
cover. It is not designed for privacy or anonymity."* It is not a VPN. Your
allocation becomes publicly routed and publicly visible.

**Generate the WireGuard keypair on the router first.** The portal will hand
you a complete configuration including a private key. That is convenient, and
it means your private key was generated elsewhere and travelled to you over the
web. Paste in a public key instead.

### Two steps, not one

The [Quick Start](https://wiki.ampr.org/wiki/44Net_Connect/Quick_Start) ends at
"confirm handshake". Follow it exactly and you have a tunnel carrying **one
address** — fine for a single machine, useless for a station with several
devices on it.

**1 · Tunnels.** Create a tunnel, choose a region and endpoint node (it carries
all your 44net traffic — pick one near the station, not near you), and paste
your **public key**. You get back a wg-quick configuration with the three
values the router needs:

| From the portal | Used as |
|---|---|
| Gateway public key | the peer's public key |
| Endpoint address and port | the peer's endpoint |
| An address for your tunnel interface | a `/32` on the tunnel interface |

**2 · Networks.** Associate your allocated prefix with that tunnel, so the
gateway routes it down to you.

**Skipping the second step is the classic failure.** The tunnel handshakes on
schedule and looks healthy while nothing is routed to you. It presents as a
broken tunnel and is not one.

> The Networks page requires a login and is not covered by the public wiki, so
> this is inferred from the resulting configuration. Corrections welcome.

## On the router

Written for OpenWrt / RutOS, since that is what most remote-station routers
run. RouterOS notes follow each part.

### 1. The gateway tunnel

```sh
uci set network.wg44=interface
uci set network.wg44.proto='wireguard'
uci set network.wg44.private_key='<your private key>'
uci set network.wg44.mtu='1420'
uci add_list network.wg44.addresses='44.xx.yy.9/32'   # from the portal

uci set network.wg44_peer=wireguard_wg44
uci set network.wg44_peer.public_key='<gateway public key>'
uci set network.wg44_peer.endpoint_host='<gateway address>'
uci set network.wg44_peer.endpoint_port='<gateway port>'
uci set network.wg44_peer.persistent_keepalive='25'
```

`persistent_keepalive` is doing real work here. Behind CGNAT the carrier holds
a translation entry only while traffic flows; let it expire and inbound packets
have nowhere to go. The keepalive is what keeps your station reachable when it
is idle, which is most of the time.

### 2. `AllowedIPs` is a source filter, not just a route

This one costs an afternoon, so it gets its own section.

```sh
uci add_list network.wg44_peer.allowed_ips='0.0.0.0/0'
uci set network.wg44_peer.route_allowed_ips='0'
```

It is natural to set `allowed_ips` to your own prefix — it is *your* address
space, after all. **Do not.** WireGuard uses `AllowedIPs` in both directions:
outbound it selects routes, but inbound it is a **source filter**. A packet
arriving on the tunnel whose source is not listed is silently discarded before
anything else sees it.

Callers from the open internet have ordinary internet addresses. Narrow the
list to `44.0.0.0/9` and you have built a station that only other 44net hosts
can reach, while every normal internet user is dropped — with no log, no
counter, and a firewall that reads perfectly correctly.

So the list must be `0.0.0.0/0`. And because it is, **`route_allowed_ips` must
be `0`**, or the router installs a default route into the tunnel and sends all
its ordinary traffic down it too.

> On RouterOS the same field is `allowed-address=0.0.0.0/0` on the peer, and
> RouterOS does not auto-install routes, so there is no second setting to
> disable. You add the routes yourself in the next step.

### 3. Routes

```sh
uci set network.route44=route
uci set network.route44.interface='wg44'
uci set network.route44.target='44.0.0.0/9'

uci set network.route44b=route
uci set network.route44b.interface='wg44'
uci set network.route44b.target='44.128.0.0/10'

# the gateway's own address must NOT route into the tunnel
uci set network.routegw=route
uci set network.routegw.interface='<wan>'
uci set network.routegw.target='<gateway address>/32'
uci set network.routegw.gateway='<your wan gateway>'
```

That last one is not optional. The gateway's public address lies inside
`44.0.0.0/9`, so without a more specific route the tunnel's own traffic is
routed into itself and it dies the moment the other routes appear.

`44.192.0.0/10` is deliberately absent — that block was sold and is not 44net.

### 4. A segment for the 44net devices

Give the 44net addresses their own bridge and their own subnet. Not your LAN.

```sh
uci set network.lan44=interface
uci set network.lan44.proto='static'
uci set network.lan44.device='br-lan44'
uci set network.lan44.ipaddr='44.xx.yy.65'      # the router's own address
uci set network.lan44.netmask='255.255.255.240' # a /28

uci set dhcp.lan44=dhcp
uci set dhcp.lan44.interface='lan44'
uci set dhcp.lan44.start='4'
uci set dhcp.lan44.limit='8'
```

Separate on purpose. Everything on this segment is exposed to the internet by
design, so it must not share a broadcast domain with the machines that are not.
That separation is what the isolation rule below enforces, and you cannot
enforce it if they are on the same bridge.

**Give each device a static DHCP lease rather than a static address:**

```sh
uci add dhcp host
uci set dhcp.@host[-1].mac='<device mac>'
uci set dhcp.@host[-1].ip='44.xx.yy.72'
uci set dhcp.@host[-1].name='station-pc'
```

The address, the reservation and the firewall rule then all live in one place —
the router. Nothing on the device is site-specific, so a rebuild, a firmware
upgrade or a swapped SD card does not lose its addressing.

## The return path: the part that will defeat you

Everything so far lets traffic *in*. This is about getting the answer back
out, and at a CGNAT site it is guaranteed to bite you.

**The symptom is distinctive and misleading: requests visibly arrive and
nothing comes back.** A capture on the tunnel shows the caller's SYNs, retried
three or four times. The device answers fine from inside. Every firewall rule
reads correctly. From outside it looks like a closed port.

### Why

Your allocation is routed to you down the tunnel, so a request from the
internet arrives on that interface. But the caller's address is an ordinary
internet address — **not** in `44.0.0.0/9`.

So when the reply is routed, your 44net routes do not match it. It follows the
default route instead, out of the WAN. What happens next decides whether you
ever notice:

| WAN | What happens to the reply |
|---|---|
| An ISP with a real address and no NAT on that source | Leaves with its `44.x` source. Asymmetric, but the caller sees a valid answer. Usually works |
| **CGNAT — cellular, Starlink, most modern connections** | The carrier rewrites the source. The caller receives an answer from a machine it never contacted, and discards it. **Dead** |

That is why the same configuration works at one site and fails at another for
no visible reason, and why a remote station needs this section and a house on a
normal ISP connection often does not.

### The fix

Replies must leave the way the request arrived. Mark connections arriving on
the tunnel, restore that mark on replies coming back from the segment, and
route marked packets into the tunnel.

`/etc/44net-reply-routing.sh`:

```sh
#!/bin/sh
TABLE=44
MARK=0x44
SEGMENT_IP=44.xx.yy.65        # the router's own address on the segment

ip route replace default dev wg44 src $SEGMENT_IP table $TABLE
ip rule del fwmark $MARK lookup $TABLE 2>/dev/null
ip rule add fwmark $MARK lookup $TABLE

iptables -t mangle -A PREROUTING -i wg44 \
    -m conntrack --ctstate NEW -j CONNMARK --set-mark $MARK
iptables -t mangle -A PREROUTING -i br-lan44 -j CONNMARK --restore-mark
```

Register it as a firewall include so it survives a reboot and a firewall
reload:

```sh
uci set firewall.reply_routing=include
uci set firewall.reply_routing.path='/etc/44net-reply-routing.sh'
uci set firewall.reply_routing.reload='1'
uci commit firewall
```

Then verify it by **deleting the rules and reloading**, not by assuming. An
include that is registered but never fires looks identical to one that works,
right up until the next reboot.

> On RouterOS the same shape is `/ip firewall mangle` with `mark-connection`
> and `mark-routing`, plus a routing table containing only the tunnel.

### `src` is not optional

Without it, a packet the **router itself** generates takes its source from the
global default route — the carrier's CGNAT address — and the gateway drops it
on the source filter described below. You get a timeout and no clue why.

### What to leave broken, on purpose

It is tempting to add this so that locally generated packets go back through
the tunnel too:

```sh
iptables -t mangle -A OUTPUT -j CONNMARK --restore-mark    # do NOT
```

**Don't.** The packets it affects are mostly ICMP errors — in particular the
*host unreachable* the router emits when it accepts a packet for the segment
and finds nobody holding that address. Delivering those makes an empty address
distinguishable from a blocked one, handing a scanner a map of which parts of
your allocation are live.

Silence is worth more than a passing reachability test. The cost is that one
check in [testing.md](testing.md) can no longer tell "accepted but empty" from
"dropped" — accept that, and prove range-wide reachability by putting a second
device on the segment instead.

## The gateway source-filters what you send

Undocumented, and it presents as an unexplained timeout: **the gateway drops
packets you send whose source address is outside your allocation.**

This is easy to prove and worth proving, because it eliminates a whole class of
wrong theories. Two pings, identical but for the source:

```sh
ping -c 5 -I 44.xx.yy.65 <some 44net host>    # 0% loss
ping -c 5 <some 44net host>                    # 100% loss
```

Anything your router originates towards 44net must carry a 44net source. That
is what the `src` in the reply-routing script is for.

## Firewall

Your allocation is public space, scanned continuously, and everything on the
segment is exposed by design. Three things to get right.

**Default deny inbound, accept only what is actually served.**

```sh
uci set firewall.r_station=rule
uci set firewall.r_station.src='wg44'
uci set firewall.r_station.dest='lan44'
uci set firewall.r_station.dest_ip='44.xx.yy.72'
uci set firewall.r_station.proto='tcp'
uci set firewall.r_station.dest_port='<port>'
uci set firewall.r_station.target='ACCEPT'
```

Never a subnet-wide accept. A blanket rule covers every address you have not
filled yet, and publishes the admin interface of whatever lands there next.

**The segment must not reach your internal network.** This is what makes it a
DMZ rather than an extension of your LAN:

```sh
uci set firewall.r_isolate=rule
uci set firewall.r_isolate.src='lan44'
uci set firewall.r_isolate.dest='lan'
uci set firewall.r_isolate.target='DROP'
```

**Check what your zone policy actually does.** A zone whose forward policy is
`DROP` may not drop what you think. On fw3 the generated chain is:

```
-A zone_wg44_dest_DROP -o wg44 -j DROP
```

Note `-o`: that matches traffic heading **out** toward the tunnel. Traffic
arriving **from** the tunnel and not matched by an accept falls straight
through and lands on the global fallback:

```
-A FORWARD -j reject
-A reject -p tcp -j REJECT --reject-with tcp-reset
```

So every unmatched probe from the internet is answered with a TCP reset — a
refusal tells a scanner something is there; silence tells it nothing. Add an
explicit drop and confirm it lands *after* your accepts:

```sh
uci set firewall.r_drop_rest=rule
uci set firewall.r_drop_rest.src='wg44'
uci set firewall.r_drop_rest.dest='lan44'
uci set firewall.r_drop_rest.target='DROP'
```

This one hides well. The resets only become visible once the return path works —
before that they were being generated all along and quietly lost on the WAN.

## Should the segment reach the internet outbound?

A reasonable thing to want: the devices are on public addresses, so letting
them fetch updates or an NTP server seems harmless.

It is roughly as safe as a guest network, and the same reasoning applies.
Connection tracking means only replies to connections the device started come
back, so it opens no inbound path. What it does mean is that a **compromised
device can reach out** — to fetch a payload, or to phone home.

Given the segment exists precisely to hold internet-exposed devices, that is a
real consideration and not a theoretical one. Two sensible positions:

- **Allow it**, if something on the segment genuinely needs it — a device that
  updates its own firmware, for instance, which is how the
  [ESP32 tester](../ESP32-44net-Tester/README.md) avoids needing an inbound
  update port at all.
- **Deny it**, if nothing does. It is the tighter default and costs nothing
  when unused.

Decide deliberately rather than inheriting whatever the zone default happens to
be.

## Proving it works

Configuration that reads correctly is not evidence, and at a remote site you
cannot walk over and look. **[testing.md](testing.md)** is the procedure: it
sends real traffic from outside your network and checks where it lands,
including the checks that must *fail*.

Do it before you rely on the station, and again after any firewall change.

## Operating notes

**Ports are opened for tests and closed afterwards.** The normal state is
everything closed except what the station serves. See
[opening a port for the test](testing.md#opening-a-port-for-the-test-and-closing-it-again).

**Expect the scanning.** In a twenty-second capture on a quiet `/28`, an
address that had never been used was already being probed on `:443` and `:23`.
That is the background, and it is why every rule above is scoped to a single
address and port.

**The keepalive is load-bearing.** If the station becomes unreachable after a
period of quiet, look at `persistent_keepalive` before anything else — the
carrier's NAT entry expired and there is nothing on your side to blame.
