#!/usr/bin/env bash
# Runs the parts of docs/test-procedure.md that can be driven from outside.
#
# Every check sends real traffic and reads the result back from the device.
# Reading configuration is not proof; this is.
#
# Must run on a host OUTSIDE the network under test.

set -uo pipefail

DEVICE="${DEVICE:?set DEVICE to the tester address on your 44net segment}"
UNUSED="${UNUSED:?set UNUSED to an address in your allocation with nothing on it}"
UDP_PORT="${UDP_PORT:-5000}"
ALLOWED_PORT="${ALLOWED_PORT:-80}"
BLOCKED_PORT="${BLOCKED_PORT:-22}"
TIMEOUT="${TIMEOUT:-6}"

pass=0
fail=0

ok()   { printf '  \033[32mPASS\033[0m  %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; fail=$((fail+1)); }
note() { printf '        %s\n' "$1"; }
head2() { printf '\n\033[1m%s\033[0m\n' "$1"; }

fetch() { curl -s -m "$TIMEOUT" "http://${DEVICE}/"; }

# ---------------------------------------------------------------- preflight

head2 "Preflight"

route="$(ip route get "$DEVICE" 2>/dev/null | head -1)"
if [[ -z "$route" ]]; then
    bad "no route to $DEVICE"
    exit 1
fi
note "$route"

if grep -qiE 'dev (wg|tun|tap|gre)' <<<"$route"; then
    bad "route leaves via a tunnel interface - this host is not outside the network under test"
    note "the procedure is meaningless from inside; use a machine on the open internet"
    exit 1
fi

# A private or CGNAT target means we are inside the network under test, whatever
# interface the route uses. Catching only tunnels was not enough: a LAN route
# looks perfectly ordinary and would let the whole suite pass while proving
# nothing about the inbound path.
same_network=0
if [[ "$DEVICE" =~ ^10\. ]] ||
   [[ "$DEVICE" =~ ^192\.168\. ]] ||
   [[ "$DEVICE" =~ ^172\.(1[6-9]|2[0-9]|3[01])\. ]] ||
   [[ "$DEVICE" =~ ^100\.(6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7])\. ]]; then
    same_network=1
    bad "$DEVICE is a private address - this is not a test of the inbound path"
    note "your 44net allocation is public space; a private target means you are"
    note "inside the network under test. Useful for shaking out this script,"
    note "worthless as evidence. Continuing in degraded mode."
else
    ok "route to $DEVICE is over the public internet, not a tunnel of yours"
fi

# ------------------------------------------------------- 1. inbound allowed

head2 "1. Outside -> allowed port, delivered end to end   (test 4)"

body="$(fetch)"
if [[ -z "$body" ]]; then
    bad "no response from http://${DEVICE}/"
    note "the whole path is: internet, gateway, tunnel, router, segment"
    note "check the inbound accept rule for ${DEVICE}:${ALLOWED_PORT}"
else
    ok "device answered on port ${ALLOWED_PORT}"
    ver="$(grep -m1 '^version=' <<<"$body" | cut -d= -f2-)"
    ota="$(grep -m1 '^ota='     <<<"$body" | cut -d= -f2-)"
    you="$(grep -m1 '^you='     <<<"$body" | cut -d= -f2-)"
    note "version=${ver:-?}  ota=${ota:-?}"
    note "device saw us as: ${you:-?}"

    if [[ "$same_network" -eq 1 ]]; then
        note "skipping the source check: on the same network there is no NAT to detect"
    else
        myip="$(curl -s -m "$TIMEOUT" https://api.ipify.org 2>/dev/null)"
        if [[ -n "$myip" && -n "$you" ]]; then
            if [[ "${you%%:*}" == "$myip" ]]; then
                ok "source address preserved end to end (${myip})"
            else
                bad "source rewritten: we are ${myip}, device saw ${you%%:*}"
                note "something between here and the segment is translating -"
                note "expected on a masqueraded path, wrong on a routed 44net path"
            fi
        fi
    fi
fi

# ------------------------------------------------------------ 2. UDP arrival

head2 "2. Outside -> allowed UDP port                     (test 4, UDP)"

echo "44net-probe" | nc -u -w 1 "$DEVICE" "$UDP_PORT" >/dev/null 2>&1
sleep 2
if fetch | grep -qE "^  udp .* -> :${UDP_PORT} "; then
    ok "UDP probe arrived and was recorded"
    fetch | grep -E "^  udp " | sed 's/^/        /'
else
    bad "UDP probe to :${UDP_PORT} never arrived"
    note "an accept rule for udp/${UDP_PORT} is missing, or the device is not listening"
fi

# -------------------------------------------------- 3. allowed port, no host

head2 "3. Outside -> allowed port on an empty address     (test 5)"

out="$(nc -nzv -w "$TIMEOUT" "$UNUSED" "$ALLOWED_PORT" 2>&1)"
if grep -qi 'no route to host' <<<"$out"; then
    ok "'No route to host' - the router accepted it and found nothing there"
    note "this error IS the success condition: the firewall did its job"
elif grep -qiE 'succeeded|open' <<<"$out"; then
    bad "something answered on ${UNUSED}:${ALLOWED_PORT} - that address should be empty"
else
    bad "silent timeout - the packet was dropped before reaching the segment"
    note "expected acceptance then failure to deliver; got neither"
fi

# ------------------------------------------------------- 4. blocked inbound

head2 "4. Outside -> blocked port                         (test 6)"

out="$(nc -nzv -w "$TIMEOUT" "$DEVICE" "$BLOCKED_PORT" 2>&1)"
if grep -qiE 'succeeded|open' <<<"$out"; then
    bad "port ${BLOCKED_PORT} is reachable from the internet"
elif grep -qi 'no route to host' <<<"$out"; then
    bad "got 'No route to host' - the firewall ACCEPTED this port, it should drop it"
    note "contrast with test 3: acceptance leaks the fact that the port is open"
elif grep -qiE 'refused|reset' <<<"$out"; then
    # A reset means the packet reached a host that declined it - nothing was
    # filtered. Reporting that as a pass would credit the firewall for a result
    # it had no part in.
    bad "connection refused on ${BLOCKED_PORT} - the packet reached the host and was reset"
    note "nothing dropped it; the target simply has no service on that port"
    note "a firewall that drops would give silence instead"
else
    ok "silent timeout on ${BLOCKED_PORT} - correct, a refusal would tell a scanner it is there"
fi

# ------------------------------------------------------------------ summary

head2 "Arrivals recorded by the device"
fetch | sed -n '/^arrivals/,$p' | sed 's/^/  /'

head2 "Summary"
printf '  %d passed, %d failed\n\n' "$pass" "$fail"
[[ "$fail" -eq 0 ]]
