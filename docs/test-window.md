# Test windows: opening a port on purpose, and closing it again

There is a difference between **testing** and **operating**, and the firewall
should know about it.

While you are testing you need a way in — something has to answer from the
segment or you cannot prove the inbound path works at all. When the test is
over, that way in has no reason to exist. The normal state of a 44net router is
**everything closed except what the station actually serves**, and a port left
open after a test is not a feature, it is a leftover.

So the rule that lets a test through should be added deliberately, scoped
tightly, and **removed automatically** — because the step everyone skips is the
last one.

That is all a "test window" is: one firewall rule with an expiry.

```
44net-test-window.sh open [minutes]    # add the rule, default 30 minutes
44net-test-window.sh close             # remove it now
44net-test-window.sh status            # is it open, and for how much longer
```

This document describes what those do and why, with a worked example on a
Teltonika. It is not a tool this repository ships — your router is not the one
in the example. The point is the shape; [translating it](#translating-this-to-your-router)
is the easy part, and there is a tip at the end for doing that with an AI.

## Two things this never opens

**Port 80.** Not on 44net. It is the most-scanned port on the internet and your
allocation is public space. The [ESP32 tester](esp32-tester.md) listens on
**8044** for exactly this reason. Moving off 80 is not concealment and it is no
substitute for closing the port — it just stops you spending the window
answering strangers.

**The update path.** Firmware updates are **pulled by the device** from GitHub
over TLS. Nothing is pushed to it, so no inbound port is involved and no window
needs to exist for an update. If your update mechanism requires an inbound
port, change the mechanism rather than widening the window — an inbound update
port is a remote code execution endpoint on public address space, and a token
crossing plaintext HTTP does not change that.

The tester's own `/update` endpoint enforces this from its side: it refuses any
caller that is not on the same segment, so even with a window open, an update
cannot be started from the internet.

## What `open` actually adds

One rule. Four things scope it, and each one you leave out widens the hole:

| Scope | Why |
|---|---|
| `src` = the gateway tunnel zone | not "wan", not "any" — the tunnel is the only place a 44net caller can arrive from |
| `dest_ip` = the one device | never the segment, never the `/28`. A subnet-wide accept covers every address you have not filled yet |
| `dest_port` = the one port | the tester's `8044`, not a range |
| `src_ip` = your test machine | the strongest one available, and the one most often skipped |

That last row is worth arguing for. You are testing from a known machine on the
open internet — the container in [test-container.md](test-container.md) running
on a VPS whose address you know. Restricting the rule to that source turns a
window anyone can walk through into one only you can. It costs one field.

The trade: with `src_ip` set you are no longer testing "is this reachable from
the internet", you are testing "is this reachable from *there*". For proving a
path works those are the same thing. Drop the field only when you specifically
want to confirm the general case, and then keep the window short.

The rule must also land **above your tunnel's catch-all drop**. On fw3 that is
automatic — accepts are emitted before the zone's fallback — but confirm it
rather than assume it, because getting this backwards produces a window that
opens nothing and a test that fails for the wrong reason.

## Worked example: Teltonika (RutOS)

RutOS is OpenWrt underneath, so this is `uci` and `fw3`. Names to substitute:
`wg44` is the gateway tunnel zone, `lan44` the 44net segment zone.

```sh
uci set firewall.testwin=rule
uci set firewall.testwin.name='44net-test-window'
uci set firewall.testwin.src='wg44'
uci set firewall.testwin.dest='lan44'
uci set firewall.testwin.src_ip='198.51.100.7'      # your test machine
uci set firewall.testwin.dest_ip='44.xx.yy.72'      # the tester
uci set firewall.testwin.proto='tcp'
uci set firewall.testwin.dest_port='8044'
uci set firewall.testwin.target='ACCEPT'
uci set firewall.testwin.enabled='1'
/etc/init.d/firewall reload
```

and to close it:

```sh
uci delete firewall.testwin
/etc/init.d/firewall reload
```

**Delete it, do not disable it.** `enabled='0'` leaves a fully-formed rule
sitting in the configuration waiting for someone — possibly you, in six
months — to flip one character. A rule that is gone cannot be re-enabled by
accident.

### Notice what is missing: `uci commit`

Uncommitted `uci` changes live in `/tmp`, and `fw3` applies them on reload
anyway. So a window opened without committing is **active immediately and gone
after a reboot**, which is precisely the lifetime you want. Committing it would
make a temporary hole permanent.

Verify that on your own router before relying on it — open a window, reboot,
and check the rule is gone:

```sh
uci changes firewall            # shows the staged rule
reboot
# after it comes back:
iptables -S | grep 8044         # expect nothing
```

Treat it as a backstop regardless. The timer below is the mechanism; a reboot
closing the window is a second line of defence, not the plan.

## Making it close itself

A window a human has to close is a window that stays open. `open` schedules its
own `close`:

```sh
SELF="$(readlink -f "$0")"
PIDFILE=/tmp/44net-test-window.pid

# cancel any timer already pending, so a second `open` resets the clock
[ -f "$PIDFILE" ] && kill "$(cat "$PIDFILE")" 2>/dev/null
rm -f "$PIDFILE"

setsid sh -c "sleep $((minutes * 60)); $SELF close" >/dev/null 2>&1 &
echo $! > "$PIDFILE"
```

`setsid` is the part that matters. Backgrounding with a plain `&` puts the
timer in your SSH session's process group, and closing the session sends it
`SIGHUP` — so the window would stay open exactly when you walked away from it.
Detaching it from the session is what makes the guarantee hold.

Cron is the alternative and is worse here: its granularity is a minute, an
absolute time has to be computed, and the entry itself becomes a thing to clean
up. The sleep is honest about being a one-shot.

## `status`, and why it reads the firewall

```sh
iptables -S | grep -- '--dport 8044' || echo "closed"
```

Ask the **firewall**, not the script's own bookkeeping. A pidfile tells you
what the script believes; the rule list tells you what is true. They disagree
after a crash, a manual `uci` edit, or a half-finished reload — and the whole
point of the exercise is not to be wrong about which ports are open.

## Verify from outside, then verify it closed

The window is only interesting if it changes what the internet can reach, and
your router cannot tell you that. Run the check from the machine the window is
scoped to:

```bash
# window open
nc -nzv -w 6 44.xx.yy.72 8044       # expect: succeeded
# after it expires
nc -nzv -w 6 44.xx.yy.72 8044       # expect: silent timeout, not "refused"
```

**Silence, not refusal.** A refusal tells a scanner something is listening
there; a drop tells it nothing. If closing the window produces `Connection
refused`, your unmatched traffic is falling through to a global reject and the
window is not the only thing you need to fix — see
[return-path.md](return-path.md).

Confirming the close is not optional politeness. It is the half of the test
that catches a `reload` that silently failed, and it takes six seconds.

## Where this fits in a session

```
1. open the window, scoped to the machine you are testing from
2. run the procedure — docs/test-procedure.md, or the container
3. read the results off the device
4. close the window
5. confirm from outside that it is closed
```

Steps 4 and 5 belong in the session, not in a note to do later. Between
sessions the router sits in its normal state: nothing inbound but the ports
your station genuinely serves.

## Translating this to your router

Nothing above is specific to Teltonika except the syntax. Every router with a
firewall can express "one scoped accept, added and removed", and the reasoning
— narrow scope, automatic expiry, delete rather than disable, verify from
outside — carries across unchanged.

On **MikroTik (RouterOS)** the same window is roughly:

```
/ip/firewall/filter/add chain=forward action=accept comment=44net-test-window \
    in-interface=<tunnel> src-address=<your-test-machine> \
    dst-address=<device> protocol=tcp dst-port=8044 \
    place-before=[find comment=44net-drop-everything-else]

/ip/firewall/filter/remove [find comment=44net-test-window]
```

RouterOS has no uncommitted-change trick, so the rule survives a reboot and the
timer is the only thing closing it. `/system/scheduler` can hold the expiry
instead of a detached sleep, which is the better fit there — but then remember
the scheduler entry is itself something to remove.

### A tip: hand this document to an AI

If your router is neither of these — pfSense, VyOS, a plain Linux box with
nftables, a UniFi gateway — the fastest route is to give an AI assistant this
page and ask it to translate. The reasoning is all here; only the syntax is
missing, and that is the part a model is reliably good at.

Give it these, or it will guess:

- **the router and firmware version** (`pfSense 2.7`, `RouterOS 7.16`, `OpenWrt 23.05`)
- **the name of the gateway tunnel interface or zone**, and of the 44net segment
- **the device address and port** you want reachable
- **the source address** you will test from
- **how your catch-all drop is expressed**, so the accept can be placed above it

Then ask it for `open`, `close` and `status`, and for the exact command that
shows whether the rule is currently live.

**Check two things in whatever it produces**, because these are the failures
that matter and both look fine at a glance:

1. **Does `close` actually remove the rule**, rather than disabling it or
   writing a second rule that shadows it?
2. **Does the accept land above the catch-all drop?** Ask for the command that
   prints the resulting rule order, and read it.

Then test it the way this document says: from outside, open and closed.

## Checklist

| | |
|---|---|
| Scoped to one source, one destination, one port | not a subnet, not a range |
| Never port 80 on 44net | the tester uses 8044 |
| No inbound update port | updates are pulled by the device |
| Closes itself on a timer | detached from your login session |
| Deleted, not disabled | nothing left to re-enable by accident |
| Above the catch-all drop | confirmed by reading the order, not assumed |
| Verified open **and** closed from outside | six seconds, catches a failed reload |
