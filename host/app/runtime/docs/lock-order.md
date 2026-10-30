# Lock and lifetime contract

User library mutex → one kernel device mutex → shared engine. The worker takes only the device mutex. No spin lock; MMIO polling sleeps and has monotonic deadlines. uaccess occurs under the device mutex only with no GPU job in flight. No callback re-enters an ioctl.

One file owns one engine. SUBMIT pins **all** objects, including FENCE, before START. WAIT timeout leaves RUNNING. Worker polls after file close; closed session buffers are freed only after validated DONE + jobs increment + pending zero + errors zero + ACK/idle/ready. Execution deadline/error pins quarantine until a controlled system restart. No recovery/reset ioctl. CDMA timeout also quarantines device and retains the permanent bounce buffer. Free cannot release quarantined memory.

v1 is built-in on hardware and suppresses bind attributes. Offline .ko proves compilation only; unload is not an accepted deployment mode. D2 reserved PS placement, D4 trusted program audit, clocks/resets and bit identity are required before real use. Host/mock pointer validation does not validate kernel uaccess.
