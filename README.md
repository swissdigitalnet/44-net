# 44-net for AREDN

![AREDN](https://img.shields.io/badge/AREDN-tunnel%20server%20%7C%20supernode-blue)
![44net](https://img.shields.io/badge/44net-44.0.0.0%2F8-orange)
![Router](https://img.shields.io/badge/router-MikroTik%20RouterOS-lightgrey)

Putting an AREDN tunnel server or supernode onto 44net. Written for operators who
want to do the same: what to do in the 44Net Connect portal, what changes on the router, what
changes on the AREDN node, the packet-size problem that catches everyone, and how to migrate
without breaking the peers you already have.

## 📻 Do you actually need this?

**If you only want to link to a node whose tunnel endpoint is a 44 address — you need nothing.**

44net is globally routed public address space. Your node reaches `44.x.y.z` exactly as it
reaches any other internet address. Set the tunnel up normally. No allocation, no gateway, no
router changes.

This catches people out regularly. A peer having a 44 address does not mean you need 44
anything.

**If you want your own node to have a 44net address that others can dial** — that is
participation, and it is what this guide is about.

## 🛰️ Why bother

Your node currently reaches the world through your provider's address. Every service it offers
is squeezed through a port forward, and it never sees who is really calling — only your
router's translated view.

44net is address space allocated to licensed amateurs. With a block of your own, your node
holds a genuine public address: reachable on its normal ports, addressed as itself, and
independent of whatever your provider hands you this month.

## 🗺️ Three scenarios

### 1. Port forwarding — the usual setup, no 44net

```
   peer's AREDN node
          │
          │  dials YOUR-PUBLIC-IP : 5525
          ▼
   ┌──────────────────────┐
   │  your router         │   address from your ISP
   │  port forward        │
   └──────────────────────┘
          │  translated to a private address
          ▼
   ┌──────────────────────┐
   │  AREDN node          │   192.168.x.y
   └──────────────────────┘
```

The whole path is ordinary internet at 1500 bytes. A peer at AREDN's 1420 default fits with
room to spare. Nothing to tune, works with every firmware version.

**What it costs:** one shared address, a port forward per service, and your node never sees the
real source address of anyone talking to it.

### 2. 44net gateway tunnel — participation

```
   peer's AREDN node                        44net gateway
          │                                        │
          │  dials YOUR-44NET-ADDRESS              │
          └──────────► internet ───────────────────┘
                                                   │
                        WireGuard tunnel, carries your /28
                                                   ▼
                                        ┌──────────────────────┐
                                        │  your router         │
                                        │  holds 44net .1      │
                                        └──────────────────────┘
                                                   │
                                                   ▼
                                        ┌──────────────────────┐
                                        │  AREDN node   .2     │
                                        └──────────────────────┘
```

Your allocation reaches your house through a tunnel from the 44net gateway. The node holds a
real public 44 address and answers on its normal ports.

**The catch:** your peer's tunnel now runs *inside* the gateway tunnel. Two layers of
encapsulation, and the inner one no longer fits. **Both ends** must reduce their tunnel MTU —
see below.

### 3. Migration — both at once

```
   peer not yet ready              peer ready
          │                             │
          │ dials your ISP address      │ dials your 44net address
          ▼                             ▼
   ┌──────────────┐             ┌──────────────────┐
   │ port forward │             │  gateway tunnel  │
   └──────────────┘             └──────────────────┘
          │                             │
          └──────────────┬──────────────┘
                         ▼
                 ┌──────────────┐
                 │  AREDN node  │
                 └──────────────┘
```

Both paths live at the same time. Peers move individually, whenever they have set their MTU.
Anyone who cannot, or will not, keeps using the old path indefinitely.

**This is the only sane way to do it.** Your own MTU you can fix in a minute. Your peers' you
cannot, and some are on firmware with no supported way to set it.

## 🌐 On the 44Net Connect portal

Two things happen at [connect.44net.cloud](https://connect.44net.cloud) before you touch the
router. You will need a portal.ampr.org login.

**Generate a WireGuard keypair on your router first** — the portal wants your public key, and
your private key should never leave the router.

### Tunnels page — [connect.44net.cloud/tunnels](https://connect.44net.cloud/tunnels)

1. Request a new tunnel and select your preferred gateway server. It carries all your 44net
   traffic, so pick one that is close to you.
2. Paste your **public key**.
3. Create the tunnel.

The portal returns a wg-quick configuration containing the three things your router needs:

| From the portal | Used as |
|---|---|
| Gateway public key | the peer's public key |
| Endpoint address and port | `endpoint-address` / `endpoint-port` |
| An address for your tunnel interface | a `/32` on the tunnel interface |

### Networks page — [connect.44net.cloud/networks](https://connect.44net.cloud/networks)

This is where your allocated prefix is associated with the tunnel you just created, so the
gateway knows to route it down that tunnel to you.

**Do not skip it.** With only the tunnels step done, the tunnel comes up and handshakes
happily, but nothing is routed to you and your allocation stays unreachable — which looks like
a broken tunnel and is not.

> The exact fields on the networks page are not covered in the public wiki and the portal
> requires a login, so the description above is from the resulting configuration rather than
> from the page itself. Corrections welcome.

Allocations themselves are requested from [ARDC](https://www.ampr.org/); a `/28` gives 13
usable addresses.

## ⚙️ What changes on the router

| | Scenario 1 | Scenarios 2 & 3 |
|---|---|---|
| Port forward to the node | yes | keep it, pointed at the node's new address |
| WireGuard tunnel to the gateway | — | yes |
| Route: gateway's own address via your ISP gateway | — | **essential**, see below |
| Routes `44.0.0.0/9` and `44.128.0.0/10` via the tunnel | — | yes |
| Masquerade for your LAN out of the tunnel | — | yes, or your LAN cannot reach 44net |
| A VLAN for the 44net segment, router holds `.1` | — | yes |
| DHCP with a static lease for the node | — | yes |
| Firewall: accept named ports, drop the rest | — | yes, at the **top** of the forward chain |

Two are easy to miss and both fail confusingly:

**The gateway's own address must not route into the tunnel.** It lies inside `44.0.0.0/9`, so
without a more specific route via your ISP gateway, the tunnel's own traffic is routed into
itself and it dies the moment you add the 44net routes.

**Without the masquerade, 44net becomes *less* reachable than before you started.** Packets
from your LAN enter the tunnel carrying a private source address that nothing can reply to.

Full configuration examples: [docs/enabling-44net.md](docs/enabling-44net.md).

## 🖥️ What changes on the AREDN node

| | Scenario 1 | Scenarios 2 & 3 |
|---|---|---|
| WAN interface | on your LAN, DHCP | on the 44net VLAN, DHCP |
| Address | private, from your router | 44net, from a static DHCP lease keyed to its WAN MAC |
| **Default Tunnel MTU** | default 1420 | **1360** |
| Anything else | — | nothing |

A static DHCP lease rather than a static address inside AREDN matters: the node keeps
`proto=dhcp`, nothing in its own configuration changes, and there is nothing for a firmware
upgrade to discard.

### If the node runs on Proxmox

One command moves its WAN interface onto the 44net VLAN:

```bash
qm snapshot <vmid> pre-44net
qm set <vmid> -net1 virtio=<mac>,bridge=vmbr0,tag=<44net-vlan>
```

virtio hot-plugs, so no reboot. The snapshot captures the VM configuration *and* the disk, so
`qm rollback <vmid> pre-44net` undoes both the VLAN change and anything altered inside AREDN in
one step.

An AREDN node has three interfaces — LAN, WAN and DtD — bridged internally. **Only the WAN one
moves.** LAN and DtD stay where they are.

Bind the DHCP lease to the MAC of the node's **`br-wan` bridge**, not to the MAC shown in the
VM configuration. AREDN builds its own bridges with their own addresses, and it is `br-wan`
that requests the lease.

## 📏 The MTU problem

This is the part that costs people evenings.

| | |
|---|---|
| Ordinary internet path | 1500 |
| Usable inside a WireGuard gateway tunnel | about 1420 |
| An AREDN tunnel at its 1420 default emits | about 1480 |

1480 does not fit in 1420. The failure is silent, because handshake packets are small enough to
get through: the tunnel connects, looks healthy, and never routes.

**What you see:** the peer appears in your mesh as a bare MAC address, no hostname, no Babel
metric.

**How to confirm** — any interface with a live handshake but no Babel neighbour:

```sh
wg show all latest-handshakes
echo dump | socat -T 8 -t 8 UNIX-CLIENT:/var/run/babel.sock - | grep neighbour
```

**The fix**, on both ends of every tunnel that crosses the gateway. AREDN web interface,
**Tunnels → Advanced**:

```
┌──────────────────────────────────────────────────────────────┐
│  Default Tunnel MTU                               [ 1360 ]   │
│  Default packet size for tunnels                             │
│                                                              │
│  The default packet (MTU) size for all tunnels. This can be  │
│  set between 1280 and 1420 if your WAN network requires      │
│  smaller packets than normal. Any change will affect all     │
│  tunnels. Remember to change the MTU size at the other end.  │
└──────────────────────────────────────────────────────────────┘
```

Note AREDN's own warning: *remember to change it at the other end*. That is why migration is
per-peer.

**Firmware matters.** This field is recent. Older releases have no supported way to set tunnel
MTU — only `ip link set dev <iface> mtu 1360`, lost at the next reboot. Check before promising
a peer anything.

## 🤝 Bringing peers across

Per peer, in this order — the order matters:

1. They set **Default Tunnel MTU** to 1360 and let tunnels re-establish.
2. They repoint the tunnel at your 44net address, same port.
3. Confirm a Babel neighbour appears with a sensible cost — not just a WireGuard handshake.
4. Retire the port forward only when everyone has moved. There is no hurry.

[docs/peer-letter.md](docs/peer-letter.md) is a template you can send.

If step 3 shows a handshake with no Babel neighbour, the MTU is still wrong somewhere. Put them
back on the old address while you work it out.

## ⚠️ Dead tunnels are not free

A tunnel that is connected but not routing still advertises a near-complete copy of the mesh at
an unusable cost, and your routing daemon re-evaluates all of it. On one supernode, three such
links accounted for most of a CPU core and roughly 40% of the routing table.

If a peer is unreachable, disable the tunnel rather than leaving it connected.

## 📚 Documentation

| Document | What it covers |
|---|---|
| [docs/enabling-44net.md](docs/enabling-44net.md) | Full setup guide with configuration examples |
| [docs/peer-letter.md](docs/peer-letter.md) | Template letter asking a peer to move |
| [docs/tunnels.md](docs/tunnels.md) | Tunnel roles, what each link needs, MTU arithmetic |
| [docs/test-procedure.md](docs/test-procedure.md) | Proving the open paths work and the closed ones do not |
| [docs/operations.md](docs/operations.md) | Health checks, consoles, recovery |
| [docs/concept.md](docs/concept.md) | Architecture and firewall design in detail |
| [docs/migration.md](docs/migration.md) | Per-node cutover runbook |

## 🔗 References

- [AREDN](https://www.arednmesh.org/) — Amateur Radio Emergency Data Network
- [ARDC](https://www.ampr.org/) — allocates 44net space
- [44Net Connect](https://wiki.ampr.org/wiki/44Net_Connect) — the WireGuard gateway service
- [AREDN issue #2594](https://github.com/aredn/aredn/issues/2594) — the MTU behaviour, still open

`44.192.0.0/10` is **not** 44net — that block was sold, and must be excluded from any route
you add.
