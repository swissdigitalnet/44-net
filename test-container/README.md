# 44net test runner

[docs/testing.md](../docs/testing.md) is a list of things to check by
hand. This is the same list as a container you can run repeatedly, on any host
outside your network, printing pass or fail per check.

## Where it has to run

**Outside the network under test.** This is the one requirement the procedure
calls non-negotiable, and the container enforces it: before anything else it
checks its own route to the device and refuses to continue if that route leaves
via a tunnel interface.

```
ip route get <device> → dev wg0 …     → refuses
ip route get <device> → via <isp-gw>  → proceeds
```

Anything on your own LAN reaches your 44net segment directly and never crosses
the gateway tunnel, so it cannot test the inbound path at all. A cheap VPS
works.

The container also carries `tcpdump`, `nc`, `curl` and `jq`, so the host it
runs on needs none of them.

## Why host networking

`compose.yml` sets `network_mode: host` deliberately. These tests are about
which source address arrives at the far end and what the firewall does with it.
Docker's default bridge puts a NAT between the probe and the wire — exactly the
kind of translation the procedure exists to detect. Host mode keeps the packets
honest.

## Setup

```bash
cp env.example .env      # then edit
docker compose build
```

| Variable | Meaning |
|---|---|
| `NETWORK` | your allocation, e.g. `44.xx.yy.64/28` — the runner finds the rest |
| `DEVICE` | optional: a specific target, skipping discovery |
| `UNUSED` | optional: an address with nothing on it (test 5) |
| `UDP_PORT` | UDP port your firewall permits and the target listens on |
| `ALLOWED_PORT` | a TCP port your firewall permits inbound |
| `BLOCKED_PORT` | a TCP port it must block |

Nothing here concerns firmware updates. The tester pulls its own image from
GitHub over TLS, so there is no push to configure and no inbound port for one.

Nothing site-specific is baked into the image.

## It finds the device itself

An allocation is small — a `/28` is thirteen usable addresses — so there is no
reason to look up where the tester landed. Set `NETWORK` and the runner sweeps
it in parallel, recognising the tester by the `id=` it reports about itself:

```
Discovery
        sweeping 44.xx.yy.64/28 for a tester
  PASS  found tester at 44.xx.yy.71
        using 44.xx.yy.65 as the empty address for test 5
```

It also picks an address that stayed quiet for test 5, which needs somewhere
the router will accept traffic for but nothing answers on. Set `DEVICE` or
`UNUSED` explicitly to override either.

If nothing answers anywhere in the range the run stops there, because from
outside **an unreachable device and an absent one look identical** — that is
the inbound accept rule missing, or no tester running, and the tests below
cannot tell you which.

## Running the tests

```bash
docker compose run --rm test
```

| Check | Procedure test | Passes when |
|---|---|---|
| Route is not a tunnel | preflight | this host is genuinely outside |
| Outside → allowed port | 4 | device answers, and its `you=` matches your real address |
| Outside → allowed UDP port | 4 | probe appears in the device's arrivals list |
| Outside → allowed port, empty address | 3a / 5 | silence, or `No route to host` |
| Outside → allowed port, excluded host | 3b / 5 | silent timeout |
| Outside → blocked port | 4 / 6 | silent timeout |

Three of those results are counter-intuitive and worth restating, because the
procedure makes the same points:

**On an empty address, silence is the pass — and it is also the less
informative answer.** `No route to host` would prove more: it means the router
accepted the packet, tried to deliver it on the segment, and found nobody
there, so the rule permits by **port across the range** and any device that
appears later is reachable without touching the firewall.

But producing that error means the router answers the internet about addresses
nobody is using, which hands a scanner a map of which parts of your allocation
are live. That disclosure is not worth a passing test, so the deliberate choice
here is silence — see [the return path](../docs/remote-station.md#the-return-path-the-part-that-will-defeat-you) for what is suppressed
and why. The runner accepts silence and says plainly that it cannot confirm
range-wide reachability from outside. To confirm it, put a second device on the
segment and see whether it is reachable without a rule change.

**The same result is a failure on an excluded host.** Set `EXCLUDED` to an
address you have deliberately kept off 44net and test 3b checks it stays that
way. `No route to host` there means nothing is excluding it — the router
accepted the packet and merely found nobody home, so the moment a device takes
that address it is exposed. Only silence proves exclusion.

**Silence is a pass, on a blocked port.** A refusal would tell a scanner
something is there. If a blocked port answers `No route to host` instead, your
firewall is accepting it and only the absence of a host is hiding you.

## It refuses to pretend

Two checks exist because the runner was first tried from inside a LAN and
reported reassuring nonsense.

**A private target is rejected.** The preflight originally looked only for
tunnel interfaces, so a plain LAN route passed as "over the public internet"
and the whole suite ran happily against a target it could reach directly. It
now rejects RFC1918 and CGNAT destinations and marks the run degraded. Your
allocation is public space; if the target is private, you are inside the
network under test and the results are worthless as evidence.

**A TCP reset is not a pass.** The blocked-port check counted any non-success
as correct, so a target with nothing listening on that port looked like a
working firewall rule. A reset means the packet reached the host and was
declined — nothing filtered it. Only silence proves a drop.

The source-address comparison is skipped when the target is on the same
network, since there is no NAT between you to detect.

## What it cannot check

Tests 1, 2, 3, 7, 8 and 9 of the procedure — the ones run *from* the router or
*from* the segment. Those need a shell on the router, or a host on the segment
that can originate traffic. The passive target described in
[the ESP32 tester](../ESP32-44net-Tester/README.md) deliberately does not probe, so the
must-fail direction still has to be checked by hand.

Run those after any firewall change. This container covers the inbound half.

## Firmware updates are not this container's job

There used to be a `docker compose run --rm ota` here that fetched the latest
release and pushed it to the device. It is gone, along with the inbound port it
needed.

The device pulls its own firmware from GitHub over TLS now. Nothing is pushed
to it, so there is no update port to open, nothing to scope to this host's
address, and no bearer token crossing plaintext HTTP. Trigger it with
`GET /update` **from the segment** — the device refuses callers that are not on
it, so an update cannot be started from the internet even during a test window.
See [the ESP32 tester](../ESP32-44net-Tester/README.md).

The sequencing lesson is recorded there too, because it cost a site visit: the
push endpoint was removed in the same release that introduced pulling, before
pulling had been proven, and the deployed device was left with no remote update
path at all. Prove the new mechanism works before you take the old one away.
