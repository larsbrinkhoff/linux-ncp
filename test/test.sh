#!/bin/sh

set -e

APPS="../apps"

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
(cd $APPS && make)
test -r simh/scp.c || git submodule update --init
test -x simh/BIN/h316 || (build)

echo "Starting ARPANET.  Allow 30 seconds for IMP network to settle."
./arpanet.sh start
# Some time for NCP too.
sleep 10

echo "Test pinging another host."
NCP=ncp2 $APPS/ncp-ping -c3 003 | grep 'Reply from host 003: seq=3' || fail

echo "Test pinging a dead host."
NCP=ncp2 $APPS/ncp-ping -c1 004 && fail

echo "Test pinging a dead IMP."
NCP=ncp2 $APPS/ncp-ping -c1 005 && fail

echo "Test RFC to socket without server."
NCP=ncp3 $APPS/ncp-finger 002 && fail
sleep 2

echo "Test ICP and simple data transfer using the Finger protocol."
NCP=ncp2 $APPS/ncp-finser || fail &
PID=$!
sleep 1
NCP=ncp3 $APPS/ncp-finger 002 "Sample Finger command from client." || fail &
sleep 3
kill $! $PID 2>/dev/null || :

echo "Test a TELNET session."

NCP=ncp2 $APPS/ncp-telnet -bs || fail &
PID=$!
sleep 1
(echo test; sleep 1) | NCP=ncp3 $APPS/ncp-telnet -bc 002 | grep -a Welcome || fail &
sleep 3
kill $! $PID 2>/dev/null && fail

exit $RESULT
