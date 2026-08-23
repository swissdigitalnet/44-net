#!/usr/bin/env bash
# Fetches the latest published release and pushes it to the tester.
#
# The device offers the OTA command; this initiates it. Nothing is pulled by
# the device itself, so the 44net segment needs no outbound internet access.
#
# The firewall rule scoping the OTA port to THIS host's source address is the
# real access control. The bearer token below crosses plaintext HTTP and only
# guards against a mistake.

set -uo pipefail

NETWORK="${NETWORK:-}"
DEVICE="${DEVICE:-}"

# Same reasoning as run-tests.sh: an allocation is small enough to sweep, so
# nobody should have to look up where the tester landed.
if [[ -z "$DEVICE" && -n "$NETWORK" ]]; then
    ip="${NETWORK%/*}"; pfx="${NETWORK#*/}"
    IFS=. read -r a b c d <<<"$ip"
    size=$(( 1 << (32 - pfx) )); base=$(( d & (255 ^ (size - 1)) ))
    for (( i = 1; i < size - 1; i++ )); do
        cand="$a.$b.$c.$(( base + i ))"
        if curl -s -m 4 "http://${cand}/" 2>/dev/null | grep -q '^id=esp32-44net-tester'; then
            DEVICE="$cand"
            echo "discovered tester at ${DEVICE}"
            break
        fi
    done
fi
if [[ -z "$DEVICE" ]]; then
    echo "no tester found - set DEVICE, or NETWORK to sweep" >&2
    exit 1
fi
GITHUB_REPO="${GITHUB_REPO:-swissdigitalnet/44-net}"
OTA_TOKEN="${OTA_TOKEN:?set OTA_TOKEN to the token built into the firmware}"
ASSET="${ASSET:-esp32-44net-tester.bin}"
FORCE="${FORCE:-0}"
TIMEOUT="${TIMEOUT:-10}"

status() { curl -s -m "$TIMEOUT" "http://${DEVICE}/"; }
field()  { grep -m1 "^$1=" <<<"$2" | cut -d= -f2-; }

echo "== device =="
body="$(status)"
if [[ -z "$body" ]]; then
    echo "FAIL: no response from http://${DEVICE}/" >&2
    exit 1
fi

cur_ver="$(field version "$body")"
cur_ota="$(field ota "$body")"
echo "  version=${cur_ver}  ota=${cur_ota}"

# A previous push that has not settled means the bootloader may still roll it
# back. Flashing on top of that would discard the evidence of what went wrong.
if [[ "$cur_ota" == "pending" && "$FORCE" != "1" ]]; then
    echo "REFUSING: the running image is still pending verification." >&2
    echo "Wait for it to become valid, or let it roll back. FORCE=1 overrides." >&2
    exit 2
fi

echo "== latest release of ${GITHUB_REPO} =="
rel="$(curl -s -m "$TIMEOUT" "https://api.github.com/repos/${GITHUB_REPO}/releases/latest")"
tag="$(jq -r '.tag_name // empty' <<<"$rel")"
url="$(jq -r --arg A "$ASSET" '.assets[]? | select(.name==$A) | .browser_download_url' <<<"$rel")"

if [[ -z "$tag" ]]; then
    echo "FAIL: no published release found" >&2
    exit 1
fi
if [[ -z "$url" ]]; then
    echo "FAIL: release ${tag} has no asset named ${ASSET}" >&2
    exit 1
fi
echo "  ${tag}  ${url}"

if [[ "$cur_ver" == "$tag" && "$FORCE" != "1" ]]; then
    echo "Device already runs ${tag}. Nothing to do. (FORCE=1 to push anyway.)"
    exit 0
fi

echo "== downloading =="
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
if ! curl -sL -m 120 -o "${tmp}/${ASSET}" "$url"; then
    echo "FAIL: download failed" >&2
    exit 1
fi
size="$(stat -c %s "${tmp}/${ASSET}")"
echo "  ${size} bytes"
if [[ "$size" -lt 65536 ]]; then
    echo "FAIL: implausibly small image, refusing to push" >&2
    exit 1
fi

echo "== pushing to ${DEVICE} =="
resp="$(curl -s -m 180 -w '\n%{http_code}' \
        -H "Authorization: Bearer ${OTA_TOKEN}" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@${tmp}/${ASSET}" \
        "http://${DEVICE}/ota")"
code="$(tail -1 <<<"$resp")"
echo "  HTTP ${code}: $(head -n -1 <<<"$resp")"

if [[ "$code" == "401" ]]; then
    echo "FAIL: token rejected" >&2
    exit 1
fi
if [[ "$code" != "200" ]]; then
    echo "FAIL: device refused the image" >&2
    exit 1
fi

echo "== waiting for it to come back =="
# It reboots, rejoins WiFi, binds its listener, then holds 30 s before marking
# itself valid. Anything less and the bootloader will restore the old image.
for i in $(seq 1 30); do
    sleep 5
    body="$(status)" || true
    [[ -z "$body" ]] && { printf '.'; continue; }
    new_ver="$(field version "$body")"
    new_ota="$(field ota "$body")"
    printf '\n  version=%s ota=%s' "$new_ver" "$new_ota"
    if [[ "$new_ver" == "$tag" && "$new_ota" == "valid" ]]; then
        echo
        echo "OK: ${tag} is running and marked valid."
        exit 0
    fi
done

echo
echo "FAIL: device did not report ${tag} as valid within the window." >&2
echo "If it never marks valid, the bootloader restores the previous image on" >&2
echo "the next boot - the old firmware should still be reachable. Re-check with:" >&2
echo "  curl http://${DEVICE}/" >&2
exit 1
