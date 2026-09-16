# betterconn (v1.0.9)
Linux network optimizer focused on Debian. Maximizes connection quality for gaming, streaming, and general use by applying proven kernel-level and traffic tuning at runtime, with adaptive real-time adjustment and full backup and restore of original settings. Written by Christian (@pusheandoando)





## Real-world results
Measured on a 20 Mbps WiFi link using [Cloudflare Speed Test](https://speed.cloudflare.com/), the recommended tool to benchmark your connection before and after applying betterconn.

<div align="center">

### Default (no betterconn)
<img 
  src="assets/screenshots/v1.0.4/speedtest_default.png" 
  width="720" 
  alt="Speedtest without betterconn" 
/>

### Optimized (betterconn active)
<img 
  src="assets/screenshots/v1.0.4/speedtest_optim.png" 
  width="720" 
  alt="Speedtest with betterconn" 
/>

</div>

| Metric | Default | With betterconn | Change |
|---|---|---|---|
| Download | 24.2 Mbps | 35.9 Mbps | +48.3% |
| Upload | 5.87 Mbps | 11.6 Mbps | +97.6% |
| Latency | 178 ms | 29.0 ms | −83.7% |
| Loaded latency (down) | 216 ms | 36.5 ms | −83.1% |
| Loaded latency (up) | 172 ms | 29.0 ms | −83.1% |
| Jitter | 9.53 ms | 8.84 ms | −7.2% |
| Packet Loss | 0% | 0% | Same |

| Quality Score | Default | With betterconn |
|---|---|---|
| Video Streaming | Poor | Good |
| Online Gaming | Poor | Great |
| Video Chatting | Average | Great |





## What it does
- **TCP BBR** congestion control — measures actual bandwidth and RTT instead of relying on packet loss, reducing latency and improving throughput — [ACM Queue](https://queue.acm.org/detail.cfm?id=3022184)
- **CAKE** queue discipline, with `fq_codel` as the fallback when `sch_cake` is unavailable — active queue management with per-flow isolation and DSCP aware tins; eliminates bufferbloat without a rate cap, making it correct for endpoint use — [RFC 8290](https://datatracker.ietf.org/doc/html/rfc8290), [tc-cake(8)](https://man7.org/linux/man-pages/man8/tc-cake.8.html)
- **TCP ECN** — routers signal congestion by marking packets rather than dropping them, pairs directly with fq_codel — [RFC 3168](https://datatracker.ietf.org/doc/html/rfc3168)
- **TCP Fast Open** (value 3) — reduces connection setup round-trips — [RFC 7413](https://datatracker.ietf.org/doc/html/rfc7413)
- **RX/TX socket buffers** — dynamically sized by the adaptive tuner based on measured real-time throughput, from 4 MB (slow links) up to 32 MB (fast links)
- **tcp_autocorking disabled** — sends each write immediately, cutting queuing delay for interactive and gaming traffic — [LWN](https://lwn.net/Articles/576263/)
- **tcp_notsent_lowat** — tuned dynamically to reduce application-level buffering and keep request latency low
- **tcp_slow_start_after_idle = 0** — BBR does not throttle connections that were briefly idle
- **WiFi power save disabled** — prevents 20–100 ms idle latency spikes caused by the card sleeping between beacon intervals; made permanent via NetworkManager so it survives reconnects
- **Adaptive NIC interrupt coalescing** via ethtool — dynamically balances interrupt rate between low latency and high throughput
- **DSCP marking** on IPv4 and IPv6 for DNS, TCP handshakes, pure acknowledgements, small interactive UDP datagrams, Steam and WebRTC ports; mac80211 derives the WMM user priority from the DS field, so the same mark that picks the CAKE tin also picks the radio access category — [RFC 2474](https://www.rfc-editor.org/rfc/rfc2474)
- **Real-time adaptive tuner** — runs as a background daemon; reads `/proc/net/wireless` (RSSI, TX retries), `/proc/net/dev` (live throughput), and RTT via ping, then adjusts buffers, `netdev_budget`, `tcp_notsent_lowat`, and the fq_codel target without any user interaction
- **WiFi channel survey monitoring** — periodically samples `iw dev <iface> survey dump`, a standard mac80211 API (`NL80211_CMD_GET_SURVEY`) exposed by essentially every Linux WiFi driver with no firmware patching or special hardware required, to read per-channel busy/receive/transmit time and noise floor; this lets the tuner anticipate ambient airtime congestion and preemptively tighten the fq_codel target and retry-sensitive buffering before it shows up as RTT or retry spikes on the local link — [Ending the Anomaly, USENIX ATC 2017](https://www.usenix.org/conference/atc17/technical-sessions/presentation/hoilan-jorgesen), [Law, USENIX NSDI 2026](https://www.usenix.org/conference/nsdi26/presentation/shen-yibin), [Mortise, USENIX NSDI 2026](https://www.usenix.org/conference/nsdi26/presentation/shen-yixin)
- **Adaptive tiered traffic prioritization** — a background thread polls the focused window (falling back to the window under the mouse cursor) every 500 ms and combines a decay-aware estimate of focus recency with live per-process network activity into a continuously updated priority score for every observed process, moving each PID into the `cgroup v2` control group matching its current tier (Hot, Warm, Cool, Cold); `iptables` and `ip6tables` translate that control group into a DSCP value, which selects both the CAKE tin on the wire and the 802.11 access category on the air, with hysteresis and a minimum dwell time before a process changes tier — [cgroup v2 xt_cgroup match, LWN](https://lwn.net/Articles/679786/), [tc-cake(8)](https://man7.org/linux/man-pages/man8/tc-cake.8.html), [Ending the Anomaly, USENIX ATC 2017](https://www.usenix.org/conference/atc17/technical-sessions/presentation/hoilan-jorgesen)
- **Airtime queue limit tightening** — the mac80211 AQL budget of every access category is reduced below the driver default so the firmware holds less queued airtime, which is where most of the WiFi latency under load actually accumulates; the original values are saved and restored on stop

The window under the mouse cursor is used to identify which process the user is actively working with, since the cursor is always present regardless of which window manager feature set is available and does not depend on any single window holding input focus. On X11, betterconn resolves the window under the cursor directly via `xdotool getmouselocation` and `xdotool getwindowpid`. On Hyprland, it resolves the same way via `hyprctl cursorpos` intersected against the rectangles reported by `hyprctl clients`. Sway's IPC does not currently expose the compositor's global pointer position, so on Sway betterconn falls back to the focused container reported by `swaymsg -t get_tree` as the closest available proxy. On compositors without a supported backend (for example GNOME/Mutter or KWin), this feature degrades gracefully and all traffic falls back to the lowest priority tier. In addition to cursor tracking, betterconn also predicts companion processes that tend to be used alongside the one currently under the cursor (for example a game and its voice-chat client) by detecting alternating cursor-target patterns over time, and pre-promotes their priority tier accordingly.





## Security notice
betterconn prioritizes maximizing internet speed and stability over network security, betterconn is designed for trusted networks only (home, personal workplace). Do not use it on public networks such as airports, hotels, cafes, or universities where untrusted devices share the same network segment.

betterconn also runs a background thread that polls the window under the mouse cursor every 500 ms, via `xdotool` on X11 or via `hyprctl cursorpos`/`hyprctl clients` on Hyprland (falling back to the focused container reported by `swaymsg -t get_tree` on Sway, since Sway's IPC does not expose a global pointer position), feeding a scoring engine that ranks every observed process into one of four bandwidth tiers (Hot, Warm, Cool, Cold) based on cursor dwell time and live network activity. This means the process ID of whichever window is currently under the cursor, along with the PIDs of other processes with detected network activity, is continuously read for as long as betterconn is running. This data never leaves the machine, is not written to persistent storage beyond the transient cgroup assignment, and stops the moment `betterconn stop` is run.

The following settings applied by betterconn reduce your security posture:
- **TCP timestamps** — can expose system uptime to remote hosts
- **TCP Fast Open** — slightly weakens SYN-flood protection
- **Ephemeral port range widened** to 1024–65535
- **Socket buffer sizes increased** significantly, raising per-connection memory usage
- **iptables TOS marking** applied to DNS, HTTP/S, and common game ports
- **WiFi power saving disabled** permanently via NetworkManager





## Requirements
### Runtime dependencies
```bash
sudo apt install iptables iproute2 kmod iputils-ping ethtool iw xdotool
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
- Revert all settings to their original values, clean all residual files, and reboot:
```bash
sudo betterconn stop
```
- Show live stats: speed, ping, and current kernel settings:
```bash
sudo betterconn status
```
- Remove all betterconn files from the system (only if already stopped):
```bash
sudo betterconn clean
```





## Persistence
`betterconn start` survives reboots, forced shutdowns, and power loss. On apply, betterconn writes:
- `/etc/modules-load.d/betterconn.conf` — loads `tcp_bbr` and `xt_TOS` early at boot
- `/etc/sysctl.d/99-betterconn.conf` — all kernel parameters, applied by `systemd-sysctl` at every boot
- `/etc/betterconn/iptables-apply.sh` — re-applies QoS rules, qdisc, and WiFi power save after boot
- `/etc/systemd/system/betterconn.service` — systemd unit that runs the boot script then starts the adaptive daemon

`betterconn stop` drops every one of those files before touching anything else, so a failure later in the revert can never bring the tuning back on the next boot. It then restores `/proc/sys` from the saved backup, reloads the distribution's own sysctl configuration so that configuration is the last writer, restores the original NIC coalescing parameters and RPS/XPS masks, removes the iptables rules, unloads the kernel modules it loaded, and regenerates the initramfs. After `stop`, the machine behaves exactly as it did before betterconn was ever installed.