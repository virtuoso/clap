#!/bin/sh -e
# Networking end-to-end test: spawn tools/server on loopback test ports, run
# the test client against it, then verify the server-side log artifacts.
# Usage: net-e2e-test.sh <server-binary> <test-client-binary>

SERVER="$1"
CLIENT="$2"
PORT=41044
WSPORT=41046
DIR="$(mktemp -d)"
SPID=""

cleanup()
{
    if [ -n "$SPID" ]; then
        kill "$SPID" 2>/dev/null || true
        wait "$SPID" 2>/dev/null || true
    fi
    rm -rf "$DIR"
}
trap cleanup EXIT

"$SERVER" -S 127.0.0.1 --port $PORT --wsport $WSPORT --log-dir "$DIR" \
    >"$DIR/server.out" 2>&1 &
SPID=$!

# the server takes a moment to come up, especially under the sanitizers
for i in $(seq 50); do
    grep -q "networking initialized" "$DIR/server.out" 2>/dev/null && break
    sleep 0.1
done

"$CLIENT" -S 127.0.0.1 -p $PORT

# a restart request against a port nobody listens on must fail
if "$SERVER" -R -S 127.0.0.1 --port 41099 >/dev/null 2>&1; then
    echo "restart against nothing reported success" >&2
    exit 1
fi

LOG="$(ls "$DIR"/clap-127.0.0.1-* 2>/dev/null | head -1)"
if [ -z "$LOG" ]; then
    echo "no client log file in $DIR" >&2
    tail -5 "$DIR/server.out" >&2
    exit 1
fi
grep -q "net-e2e marker" "$LOG" || { echo "marker missing from $LOG" >&2; exit 1; }
grep -q -- "--- connection closed ---" "$LOG" || { echo "no clean close in $LOG" >&2; exit 1; }

echo "net-e2e: OK ($(wc -l < "$LOG") log lines)"
