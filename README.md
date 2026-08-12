# 44-net

![Status](https://img.shields.io/badge/phase%201-complete-brightgreen)
![Allocation](https://img.shields.io/badge/AMPRNet-44.xx.xx.xx%2F28-blue)
![Router](https://img.shields.io/badge/RouterOS-7.20.1-orange)
![Nodes](https://img.shields.io/badge/AREDN-4.26-lightgrey)

Bringing real, routable AMPRNet addresses to the AREDN systems at HB9BLA.

## 📻 The problem

The AREDN systems at home share one public address from the internet provider. Every
service they offer has to be squeezed through a port forward on that single address, and
the systems never see who is really talking to them — only the router's translated view.
Adding a second service means finding another free port and remembering what it was for.

Licensed radio amateurs can be allocated addresses from AMPRNet, the 44.0.0.0/8 range
reserved for amateur radio. Those are genuine public addresses. The difficulty is getting
them from the network that allocates them to a house on an ordinary internet connection.

## 🛰️ The solution

A WireGuard tunnel from the home router to an AMPRNet gateway carries a block of 14
addresses to the house. The AREDN systems sit on their own network segment with real
public addresses of their own — no port forwards, no address translation, and every
service reachable on its normal port.

```
44.xx.xx.xx  is the supernode.  Not "port 6526 on the router, which forwards to a VM".
```

## ⚙️ What it does

- Carries the allocated `44.xx.xx.xx/28` from the AMPRNet gateway to the home network
- Gives each AREDN system a real public address on a dedicated VLAN
- Keeps the old port-forward path working at the same time, so systems migrate one at a
  time with no outage and an easy way back
- Blocks everything inbound except the ports each system actually serves
- Prevents the public segment from reaching any internal network — it is treated as a DMZ
- Sends amateur radio traffic out through the tunnel while ordinary internet traffic keeps
  using the normal connection

The allocated range is publicly routed and **continuously scanned from the open internet**.
That is not a flaw in the setup; it is what a public address means. The firewall design
assumes it.

## 🗺️ How it works

```
            AMPRNet gateway 44.xx.xx.xx
                       |
                       |  WireGuard, carries 44.xx.xx.xx/28
                       v
        +--------------------------------+
        |   MikroTik router 192.0.2.1  |
        |   gateway 44.xx.xx.xx          |
        +--------------------------------+
           |                          |
           |  only named ports        |  no new connections
           v                          X
      +---------+        +-------------------------------+
      | VLAN 44 |        |  VLAN 100 private             |
      |         |        |  VLAN 20  VoIP                |
      | .82 SN  |        |  VLAN 300 remote station      |
      | .83 TS  |        |  VLAN 200 guest / 555 IoT     |
      +---------+        +-------------------------------+
```

Traffic in from the tunnel reaches VLAN 44 only on the ports the AREDN systems serve.
Traffic out of VLAN 44 toward any internal network is dropped, so a compromised node on
the public segment cannot reach the house. Replies to connections the house starts are
still allowed, because only *new* connections are blocked.

## 📍 Current state

| | |
|---|---|
| Tunnel | live, `wireguardPOP`, MTU 1380 |
| Allocation | `44.xx.xx.xx/28`, routed by the gateway and confirmed arriving |
| VLAN 44 | created, router holds `44.xx.xx.xx` |
| Firewall | inbound accepts and DMZ isolation in place, verified by test |
| AREDN-local | on `44.xx.xx.xx` |
| Supernode / tunnel server | on `<node-address>` / `<node-address>` behind port forwards |

The remaining work is moving the supernode and tunnel server onto their public addresses. See
[docs/migration.md](docs/migration.md), and [docs/tunnels.md](docs/tunnels.md) for the
baseline a move has to match.

## 📚 Documentation

| Document | What it covers |
|---|---|
| [docs/concept.md](docs/concept.md) | Architecture, addressing, firewall design, and the constraints behind them |
| [docs/operations.md](docs/operations.md) | Running it day to day: health checks, consoles, recovery, troubleshooting |
| [docs/test-procedure.md](docs/test-procedure.md) | Repeatable tests proving open paths work and closed paths do not |
| [docs/migration.md](docs/migration.md) | Moving each AREDN system onto its public address |
| [docs/tunnels.md](docs/tunnels.md) | Tunnel inventory, what each link needs, and the move plan |

## 🔢 Address plan

| Address | Host |
|---|---|
| `44.xx.xx.xx` | Router, gateway on `vlan-44-AMPR` |
| `44.xx.xx.xx` | AREDN supernode, Proxmox VM 105 |
| `44.xx.xx.xx` | AREDN tunnel server, Proxmox VM 104 |
| `44.xx.xx.xx` | AREDN-local, Proxmox VM 103 |
| `44.xx.xx.xx`–`.94` | Free |

`44.xx.xx.xx` is the network address and `.95` the broadcast address.

## 🔓 What is reachable from outside

Only these. Everything else inbound is dropped without a reply.

| Destination | Protocol | Port | Purpose |
|---|---|---|---|
| `44.xx.xx.xx` | UDP | 6526-6550 | AREDN supernode tunnels |
| `44.xx.xx.xx` | UDP | 5525-5570 | AREDN WireGuard tunnels |

The router itself answers nothing on VLAN 44 or through the tunnel — no SSH, no WinBox, no
DNS, no web interface.

## 🩺 Health check

```
# tunnel alive - handshake should be under a minute old
/interface/wireguard/peers/print detail where comment="POP Server Peer"

# traffic actually crossing the tunnel
/ping 44.xx.xx.xx interface=wireguardPOP src-address=44.xx.xx.xx count=3
```

The full set of checks, including tests that prove the blocked paths really are blocked, is
in [docs/test-procedure.md](docs/test-procedure.md).

## 🔧 Troubleshooting

**The tunnel goes down for no apparent reason.** Check that the route
`44.xx.xx.xx/32` still points at the internet provider's gateway and is active. The gateway
address is learned by DHCP; if the provider changes it, the route goes inactive, the
gateway's own address falls back to the tunnel route, and the tunnel routes its own traffic
into itself.

**An AREDN system loses a setting after a reboot or firmware upgrade.** AREDN rebuilds its
network configuration from its own settings at boot. Anything added outside that — an extra
address, a hand-edited configuration file — is discarded. Configure through AREDN's own
settings, or handle it on the router instead.

**A node is unreachable after a change.** Every AREDN VM has a serial console on the
Proxmox host, and a snapshot taken before a change restores both the machine settings and
the disk. See [docs/operations.md](docs/operations.md).

**Machines on the home network cannot reach 44-net.** The `defconf: masquerade` rule is
scoped to `out-interface-list=WAN`, and the gateway tunnel is in no interface list, so LAN
traffic enters the tunnel still carrying a private source address and nothing can reply. Add
a masquerade for `out-interface=wireguardPOP`. `git.ampr.org` is itself on 44-net
(`44.1.2.69`), so it is a quick test.

**AREDN tunnels connect but large transfers stall.** An AREDN tunnel carried inside this
tunnel is wrapped twice. The outer tunnel allows 1380 bytes, leaving roughly 1300 for the
inner one. This is corrected on the AREDN node, not on the router.

## 🔗 References

- [AREDN](https://www.arednmesh.org/) — Amateur Radio Emergency Data Network
- [ARDC](https://www.ampr.org/) — Amateur Radio Digital Communications, who allocate 44-net space
- [MikroTik WireGuard](https://help.mikrotik.com/docs/display/ROS/WireGuard)

`44.192.0.0/10` is not AMPRNet — that block was sold and is deliberately excluded from the
routing here.
