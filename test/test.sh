#!/bin/sh

set -e

RESULT=0

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
NCP=ncp2 ../src/ping 003 1 | grep 'One octet from host 003: 001' || fail

./arpanet.sh stop

exit $RESULT
