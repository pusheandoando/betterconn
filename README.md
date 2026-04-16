# betterconn
Linux network optimizer focused on Debian. Maximizes connection quality for gaming, streaming, and general use by applying proven kernel-level and traffic tuning at runtime, with full backup and restore of original settings. Written by Christian (@pusheandoando)





## What it does
- **TCP BBR** congestion control (Google's algorithm — measures actual bandwidth and RTT instead of relying on packet loss, significantly reducing latency and improving throughput) — [paper](https://queue.acm.org/detail.cfm?id=3022184)
- **fq** queue discipline (fair queuing, required for BBR pacing) — [kernel docs](https://www.kernel.org/doc/Documentation/networking/fq.txt)
- **TCP ECN** (Explicit Congestion Notification) — routers signal congestion by marking packets rather than dropping them, allowing TCP to back off before any loss occurs; pairs directly with BBR+fq for bufferbloat reduction — [RFC 3168](https://datatracker.ietf.org/doc/html/rfc3168)
- **TCP Fast Open** enabled for client and server (value 3), reducing connection setup round-trips — [RFC 7413](https://datatracker.ietf.org/doc/html/rfc7413)
- **RX/TX socket buffers** raised to 32 MB max with 1 MB default; covers the bandwidth-delay product for 1 Gbps links at up to ~250 ms RTT without overcommitting RAM
- **tcp_autocorking disabled** — the kernel normally batches small writes to fill packets; disabling this sends each write immediately, cutting queuing delay for interactive and gaming traffic where microseconds matter — [LWN](https://lwn.net/Articles/576263/)
- **tcp_notsent_lowat = 131072** to reduce bufferbloat and keep HTTP/game request latency low — [paper](https://dl.acm.org/doi/10.1145/2063176.2063196)
- **tcp_slow_start_after_idle = 0** so BBR does not throttle connections that were briefly idle (critical for gaming and real-time apps)
- **tcp_fin_timeout = 15** to release closed connections faster
- **tcp_tw_reuse = 1** to reuse TIME_WAIT sockets safely
- **tcp_mtu_probing = 1** for automatic MTU discovery on lossy paths — [RFC 4821](https://datatracker.ietf.org/doc/html/rfc4821)
- **ip_local_port_range = 1024–65535** to maximize available outgoing ports
- **TCP keepalive tuning** — detects dead connections in ~120 s instead of the default ~2.5 hours, freeing ports and avoiding hangs in games and applications
- **Adaptive NIC interrupt coalescing** via ethtool — the NIC driver dynamically adjusts its interrupt rate to balance between low latency and high throughput depending on current traffic patterns
- **iptables TOS 0x10** (Minimize Delay) rules for DNS, HTTP/S, Steam, and common game server UDP ports — [RFC 791](https://www.rfc-editor.org/rfc/rfc791) · [RFC 2474](https://www.rfc-editor.org/rfc/rfc2474.html)





## Persistence
`--start` survives reboots, forced shutdowns, and power loss. On apply, betterconn writes:
- `/etc/modules-load.d/betterconn.conf`: loads `tcp_bbr` and `xt_TOS` early at boot
- `/etc/sysctl.d/99-betterconn.conf`: all kernel parameters, applied by `systemd-sysctl` at every boot
- `/etc/systemd/system/betterconn.service`: oneshot systemd unit that re-applies the iptables QoS rules and adaptive NIC coalescing after the network is up

`--stop` removes all three files, disables the systemd unit, reverts `/proc/sys` to the saved backup, restores the original NIC coalescing state, and removes the iptables rules. After `--stop`, rebooting leaves no trace of betterconn.





## Requirements
### Runtime dependencies
```bash
sudo apt install iptables iproute2 kmod iputils-ping curl ethtool
```
These are pre-installed on most Debian systems, with the possible exception of `ethtool`. The Linux kernel must be 4.9 or newer for BBR support (Debian 9+ ships this by default). ECN requires kernel 2.4+ (all current Debian releases qualify).

### Build dependencies
```bash
sudo apt install build-essential cmake
```
CMake 3.16 or newer is required. Debian 11 ships cmake 3.18, Debian 12 ships cmake 3.25. No external C++ libraries are needed beyond the standard C++17 library (included with GCC 8+).





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
```bash
sudo betterconn --start      # apply all optimizations (persists across reboots)
betterconn --status          # live stats: speed, ping, current kernel settings
sudo betterconn --stop       # revert everything to original settings
sudo betterconn --clean      # remove all betterconn files from the system (requires --stop first)
betterconn --help            # usage
```