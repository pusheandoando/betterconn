# betterconn (v1.0.3)
Linux network optimizer focused on Debian. Maximizes connection quality for gaming, streaming, and general use by applying proven kernel-level and traffic tuning at runtime, with adaptive real-time adjustment and full backup and restore of original settings. Written by Christian (@pusheandoando)





## What it does
- **TCP BBR** congestion control — measures actual bandwidth and RTT instead of relying on packet loss, reducing latency and improving throughput — [ACM Queue](https://queue.acm.org/detail.cfm?id=3022184)
- **fq_codel** queue discipline — active queue management with per-flow isolation; eliminates bufferbloat without a rate cap, making it correct for endpoint use — [RFC 8290](https://datatracker.ietf.org/doc/html/rfc8290), [WiFi 6 AQM study 2025](https://arxiv.org/abs/2512.18259)
- **TCP ECN** — routers signal congestion by marking packets rather than dropping them, pairs directly with fq_codel — [RFC 3168](https://datatracker.ietf.org/doc/html/rfc3168)
- **TCP Fast Open** (value 3) — reduces connection setup round-trips — [RFC 7413](https://datatracker.ietf.org/doc/html/rfc7413)
- **RX/TX socket buffers** — dynamically sized by the adaptive tuner based on measured real-time throughput, from 4 MB (slow links) up to 32 MB (fast links)
- **tcp_autocorking disabled** — sends each write immediately, cutting queuing delay for interactive and gaming traffic — [LWN](https://lwn.net/Articles/576263/)
- **tcp_notsent_lowat** — tuned dynamically to reduce application-level buffering and keep request latency low
- **tcp_slow_start_after_idle = 0** — BBR does not throttle connections that were briefly idle
- **WiFi power save disabled** — prevents 20–100 ms idle latency spikes caused by the card sleeping between beacon intervals; made permanent via NetworkManager so it survives reconnects
- **Adaptive NIC interrupt coalescing** via ethtool — dynamically balances interrupt rate between low latency and high throughput
- **iptables TOS 0x10** (Minimize Delay) for DNS, HTTP/S, Steam, and common game UDP ports — [RFC 791](https://www.rfc-editor.org/rfc/rfc791)
- **Real-time adaptive tuner** — runs as a background daemon; every 3 seconds reads `/proc/net/wireless` (RSSI, TX retries), `/proc/net/dev` (live throughput), and RTT via ping, then adjusts buffers, `netdev_budget`, `tcp_notsent_lowat`, and the fq_codel target without any user interaction





## Persistence
`betterconn start` survives reboots, forced shutdowns, and power loss. On apply, betterconn writes:

- `/etc/modules-load.d/betterconn.conf` — loads `tcp_bbr` and `xt_TOS` early at boot
- `/etc/sysctl.d/99-betterconn.conf` — all kernel parameters, applied by `systemd-sysctl` at every boot
- `/etc/betterconn/iptables-apply.sh` — re-applies QoS rules, qdisc, and WiFi power save after boot
- `/etc/systemd/system/betterconn.service` — systemd unit that runs the boot script then starts the adaptive daemon

`betterconn stop` removes all of the above, stops the daemon, reverts `/proc/sys` to the saved backup, restores the original NIC coalescing state, and removes the iptables rules. After `stop`, rebooting leaves no trace of betterconn.





## Requirements
### Runtime dependencies
```bash
sudo apt install iptables iproute2 kmod iputils-ping ethtool iw
```
Kernel 4.9 or newer required for BBR (Debian 9+ ships this by default).

### Build dependencies
```bash
sudo apt install build-essential cmake
```
CMake 3.16+, GCC with C++17 support (GCC 8+). No external libraries needed.

## Build
```bash
chmod +x build.sh
./build.sh
```

## Build a .deb package
```bash
chmod +x build_debian.sh
./build_debian.sh
```





## Usage
- Apply all optimizations and start the adaptive daemon. Persists across reboots:
```bash
sudo betterconn start
```

- Revert all settings to their original values and stop the daemon:
```bash
sudo betterconn stop
```

- Show live stats: speed, ping, and current kernel settings:
```bash
betterconn status
```

- Remove all betterconn files from the system. Requires `stop` first:
```bash
sudo betterconn clean
```