#!/bin/sh

NCPD="../src/ncpd"
H316="simh/BIN/h316"

start() {
    screen -S arpanet -X screen
    screen -S arpanet -X select "$1"
    screen -S arpanet -X stuff "$3"
}

stop() {
    screen -S arpanet -X select "$1"
    screen -S arpanet -X stuff "$2"
    screen -S arpanet -X kill
}

case "$1" in
    start)
        screen -dm -S arpanet -c screenrc
        start 0 "imp2" ""$H316" imp2.simh 2>imp2.log^M"
        start 1 "imp3" ""$H316" imp3.simh 2>imp3.log^M"
        start 2 "imp4" ""$H316" imp4.simh 2>imp4.log^M"
        sleep 30
        start 3 "ncp2" "NCP=ncp2 "$NCPD" localhost 22001 22002 2>ncp2.log^M"
        start 4 "ncp3" "NCP=ncp3 "$NCPD" localhost 22003 22004 2>ncp3.log^M"
        ;;
    attach)
        screen -S arpanet -x
        ;;
    status)
        if screen -S arpanet -Q select . > /dev/null; then
            echo "Arpanet up."
            exit 0
        else
            echo "Arpanet down."
            exit 1
        fi
        ;;
    stop)
        stop 0 "^Eq^M"
        stop 1 "^Eq^M"
        stop 2 "^Eq^M"
        stop 3 "^C"
        stop 4 "^C"
        screen -S arpanet -X quit
        pkill `basename "$H316"`
        pkill `basename "$NCPD"`
        rm -f ncp2 ncp3
        ;;
    *)
        echo "Uknown argument: $1"
        ;;
esac
