ARPANET Network Control Program for Linux,
interfacing with the SIMH IMP emulator.

**This is a work in progress.**  At this point, the NCP is not a high
quality implementation.  It's primarily used for testing the network.

- [x] Implement the IMP-host interface.
- [x] Exchange 1822 NOP messages between host NCP and IMP.
- [x] Send ECO message to another host, get ERP back.
- [x] Application library for NCP.
- [x] Send RFC and CLS messages to open and close connections.
- [x] Send data back and forth.
- [x] Implement FINGER and TELNET, both clients and servers.
- [x] Test against PDP-10 emulator running ITS.
- [ ] Test against PDP-10 emulator running WAITS.
- [ ] ???
- [ ] Profit!
