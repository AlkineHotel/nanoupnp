# nanoupnp

[![CI](https://github.com/AlkineHotel/c-upnp-igd/actions/workflows/ci.yml/badge.svg)](https://github.com/AlkineHotel/c-upnp-igd/actions/workflows/ci.yml)

A minimal, zero-dependency UPnP IGD port mapping client in C.

Drop `upnp.c` and `upnp.h` into any C project to open public ports on a
UPnP-capable router — no build system, no third-party libraries, no cmake.

**Tested on:** Windows x64 (MinGW + MSVC), Linux x86_64, Linux ARM64 (Termux/Android)

---

## Why not miniupnpc?

[miniupnpc](https://github.com/miniupnp/miniupnp) is the standard answer, but
it's 30+ files, requires a build step, and adds a meaningful dependency surface
to any project that uses it.

`nanoupnp` is two files. It implements exactly the four SOAP actions needed
to open and close a port mapping — nothing more.

---

## Usage

```c
#include "upnp.h"

// On Windows: call WSAStartup first.

UPnPMapping m = {0};
if (upnp_request_mapping(9999, "TCP", /*verbosity=*/1, &m) == 0) {
    upnp_register_cleanup(&m);   // releases mapping on Ctrl+C / SIGTERM
    printf("reachable at %s:%u\n", m.ext_ip, m.ext_port);
    // start your server here...
}
```

See [`example/map_port.c`](example/map_port.c) for a complete working example.

---

## Build

**Linux / macOS / Android (Termux):**
```bash
gcc -o yourprogram yourprogram.c upnp.c -I. -lpthread
```

**Windows (MinGW):**
```bash
gcc -o yourprogram.exe yourprogram.c upnp.c -I. -lws2_32 -liphlpapi
```

**Windows (MSVC):**
```
cl yourprogram.c upnp.c /I. ws2_32.lib iphlpapi.lib
```

That's it. No `./configure`, no cmake, no pkg-config.

---

## CI

GitHub Actions runs a **compile-only** build on every push and pull request:

- **Linux:** `make clean && make`
- **Windows (MinGW):** `make clean && make windows`

CI builds the library, the example program, and both test binaries. It does
**not** run the smoke test or the live reachability test, because those require
a real UPnP-capable router on the runner's local network.

---

## API

```c
// Discover IGD, get external IP, open port mapping.
// Returns 0 on success; fills *m.
// verbosity: 0=silent  1=result only  2=info  4=debug
int upnp_request_mapping(unsigned short port, const char *proto,
                         int verbosity, UPnPMapping *m);

// Send DeletePortMapping. Safe to call multiple times.
void upnp_release_mapping(UPnPMapping *m);

// Install SIGINT/SIGTERM handler that calls upnp_release_mapping before exit.
void upnp_register_cleanup(UPnPMapping *m);
```

`proto` is `"TCP"` or `"UDP"`.

---

## Protocol

Five steps, all implemented with raw sockets and libc — no HTTP library, no XML
parser, no SOAP framework:

1. **SSDP M-SEARCH** — UDP multicast to `239.255.255.250:1900`. Sent on every
   non-loopback IPv4 interface simultaneously so the packet egresses the correct
   physical NIC even on machines with WSL/Hyper-V/VPN virtual adapters.

2. **Direct gateway probe** — fallback when SSDP multicast is filtered. Derives
   candidate gateway addresses from local /24 prefixes and HTTP-probes common
   IGD description paths.

3. **Device description fetch** — HTTP/1.0 GET of the router's IGD XML.
   Extracts `controlURL` for `WANIPConnection` (v1/v2) or `WANPPPConnection`
   using a minimal text scraper. No DOM, no SAX.

4. **SOAP calls** — hand-built envelopes sent over plain TCP. Handles UPnP
   SOAP fault envelopes (HTTP 500 with `<errorCode>` / `<errorDescription>`).
   `AddPortMapping` tries `NewLeaseDuration=3600` first, falls back to `0` for
   routers (e.g. Eero) that reject finite leases.

5. **LAN IP detection** — `connect()` a UDP socket toward the IGD host and read
   back `getsockname()`. No packets sent; the kernel's routing table picks the
   correct LAN interface automatically.

Full protocol documentation is in the source file header of `upnp.c`.

---

## Cleanup strategy

`DeletePortMapping` is sent **only on SIGINT/SIGTERM**, not on clean program
exit. This is intentional.

On Windows, Hyper-V's WinNAT service caches hairpin NAT translation entries. If
a mapping is deleted and immediately re-added (as happens when a listener
restarts), WinNAT retains the stale entry and silently drops new hairpin
connections until it ages out. Leaving the mapping alive across restarts
eliminates this window entirely.

---

## Platform notes

| Platform | Interface enum | Signal handling |
|---|---|---|
| Windows | `GetAdaptersAddresses` (iphlpapi) | `SetConsoleCtrlHandler` |
| Linux / Android | `getifaddrs` | `sigaction(SIGINT/SIGTERM)` |
| macOS | `getifaddrs` | `sigaction(SIGINT/SIGTERM)` |

---

## Testing

**Local smoke test** — discovers IGD, maps port, verifies fields, releases.
Exits 0 on pass. This is not run in GitHub Actions CI because it requires a
real UPnP-capable router on the local network.
```bash
make test                        # Linux/macOS
.\test\test_upnp.exe             # Windows
```

**Live reachability test** — maps port, binds a real socket, holds it open for
external probing. This is a manual integration test, not a CI check.
```bash
.\test\test_live.exe             # TCP/9999 (default)
.\test\test_live.exe 9999 UDP    # UDP/9999

# From another terminal (hairpin NAT works fine from same LAN):
nping --tcp -p 9999 <ext_ip>
nping --udp -p 9999 <ext_ip>
nc -zv <ext_ip> 9999
```

TCP: nping receives SYN-ACK. UDP: listener stays alive, prints each arriving packet, sends echo reply. Press Ctrl+C to release the mapping and exit.

Note: routers typically rate-limit repeated probes from the same source, so 3/5 nping responses on TCP is normal and acceptable.

---



SSDP (step 1) is spec-compliant and works with any UPnP-enabled router.

The direct gateway probe fallback (step 2) tries these ports:

| Port  | Covers |
|-------|--------|
| 1900  | Eero, Apple AirPort, most consumer routers |
| 5000  | Synology, QNAP, some Netgear |
| 8080  | ASUS |
| 49000 | AVM Fritz!Box (`/igddesc.xml`) |
| 49152 | Netgear Nighthawk, Belkin, some Linksys |

The fallback is only reached when SSDP multicast is filtered. Most routers
respond to SSDP and never need it. If you hit a router where both SSDP and
the probe fail, open an issue with the port your router uses.

---

## License

MIT
