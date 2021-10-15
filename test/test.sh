#!/bin/sh

set -e

PIDS=""

build() {
    cd simh
    make h316
}

imp() {
    simh/BIN/h316 "$1.simh" > "$1.log" 2>&1 &
    PIDS="$PIDS $!"
}

arpanet() {
    imp imp2
    imp imp3
    imp imp4
}

test -x simh/BIN/h316 || (build)

arpanet
echo "Arpanet started."
echo "IMP pids:$PIDS."
sleep 10
for i in $PIDS; do
    kill $i
done
echo "Arpanet stopped."
