# Verifying your 44net firewall

Your allocation is public address space and is scanned continuously. Before you put anything on
it, prove two things: the paths you meant to open are open, and the paths you meant to close are
closed.

Reading the configuration is not proof. Every test below sends real traffic and checks where it
lands. Run it after any firewall or routing change.

## What you need

- A machine **outside your network**. This is not optional. Anything on your own LAN reaches
  your 44net segment directly and never crosses the gateway tunnel, so it cannot test the
  inbound path at all. A cheap VPS works.
- Access to your router's firewall counters.
- Something on the 44net segment to aim at. Before your real node moves there, any spare host
  with an address in the range will do.

Confirm your external machine really routes to your allocation over the internet:

```bash
ssh <external-host> "ip route get <your-44net-address>"
# expect: via <its own gateway> — NOT via any tunnel of yours
```

## Counters are the evidence

A ping that fails proves little on its own — it might never have reached the rule you think
blocked it. Take the counters before and after, so each result is attributable:

```
:foreach r in=[/ip/firewall/filter/find chain=forward] \
  do={:put ([/ip/firewall/filter/get $r comment]." = ".[/ip/firewall/filter/get $r packets])}
```

The tunnel's drop counter climbs on its own from internet scanning, so judge that one by
behaviour rather than exact numbers. Your accept rules are quiet and give exact attribution.

## Paths that must work

### 1. Router reaches the 44net segment

```
/ping <host-on-44net-segment> count=3
```

### 2. The segment reaches its gateway

From the host on the segment, ping the router's 44net address. Both directions confirm the
VLAN and the gateway address before anything else is in play.

### 3. Your LAN reaches the segment, with its real source address

From a LAN machine, ping the host on the 44net segment. Then capture on that host while
pinging:

```bash
tcpdump -nni <interface> icmp
```

Expect the LAN machine's **real** address, not the router's. If the router's address appears,
something is masquerading between your own VLANs that should not be.

### 4. An allowed port, delivered end to end

Capture on the target host, then send from the external machine:

```bash
# on the target
tcpdump -nni <interface> 'udp port <allowed-port>'
# from outside
echo probe | nc -u -w 1 <your-44net-address> <allowed-port>
```

Expect the packet to arrive, and the matching accept counter to rise. This is the whole path:
internet, gateway, tunnel, router, segment.

### 5. An allowed port to an address with nothing on it

```bash
nc -nzv -w 6 <unused-44net-address> <allowed-port>
```

Expect `No route to host` and the accept counter to rise. That error is the **success**
condition — the router accepted the packet, tried to deliver it on the segment, and found
nothing there. The firewall did its job.

**But note what it costs.** Producing that error means the router answers the
internet about addresses nobody is using, which tells a scanner which parts of
your allocation are live. If you would rather not say, suppress it and accept
that this test can no longer distinguish "accepted but empty" from "dropped" —
see [return-path.md](return-path.md). Silence leaks less than a passing test.

## Paths that must fail

**If a test below shows the request arriving but nothing coming back**, stop and
read [return-path.md](return-path.md). Replies to a caller outside 44net do not
match your 44net routes, so they leave by the default route — and if that path
does NAT, as cellular does, the answer never reaches the caller. It looks like a
closed port and is not.

### 6. Blocked ports from outside

```bash
nc -nzv -w 6 <your-44net-address> 80
nc -nzv -w 6 <your-44net-address> 22
```

Expect silent timeouts. Silence is correct — a refusal would tell a scanner something is there.

If you left test 5's ICMP error enabled, note the contrast: an allowed port to an empty address
answers `No route to host` while a blocked port to a live address says nothing, and that
difference proves your firewall discriminates by port and not merely by host.

If you suppressed it, both are silent — which is the quieter posture and the one that tells a
scanner least. You lose the contrast as evidence; put a second device on the segment to prove
range-wide reachability instead.

### 7. The 44net segment into your internal network

The rule that makes the segment a DMZ. From the host on the segment, forced through the router:

```bash
ip route add <internal-host>/32 via <router-44net-address> dev <interface>
ping -c 3 -W 3 -I <44net-address> <internal-host>
ip route del <internal-host>/32
```

Expect 100% loss **and** the isolation counter up by exactly three. Without the explicit route
the traffic never passes the router and the test proves nothing.

Tests 3 and 7 are a pair: traffic flows inward to the segment and not back out of it, while
replies to internally-started connections still work because only `connection-state=new` is
dropped.

### 8. Router services from the segment

```bash
nc -nzv -w 6 -s <44net-address> <router-44net-address> 22
nc -nzv -w 6 -s <44net-address> <router-44net-address> 8291
nc -nzv -w 6 -s <44net-address> <router-44net-address> 53
```

SSH, the management interface and DNS must all time out.

### 9. Rules you forgot were there

The one most likely to catch you. Any accept rule in your forward chain **without an interface
restriction** — a port forward to a web server, a rule between two of your own subnets — will
also match traffic arriving from the gateway tunnel, because it was written before the tunnel
existed.

List the chain in order and look for accepts above your tunnel drop:

```
:foreach r in=[/ip/firewall/filter/find chain=forward] \
  do={:put ([/ip/firewall/filter/get $r action]." | in=".[/ip/firewall/filter/get $r in-interface]." | ".[/ip/firewall/filter/get $r comment])}
```

The fix is to put your tunnel's accept-and-drop pair at the **very top** of the chain. The drop
matches `connection-state=new` only, so established return traffic still reaches the
`accept established,related` rule further down.

Verify the resulting order rather than trusting the move command. Getting the drop above the
accept produces exactly the symptom this whole document exists to catch: connected, but not
routing.

## Summary

| # | Path | Expected |
|---|---|---|
| 1 | Router → 44net segment | reachable |
| 2 | Segment → gateway | reachable |
| 3 | LAN → segment | reachable, source address preserved |
| 4 | Outside → allowed port | delivered, visible in capture |
| 5 | Outside → allowed port, empty address | `No route to host`, or silent if you suppress it |
| 6 | Outside → blocked port | silent timeout |
| 7 | Segment → internal network | dropped, counter matches exactly |
| 8 | Segment → router services | silent timeout |
| 9 | Unscoped accepts above the tunnel drop | none |

## Running part of this automatically

Tests 4, 5 and 6 — everything driven from the outside machine — are packaged in
[test-container.md](test-container.md) as a container you can re-run after any
change. It refuses to run if its own route to the target leaves via a tunnel,
so it cannot quietly test from the wrong side.

The rest still has to be done by hand. Tests 1, 2, 3, 7 and 8 originate from the
router or from the segment, and test 9 is a reading of your rule order that no
probe can substitute for.

If you need something to aim at before your real node moves onto the segment,
[esp32-tester.md](esp32-tester.md) describes a small device that holds an
address, answers, and reports the source address each packet carried — which is
what separates a reachability failure from a translation failure.
