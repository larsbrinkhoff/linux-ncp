#!/bin/sh

set -e

RESULT=0

trap "./arpanet.sh stop" EXIT INT QUIT

fail() {
    echo FAILED
    RESULT=1
}

build() {
    cd simh
    make h316
}

(cd ../src && make)
test -r simh/scp.c || git submodule update --init
test -x simh/BIN/h316 || (build)

echo Starting ARPANET.   Allow 30 seconds for IMP network to settle.
./arpanet.sh start
# Some time for NCP too.
sleep 10

echo "Test pinging another host."
NCP=ncp2 ../src/ping -c3 003 | grep 'Reply from host 003: seq=3' || fail

exit $RESULT
