# Letter to a peer: moving a tunnel to 44net

Template for asking an existing tunnel partner to move their link from your ISP address to
your 44net address. Replace the bracketed parts.

The important thing this letter has to achieve is that they set their tunnel MTU **before**
they repoint. If they repoint first, the tunnel will connect and then silently fail to route,
which wastes an evening for both of you.

---

**Subject: Moving our AREDN tunnel to 44net — one setting needed at your end**

Hello [name]

My node now has a 44net fixed IP address, and I would like to move our tunnel over to it when it suits
you. Traffic between us would then run over 44net rather than through my commercial
internet connection.
44net is a unique HAM radio IP range. Google for it and read the readme (https://github.com/SensorsIot/44-net) if you are interested in changing your setup, too.

There are three changes you have to make:
1. upgrade your tunnelserver or Supernode to the newest nightly
2. go to advanced settings of your tunnel and add MTU 1360
3. Change the address of my tunnel from <my-tunnel-hostname> to **44.xx.xx.xx** if you connect to my supernode, or **44.xx.xx.xx** if you operate a tunnel server. The port stays the same.

If you do not see the MTU field, your firmware is not on the latest release.

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
