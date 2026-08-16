# Enabling 44net on an AREDN tunnel server or supernode

A practical guide for operators. Two quite different things get called "using 44net", and only
one of them needs any work — so start by deciding which you want.

## First: do you actually need this?

**You want to link to a node whose tunnel endpoint is a 44net address.**

Nothing to do. 44net is globally routed public address space, so your node reaches
`44.x.y.z` exactly as it reaches any other internet address. Set the tunnel up in the normal
way. No allocation, no gateway, no routing changes.

This surprises people. A peer having a 44 address does not mean you need 44 anything.

**You want your own node to have a 44net address that others can dial.**

That is participation, and it needs an allocation, a gateway to carry it to your house, and
some router configuration. The rest of this document is about that.

## What participation requires

| | |
|---|---|
| An allocation | Requested from [ARDC](https://www.ampr.org/). A /28 gives 13 usable addresses. |
| A gateway | Your allocation has to reach your house somehow. Two options, below. |
| Router configuration | Routes, firewall, and one NAT rule. |
| Attention to MTU | The part that catches everyone. See below. |

### Choosing a gateway

**44Net Connect** builds a WireGuard tunnel from your router to an ARDC endpoint. It works
behind CGNAT and on routers that cannot forward IP protocol 4, which is why it is the
recommended path now. It costs about 60 bytes of packet overhead.

**IPENCAP (IP-in-IP)** is the older mesh of point-to-point tunnels between gateways. It costs
about 20 bytes, so it leaves far more room for tunnels carried inside it — but it needs a real
public address and a router that passes IP protocol 4.

That 40-byte difference decides whether other operators can reach you without changing their
own MTU. See [tunnels.md](tunnels.md) for the arithmetic.

## In the 44Net Connect portal

Two steps at [connect.44net.cloud](https://connect.44net.cloud), before any router work. A
portal.ampr.org login is required.

Generate a WireGuard keypair on your router first — the portal asks for your public key, and
the private key should never leave the router.

**Tunnels — [connect.44net.cloud/tunnels](https://connect.44net.cloud/tunnels)**

Request a new tunnel, choose a gateway server near you (it carries all your 44net traffic),
paste your **public key**, and create it. The portal returns a wg-quick configuration with the
three values the router needs:

| From the portal | Used as |
|---|---|
| Gateway public key | the peer's public key |
| Endpoint address and port | `endpoint-address` / `endpoint-port` |
| Address for your tunnel interface | a `/32` on the tunnel interface |

**Networks — [connect.44net.cloud/networks](https://connect.44net.cloud/networks)**

Associate your allocated prefix with the tunnel you just created, so the gateway routes it down
that tunnel to you.

Skipping this produces a confusing result: the tunnel comes up and handshakes normally, but
nothing is routed to you and your allocation stays unreachable. It looks like a broken tunnel
and is not.

> The networks page is not covered in the public wiki and the portal requires a login, so the
> description above is inferred from the resulting configuration rather than from the page.
> Corrections welcome.

Allocations are requested from [ARDC](https://www.ampr.org/).

## Router configuration

Written for RouterOS. Replace the placeholders with your own values.

```
# 1. The gateway tunnel itself, per your provider's instructions
/interface/wireguard/add name=<tunnel> private-key="<key>" mtu=<tunnel-mtu> listen-port=<port>
/interface/wireguard/peers/add interface=<tunnel> public-key="<peer-key>" \
    allowed-address=0.0.0.0/0 endpoint-address=<gateway> endpoint-port=<gw-port> \
    persistent-keepalive=20s

# 2. The gateway's own address must NOT route into the tunnel
/ip/route/add dst-address=<gateway>/32 gateway=<your-isp-gateway>

# 3. 44net via the tunnel
/ip/route/add dst-address=44.0.0.0/9    gateway=<tunnel>
/ip/route/add dst-address=44.128.0.0/10 gateway=<tunnel>

# 4. So your own network can reach 44net through it
/ip/firewall/nat/add chain=srcnat action=masquerade out-interface=<tunnel> \
    src-address=<your-lan-subnet>
```

Step 2 is not optional and is easy to miss. The gateway's public address lies inside
`44.0.0.0/9`, so without a more specific route the tunnel's own traffic is routed into the
tunnel and it stops working the moment you add step 3.

`44.192.0.0/10` is deliberately absent — that block was sold and is not 44net.

Step 4 matters more than it looks. With the routes in place but no masquerade, packets from
your network enter the tunnel carrying a private source address that nothing can reply to — so
44net becomes *less* reachable than before you started.

### Firewall

Your allocation is public space and is scanned continuously from the open internet. Treat it
accordingly.

```
# Allow only what your node actually serves
/ip/firewall/filter/add chain=forward action=accept in-interface=<tunnel> \
    protocol=udp dst-address=<node> dst-port=<node-tunnel-ports>

# Drop everything else arriving from the tunnel
/ip/firewall/filter/add chain=forward action=drop connection-state=new in-interface=<tunnel>
```

Put both **at the very top of the forward chain**. Any address-based accept rule that sits
above them — a port forward to a web server, a rule between two of your own subnets — will
also match traffic arriving from the tunnel, because those rules were written without the
tunnel in mind.

Never use a subnet-wide accept. An AREDN node listens on `0.0.0.0` for its web interface,
so a blanket rule publishes its admin pages to the internet.

Do not add the tunnel to your LAN interface list, or your router's own services become
reachable from it.

## MTU: the part that catches everyone

An AREDN tunnel to a peer inside 44net travels **inside** your gateway tunnel. Two layers of
encapsulation, each costing about 60 bytes.

| | |
|---|---|
| Ordinary internet path | 1500 |
| Usable inside a WireGuard gateway tunnel | about 1420 |
| An AREDN tunnel at its 1420 default emits | about 1480 |

1480 does not fit in 1420. The failure is silent: the handshake succeeds because those packets
are small, so the tunnel looks connected while full-size packets vanish and routing never
establishes.

**Symptom.** The peer appears in your mesh as a bare MAC address with no hostname and no
Babel metric. Nothing is routed over it.

**Check.** Any interface with a live handshake but no Babel neighbour is affected:

```sh
wg show all latest-handshakes
echo dump | socat -T 8 -t 8 UNIX-CLIENT:/var/run/babel.sock - | grep neighbour
```

**Fix.** Reduce the tunnel MTU so the inner tunnel fits inside the outer one. Subtract about
60 from whatever your gateway tunnel carries — for a 1420 gateway tunnel, use **1360**.

### Setting it

AREDN web interface → **Tunnels** → **Advanced** (the toggle at the bottom of the dialog):

![Default Tunnel MTU field in the AREDN Tunnels dialog](img/tunnel-mtu.png)

> The default packet (MTU) size for all tunnels. This can be set between 1280 and 1420
> if your WAN network requires smaller packets than normal. Any change will affect all
> tunnels. **Remember to change the MTU size at the other end.**

Accepts 1280–1420, applies to every tunnel on the node, and persists across reboots and
firmware upgrades because it is stored in AREDN's own configuration.

Note AREDN's own warning: *"Remember to change the MTU size at the other end."* That is the
crux of the next section.

**Firmware requirement.** This field needs a recent build. On 4.26.1.0 there is no supported
way to set tunnel MTU at all; `ip link set dev <iface> mtu 1360` works but is lost on reboot.
Check your version before promising anyone anything.

## Migration strategy

The awkward part is not your node. It is that **each direction is limited by the path it
travels**, and your peers are not all ready at once.

Your node can be made to fit immediately by lowering its MTU. A peer's packets to you cannot —
their MTU is theirs to set, and if they are on older firmware they may have no supported way to
do it. So a peer that dials your 44net address before reducing its MTU will connect and then
fail to route, which looks exactly like a broken tunnel.

The way through is to run **both paths at once** and let peers move individually.

### Keep the old path working

If peers currently reach you through a port forward on your ISP address, leave it in place and
point it at wherever your node now lives. That path crosses only the ordinary internet at 1500,
so a peer at the 1420 default fits comfortably and needs to change nothing.

This is worth stating plainly: the port forward is not a temporary crutch to be removed once
migration finishes. It is the path that works with **unmodified** peers, and it can stay
indefinitely.

### Add the new path

Two ways to give your node a 44net address, depending on how much disruption you want:

**Address translation, node untouched.** Map a 44net address to the node's existing address on
your router:

```
/ip/firewall/nat/add chain=dstnat action=netmap dst-address=<44net-addr> \
    in-interface=<tunnel> to-addresses=<node>
/ip/firewall/nat/add chain=srcnat action=netmap src-address=<node> \
    out-interface=<tunnel> to-addresses=<44net-addr>
```

No node configuration, no downtime, and rolling back is deleting two rules. The node does not
truly hold a public address, but peers can reach it at one. Good for a migration period.

Two things to get right: the `srcnat` netmap must sit **above** any masquerade for the same
interface, or your node's outbound traffic gets the wrong source address. And your forward
accept must name the node's **translated** address, because translation happens before the
firewall sees the packet.

**A real address on the node.** Give the node's WAN a 44net address directly. The cleanest way
is a static DHCP lease on your router keyed to the node's WAN MAC — the node keeps `proto=dhcp`
and nothing inside AREDN changes, so there is nothing for a firmware upgrade to discard.

An AREDN node has exactly one WAN interface, so it cannot hold its old address and a 44net
address at the same time. That is why the parallel period lives on the router, not the node.

### Moving peers across

For each peer, in this order:

1. Both ends set their tunnel MTU appropriately — yours is already done, theirs is the ask.
2. They repoint their tunnel at your 44net address.
3. Confirm a Babel neighbour appears with a sensible cost, not just a WireGuard handshake.
4. Only when every peer has moved, consider retiring the port forward.

If step 3 shows a handshake with no Babel neighbour, the MTU is still wrong somewhere. Put them
back on the old address while you sort it out — that path always works.

### Before you start

- Point your monitoring at whatever address the node will have. Otherwise every check fails for
  the wrong reason and a real fault is invisible.
- Record which tunnels are up *and routing* first, so you can tell afterwards whether anything
  actually regressed. A tunnel that was already broken will look like migration damage.
- Take a snapshot or backup you can actually restore from.

## A note on dead links

A tunnel that is connected but not routing is not harmless. It still advertises a near-complete
copy of the mesh at an unusable cost, and your routing daemon re-evaluates all of it. Three such
links on one supernode accounted for most of a CPU core and roughly 40% of its routing table.

If a peer is unreachable, disable the tunnel rather than leaving it connected.
