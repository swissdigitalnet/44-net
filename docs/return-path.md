# The return path

Everything else in this repository is about letting traffic *in*. This chapter
is about getting the answer back out, which is where the time actually goes.

The symptom is distinctive and misleading: **requests visibly arrive and nothing
comes back**. A capture on the tunnel shows the caller's SYNs, retried three or
four times. The target is up and answers fine from inside. Every firewall rule
reads correctly. From outside it looks like the port is closed.

## Why it happens

Your allocation is routed to you down the gateway tunnel, so a request from the
open internet arrives on that interface. But the caller's address is an ordinary
internet address — it is **not** in `44.0.0.0/9`.

So when the reply is routed, the two 44net routes do not match it. It follows
the default route instead, out of your ordinary WAN. What happens next decides
whether you notice:

| Your WAN | What happens to the reply |
|---|---|
| Ordinary ISP, no NAT on that source | Leaves with its `44.x` source. Usually works. Asymmetric, but the caller sees a valid answer. |
| NAT or CGNAT — cellular, most LTE | Source is rewritten to the carrier's address. The caller sees an answer from a machine it never contacted, and discards it. **Dead.** |

That is why the same configuration can work at one site and fail at another for
no visible reason. It is not the firewall.

## The fix

Replies must leave the way the request arrived. Mark connections that arrive on
the tunnel, restore that mark on the replies coming back from the segment, and
route marked packets into the tunnel.

Worked example on OpenWrt, where `wg44` is the gateway tunnel and `br-lan44`
the segment:

```sh
TABLE=44
MARK=0x44
SEGMENT_IP=44.xx.yy.65        # the router's own address on the segment

ip route replace default dev wg44 src $SEGMENT_IP table $TABLE
ip rule add fwmark $MARK lookup $TABLE

iptables -t mangle -A PREROUTING -i wg44 \
    -m conntrack --ctstate NEW -j CONNMARK --set-mark $MARK
iptables -t mangle -A PREROUTING -i br-lan44 -j CONNMARK --restore-mark
```

Install it as a firewall include so it survives a reboot, and verify by deleting
the rules and reloading rather than by assuming:

```sh
uci set firewall.reply_routing=include
uci set firewall.reply_routing.path='/etc/44net-reply-routing.sh'
uci set firewall.reply_routing.reload='1'
```

On RouterOS the same shape is `/ip firewall mangle` with `mark-connection` and
`mark-routing`, plus a routing table containing only the tunnel.

### `src` is not optional

Without it, a packet the **router itself** generates takes its source from the
global default route — the carrier's address — and the gateway drops it on the
source filter described in [enabling-44net.md](enabling-44net.md). You get a
timeout and no clue why. With `src` set to an address inside your allocation,
it is accepted.

## What to leave broken, on purpose

It is tempting to add `iptables -t mangle -A OUTPUT -j CONNMARK --restore-mark`
so that locally generated packets go back through the tunnel too. **Do not.**

The packets this affects are mostly ICMP errors — in particular the *host
unreachable* the router emits when it accepts a packet for the segment and finds
nobody holding that address. Delivering those makes an empty address
distinguishable from a blocked one, which hands a scanner a map of which parts
of your allocation are live.

Silence is worth more than a passing reachability test. The cost is that
[test-procedure.md](test-procedure.md) test 5 can no longer tell "accepted but
empty" from "dropped" — accept that, and confirm range-wide reachability by
putting a second device on the segment instead.

## Check what your zone policy actually does

A zone whose forward policy is `DROP` may not drop what you think. On fw3 the
generated chain is:

```
-A zone_<tunnel>_dest_DROP -o <tunnel> -j DROP
```

Note `-o`: that matches traffic heading **out** toward the tunnel. Traffic
arriving **from** the tunnel and not matched by an accept falls straight
through, out of the chain, and lands on the global fallback:

```
-A FORWARD -j reject
-A reject -p tcp -j REJECT --reject-with tcp-reset
```

So every unmatched probe from the internet is answered with a TCP reset. A
refusal tells a scanner something is there; silence tells it nothing. Add an
explicit drop for the tunnel-to-segment direction and confirm it lands *after*
your accepts:

```sh
uci set firewall.r_drop_rest=rule
uci set firewall.r_drop_rest.src='<tunnel-zone>'
uci set firewall.r_drop_rest.dest='<segment-zone>'
uci set firewall.r_drop_rest.target='DROP'
```

This one hides well. The resets only become visible once the return path works —
before that they were being generated all along and quietly lost on the WAN.

## Confirming it

From a machine outside your network, all three of these should hold:

```
allowed port on the live host   → answers
allowed port on an empty host   → silent
any blocked port                → silent
```

If the blocked port says `Connection refused`, the zone policy is falling
through to the global reject. If the live host times out while a capture on the
tunnel shows the request arriving, the return path is the problem and nothing
else is.
