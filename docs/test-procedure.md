# Test procedure

Proves two things: paths that should be open are open, and paths that should be closed are
closed. Reading the configuration is not evidence — every test below sends real traffic and
checks where it lands.

Run this after any firewall or routing change on the 44-net path.

## What you need

- SSH to the router as `admin`
- SSH to the Proxmox host `<hypervisor>` as root
- A machine on the internal network, VLAN 100 — `dev-1` at `<lan-host>` below
- **A machine outside the house.** Essential. A machine on the home network reaches
  `44.xx.xx.xx/28` directly over VLAN 44 and never crosses the tunnel, so it cannot test the
  inbound path at all. `<external-host>` is used below.

Confirm the external machine really routes to the allocation over the internet:

```bash
ssh <external-host> "ip route get 44.xx.xx.xx"
# expect: via <provider gateway> dev eth0 — NOT via any local tunnel
```

## Test rig

Until the AREDN systems move, nothing exists on VLAN 44. Give the Proxmox host a temporary
address there to act as a stand-in. All of this is runtime-only; nothing is written to
`/etc/network/interfaces`.

```bash
ssh root@<hypervisor> "
  ip link add link eno1 name eno1.44 type vlan id 44
  ip addr add 44.xx.xx.xx/28 dev eno1.44
  ip link set eno1.44 up
"
```

Tests 7 and 8 additionally need a route forcing internal traffic through the router,
added and removed within those tests. Without it the host reaches internal addresses over
its own LAN connection, the traffic never passes the router, and the isolation rule is never
exercised — the test would pass while proving nothing.

## Baseline counters

Take these before testing and compare after. They are what attributes a result to a specific
rule.

```
:foreach r in=[/ip/firewall/filter/find where \
  comment~"AREDN supernode tunnels" or comment~"AREDN wireguard tunnels" or \
  comment~"AREDN vtun" or comment~"Drop new connections from 44net" or \
  comment~"cannot initiate into internal"] \
  do={:put ([/ip/firewall/filter/get $r comment]." = ".[/ip/firewall/filter/get $r packets])}
```

The tunnel drop counter climbs on its own from internet scanning, so judge it by behaviour
rather than by exact numbers. The accept counters and the isolation counter are quiet and
give exact attribution.

## Open paths — must work

### 1. Gateway reachable from the public segment

```bash
ssh root@<hypervisor> "ping -c 3 -W 3 -I 44.xx.xx.xx 44.xx.xx.xx"
```

Expect 3/3, well under a millisecond.

### 2. Public segment reachable from the router

```
/ping 44.xx.xx.xx count=3
```

Expect 3/3.

### 3. Internal network reaches the public segment

VLAN 100 and VLAN 44 are separate networks joined only by the router. The internal side may
start connections to the public segment; the reverse may not (test 7). Run from a VLAN 100
machine:

```bash
ping -c 3 -W 3 44.xx.xx.xx
```

Expect 3/3. This is what keeps internal services working once the AREDN systems move —
they remain reachable, at their new addresses.

### 4. Source address survives that crossing

No translation happens between the two VLANs. Capture on the public segment while pinging
from VLAN 100:

```bash
ssh root@<hypervisor> "(setsid timeout 12 tcpdump -nni eno1.44 -c 2 icmp > /tmp/src.txt 2>&1 &)"
ping -c 3 -W 2 44.xx.xx.xx >/dev/null
ssh root@<hypervisor> "grep 'IP ' /tmp/src.txt | head -2"
```

Expect the real internal address, not the router's:

```
IP <lan-host> > 44.xx.xx.xx: ICMP echo request
```

An AREDN system therefore sees which internal client is talking to it. If the router's own
address appears instead, something is masquerading that should not be.

### 5. Allowed port from outside, delivered end to end

Start a capture on the stand-in, then send from the external machine:

```bash
ssh root@<hypervisor> "(setsid timeout 18 tcpdump -nni eno1.44 -c 3 'udp port 6526' > /tmp/vlan44.txt 2>&1 &)"
ssh <external-host> "for i in 1 2 3; do echo probe\$i | timeout 4 nc -u -w 1 44.xx.xx.xx 6526; done"
ssh root@<hypervisor> "cat /tmp/vlan44.txt"
```

Expect three packets from the external machine's public address arriving at
`44.xx.xx.xx.6526`, and the `AREDN supernode tunnels` counter up by three. This is the whole
path: internet, gateway, tunnel, router, VLAN 44.

### 6. Allowed port to an address with nothing on it

```bash
ssh <external-host> "timeout 8 nc -nzv -w 6 44.xx.xx.xx 5525"
```

Expect `No route to host` and the `AREDN vtun` counter to rise. That error is the *success*
condition here: the router accepted the packet, tried to deliver it on VLAN 44, and found
nothing at that address. The firewall did its job.

## Closed paths — must fail

### 7. Public segment into the internal network

The reverse of test 3, and the rule that makes VLAN 44 a DMZ.

```bash
ssh root@<hypervisor> "
  ip route add <lan-host>/32 via 44.xx.xx.xx dev eno1.44 src 44.xx.xx.xx
  ping -c 3 -W 3 -I 44.xx.xx.xx <lan-host> 2>&1 | tail -3
  ip route del <lan-host>/32
"
```

Expect 100% loss, and the `cannot initiate into internal VLANs` counter up by exactly three.
Exact attribution matters — 0/3 with no counter movement would mean the traffic never
reached the rule and the test proved nothing.

Tests 3 and 7 together are the point: traffic flows inward to the public segment and not
back out of it, while replies to internally-started connections still work because only
`connection-state=new` is dropped.

### 8. Router services from the public segment

```bash
ssh root@<hypervisor> "
  timeout 8 nc -nzv -w 6 -s 44.xx.xx.xx 44.xx.xx.xx 22
  timeout 8 nc -nzv -w 6 -s 44.xx.xx.xx 44.xx.xx.xx 8291
  timeout 8 nc -nzv -w 6 -s 44.xx.xx.xx 44.xx.xx.xx 53
"
```

SSH, WinBox and DNS must all time out.

### 9. Blocked ports from outside

```bash
ssh <external-host> "timeout 8 nc -nzv -w 6 44.xx.xx.xx 80"
ssh <external-host> "timeout 8 nc -nzv -w 6 44.xx.xx.xx 22"
```

Expect both to time out with no reply. Silence is correct — a refusal would tell a scanner
something is there.

Note the contrast with test 6: an allowed port to an empty address answers `No route to
host`, a blocked port to a live address says nothing at all. That difference is the proof
the firewall discriminates by port and not merely by host.

## Tear down

```bash
ssh root@<hypervisor> "
  ip link del eno1.44
  rm -f /tmp/vlan44.txt /tmp/src.txt
"
```

Confirm the host is back to its normal VLAN interfaces — one `eno1.<tag>` and one
`vmbr0v<tag>` per VLAN actually in use, with no `.44`:

```bash
ssh root@<hypervisor> "ls /sys/class/net/ | grep -E 'eno1\.|vmbr0v' | sort | tr '\n' ' '"
ssh root@<hypervisor> "ip route get <lan-host>"
```

The route must come back via `vmbr0`, not via `44.xx.xx.xx`.

## Summary

| # | Path | Expected |
|---|---|---|
| 1 | VLAN 44 → gateway | reachable |
| 2 | Router → VLAN 44 | reachable |
| 3 | VLAN 100 → VLAN 44 | reachable |
| 4 | VLAN 100 → VLAN 44, source address | real internal address, not translated |
| 5 | Outside → `.82` udp 6526 | delivered, visible in capture |
| 6 | Outside → `.83` tcp 5525 | accepted, `No route to host` |
| 7 | VLAN 44 → VLAN 100 | dropped, counter matches exactly |
| 8 | VLAN 44 → router services | silent timeout |
| 9 | Outside → `.82` tcp 80 / 22 | silent timeout |
