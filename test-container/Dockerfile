# Test runner for a 44net allocation.
#
# It carries the tools the procedure needs so the host running it does not have
# to. Run it on a machine OUTSIDE the network under test - that is the one
# non-negotiable requirement in docs/test-procedure.md, because anything on
# your own LAN reaches the segment directly and never crosses the gateway
# tunnel.
FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        ca-certificates \
        netcat-openbsd \
        tcpdump \
        iproute2 \
        iputils-ping \
        jq \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/44net
COPY run-tests.sh ota-push.sh ./
RUN chmod +x run-tests.sh ota-push.sh

ENTRYPOINT ["/opt/44net/run-tests.sh"]
