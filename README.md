# betterconn
Linux network optimizer focused on Debian. Maximizes connection quality for gaming, streaming, and general use by applying proven kernel-level and traffic tuning at runtime, with full backup and restore of original settings. Written by Christian (@pusheandoando)





## What it does
- **TCP BBR** congestion control (Google's algorithm — measures actual bandwidth and RTT instead of relying on packet loss, significantly reducing latency and improving throughput) — [paper](https://queue.acm.org/detail.cfm?id=3022184)
- **fq** queue discipline (fair queuing, required for BBR pacing) — [paper](https://dl.acm.org/doi/10.1145/75247.75248)
- **TCP Fast Open** enabled for client and server (value 3), reducing connection setup round-trips — [paper](https://research.google.com/pubs/pub37517.html) · [RFC 7413](https://datatracker.ietf.org/doc/html/rfc7413)
- **RX/TX socket buffers** raised to 16 MB to prevent throughput bottlenecks on fast links
- **tcp_notsent_lowat = 131072** to reduce bufferbloat and keep HTTP/game request latency low — [paper](https://dl.acm.org/doi/10.1145/2063176.2063196)
- **tcp_slow_start_after_idle = 0** so BBR does not throttle connections that were briefly idle (critical for gaming and real-time apps)
- **tcp_fin_timeout = 15** to release closed connections faster
- **tcp_tw_reuse = 1** to reuse TIME_WAIT sockets safely
- **tcp_mtu_probing = 1** for automatic MTU discovery on lossy paths — [RFC 4821](https://datatracker.ietf.org/doc/html/rfc4821)
- **ip_local_port_range = 1024–65535** to maximize available outgoing ports
- **iptables TOS 0x10** (Minimize Delay) rules for DNS, HTTP/S, Steam, and common game server UDP ports — [RFC 791](https://www.rfc-editor.org/rfc/rfc791) · [RFC 2474](https://www.rfc-editor.org/rfc/rfc2474.html)





## Requirements
### Runtime dependencies
```bash
sudo apt install iptables iproute2 kmod iputils-ping
```
These are pre-installed on most Debian systems. The Linux kernel must be 4.9 or newer for BBR support (Debian 9+ ships this by default).

### Build dependencies
```bash
sudo apt install build-essential cmake
```
CMake 3.16 or newer is required. Debian 11 ships cmake 3.18, Debian 12 ships cmake 3.25.
No external C++ libraries are needed beyond the standard C++17 library (included with GCC 8+).





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
sudo betterconn --start    # apply all optimizations
betterconn --status        # live stats: speed, ping, current settings
sudo betterconn --stop     # revert everything to original settings
betterconn --help          # usage
```