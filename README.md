# Real Time Embedded Systems Final Assignment, AUTh [2019]
> Communicate with other devices (embedded systems) through WiFi using minimum energy possible.

> Embedded System used	-> Raspberry Pi 0.

> Cross Compiler used	-> [Cross Compiler for Raspberry Pi Zero](https://sourceforge.net/projects/raspberry-pi-cross-compilers/files/Raspberry%20Pi%20GCC%20Cross-Compiler%20Toolchains/GCC%206.3.0/Raspberry%20Pi%201%2C%20Zero/)

This is an *experimental* application developed as part of the course "Real Time Embedded Systems" assignment, that took place in the Department of Electrical & Computer Engineering at Aristotle University of Thessaloniki in 2019.


The goal is to communicate with other devices (Raspberry Pi), through WiFi to exchange messages. Each device will represent a node in the communication network and will be responsible for generating and sending new messages, but also for forwarding messages to others so that the information is transmitted to all devices and eventually the message will be delivered to its recipient.
---

## Execution

To execute the code, you first need to cross compile it (for Raspberry Pi) using:
```sh
make all
```
secure copy it (scp to Raspberry) and then run using:
```sh
./MessengerApp
```

---

## Status

As of the completion of the project, it will NOT be maintained. By no means should it ever be considered stable or safe to use, as it may contain incomplete parts, critical bugs and security vulnerabilities.

---

## Support

Reach out to me:

- [mpalaourg's email](mailto:gbalaouras@gmail.com "gbalaouras@gmail.com")

---

## License

[![Beerware License](https://img.shields.io/badge/license-beerware%20%F0%9F%8D%BA-blue.svg)](https://github.com/mpalaourg/RTES_FinalTask/blob/master/LICENCE.md)