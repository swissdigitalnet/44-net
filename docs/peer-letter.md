# Letter to a peer: moving a tunnel to 44net

Template for asking an existing tunnel partner to move their link from your ISP address to
your 44net address. Replace the bracketed parts.

The important thing this letter has to achieve is that they set their tunnel MTU **before**
they repoint. If they repoint first, the tunnel will connect and then silently fail to route,
which wastes an evening for both of you.

---

**Subject: Moving our AREDN tunnel to 44net — one setting needed at your end**

Hello [name]

My node now has a 44net address, and I would like to move our tunnel over to it when it suits
you. Traffic between us would then run over 44net rather than through my commercial
internet connection.

There is one thing that has to be set at your end first, and doing it in the wrong order
causes a confusing failure — so I have written it out.

**Why a setting is needed.** My 44net allocation reaches me through a WireGuard tunnel from the
44net gateway. Once you dial my 44net address, your tunnel runs *inside* that one. Two layers
of encapsulation, and the inner one no longer fits: the usable size drops from about 1500 to
about 1420, while an AREDN tunnel at its 1420 default needs roughly 1480.

**What that failure looks like**, in case you meet it anywhere else: the tunnel connects, the
handshake is fine, and my node appears in your mesh as a bare MAC address with no hostname and
no Babel metric. Nothing routes. Small packets get through and full-size ones do not, so
everything looks healthy except that it does not work.

**The setting.** In the AREDN web interface: **Tunnels → Advanced → Default Tunnel MTU**, set
to **1360**. It applies to all tunnels on the node, and it persists across reboots and firmware
upgrades. The field accepts 1280 to 1420.

If you do not see that field, your firmware predates it. On older releases the only option is
`ip link set dev <interface> mtu 1360` over SSH, which is lost at the next reboot — so a
firmware update is worth doing first.

**Then, in this order:**

1. Set the MTU to 1360 and let the tunnels re-establish.
2. Change our tunnel's address from [your ISP address] to **[your 44net address]**, same port.
3. Check that my node appears with a proper Babel metric, not just as a MAC address.

**If anything goes wrong**, put the old address back. I am leaving the existing path in place
indefinitely, so it will keep working — nothing you do here is one-way.

Happy to do it together over a sked if that is easier.

73
[your callsign]

---

## If they ask why they should bother

Worth having an answer ready, because the honest one is not "it is faster".

Traffic over 44net stays inside amateur radio address space and does not depend on either
party's commercial provider for its addressing. Some operators care about that; others
reasonably do not. It costs them one setting and one address change, and it costs you nothing
if they decline — the old path works either way.

## If they are willing but on old firmware

The MTU setting arrived in a recent AREDN release. Before that there is no supported way to set
it, only a command that does not survive a reboot. If they cannot update, leave them on the
port-forward path. It works perfectly well and needs nothing from them.
