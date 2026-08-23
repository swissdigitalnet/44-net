# Running the test procedure from a container

[docs/test-procedure.md](test-procedure.md) is a list of things to check by
hand. This is the same list as a container you can run repeatedly, on any host
outside your network, printing pass or fail per check.

Source: [`test-container/`](../test-container).

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
| `DEVICE` | the target on your 44net segment |
| `UNUSED` | an address in your allocation with nothing on it (test 5) |
| `UDP_PORT` | UDP port your firewall permits and the target listens on |
| `ALLOWED_PORT` | a TCP port your firewall permits inbound |
| `BLOCKED_PORT` | a TCP port it must block |
| `GITHUB_REPO`, `OTA_TOKEN` | update push only |

Nothing site-specific is baked into the image.

## Running the tests

```bash
docker compose run --rm test
```

| Check | Procedure test | Passes when |
|---|---|---|
| Route is not a tunnel | preflight | this host is genuinely outside |
| Outside → allowed port | 4 | device answers, and its `you=` matches your real address |
| Outside → allowed UDP port | 4 | probe appears in the device's arrivals list |
| Outside → allowed port, empty address | 5 | `No route to host` |
| Outside → blocked port | 6 | silent timeout |

Two of those results are counter-intuitive and worth restating, because the
procedure makes the same point:

**`No route to host` is a pass.** The router accepted the packet, tried to
deliver it on the segment, and found nothing there. The firewall did its job.

**Silence is a pass, on a blocked port.** A refusal would tell a scanner
something is there. If a blocked port answers `No route to host` instead, your
firewall is accepting it and only the absence of a host is hiding you.

The contrast between those two is the actual proof that your rules discriminate
by port and not merely by address.

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
[esp32-tester.md](esp32-tester.md) deliberately does not probe, so the
must-fail direction still has to be checked by hand.

Run those after any firewall change. This container covers the inbound half.

## Pushing firmware

```bash
docker compose run --rm ota
```

Fetches the latest published release, compares it with the version the device
reports, and pushes it if they differ. It refuses when the device reports
`ota=pending` — a previous push has not settled, and flashing over it would
discard the evidence of what went wrong. `FORCE=1` overrides.

After pushing it waits for the device to reboot, rejoin, and report both the
new version and `ota=valid`. If it never does, the bootloader restores the
previous image on the next boot and the old firmware should still answer.

The host running this must be the one your OTA firewall rule names as source.
