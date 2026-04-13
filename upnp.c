/*
 * upnp.c — UPnP IGD port mapping, zero external dependencies.
 *
 * Drop upnp.c + upnp.h into any C project.  No build system, no third-party
 * dependencies — only the platform socket library (Winsock2 on Windows,
 * POSIX sockets on Linux/macOS/Android).  See upnp.h for the public API.
 *
 * ════════════════════════════════════════════════════════════════════════════
 * OVERVIEW
 * ════════════════════════════════════════════════════════════════════════════
 *
 * A from-scratch UPnP Internet Gateway Device (IGD) client implementing the
 * minimum subset of UPnP Device Architecture 1.1 needed to open and close a
 * public TCP or UDP port mapping.  No libminiupnpc, no libupnp, no external
 * dependencies of any kind — only POSIX sockets (or Winsock2) and libc.
 *
 * The design is a direct C port of zzncat-go/upnp.go, preserving identical
 * protocol logic and fallback behaviour across both implementations.
 *
 * ════════════════════════════════════════════════════════════════════════════
 * PROTOCOL FLOW  (five steps)
 * ════════════════════════════════════════════════════════════════════════════
 *
 * 1. SSDP M-SEARCH  ─────────────────────────────────────────────────────────
 *
 *    Simple Service Discovery Protocol (SSDP, part of UPnP DA 1.1 §1.3).
 *    An M-SEARCH request is sent as a UDP multicast to 239.255.255.250:1900
 *    (the SSDP reserved address).  We search for three service types in order
 *    of preference:
 *
 *      urn:schemas-upnp-org:service:WANIPConnection:2   (IGDv2)
 *      urn:schemas-upnp-org:service:WANIPConnection:1   (IGDv1, most routers)
 *      urn:schemas-upnp-org:service:WANPPPConnection:1  (PPPoE gateways)
 *
 *    Multi-interface send strategy: on machines with multiple network adapters
 *    (WSL virtual NICs, Hyper-V switches, VPN tap devices, etc.) the OS may
 *    route a 0.0.0.0-bound multicast out a virtual interface that the physical
 *    router never sees.  To guarantee delivery, we enumerate every non-loopback
 *    non-link-local IPv4 address via GetAdaptersAddresses (Windows) or
 *    getifaddrs (POSIX), bind a separate send socket to each address, and send
 *    M-SEARCH on all of them.  A single receive socket bound to 0.0.0.0
 *    captures replies from any interface.
 *
 *    Timeout: 3 seconds.  On success the router's reply contains a LOCATION
 *    header with the URL of its device description XML.
 *
 * 2. Direct gateway probe  ──────────────────────────────────────────────────
 *
 *    Fallback when SSDP is filtered (some router firmware blocks multicast).
 *    We derive candidate gateway addresses by taking every local /24 and
 *    trying the .1 and .254 host addresses, then attempting HTTP GET on four
 *    common IGD XML paths (port 1900/5000/49152, paths /igd.xml /rootDesc.xml)
 *    until one responds with a valid device description.
 *
 * 3. Device description fetch  ──────────────────────────────────────────────
 *
 *    HTTP/1.0 GET of the LOCATION URL (or a probe URL from step 2).  We parse
 *    the response with a minimal XML scraper — no DOM, no SAX, no external
 *    parser.  The scraper walks the text looking for <serviceType> elements
 *    matching our target service types, then extracts the sibling <controlURL>
 *    from the enclosing <service> block.  Relative controlURLs are resolved
 *    against the base URL (derived from the LOCATION URL or an explicit
 *    <URLBase> element if the router provides one).
 *
 *    Why no XML library?  The information we need is in a small, well-known
 *    subset of the document.  A full XML parser would be a significant
 *    dependency for three string extractions.  The Go implementation uses the
 *    same strategy (encoding/xml for structure, direct string search for
 *    fallback).  The C scraper uses stristr() + xml_extract() for the same
 *    purpose.
 *
 * 4. SOAP actions  ──────────────────────────────────────────────────────────
 *
 *    All three SOAP calls (GetExternalIPAddress, AddPortMapping,
 *    DeletePortMapping) share a single soap_call() helper that:
 *      a. Builds a SOAP 1.1 envelope as a plain C string.
 *      b. Sends it as an HTTP/1.0 POST with Content-Type and SOAPAction headers.
 *      c. Accumulates the response into a growable Sbuf.
 *      d. On HTTP 200: returns the response body for the caller to parse.
 *      e. On HTTP 500: extracts <errorCode> and <errorDescription> from the
 *         UPnP SOAP fault envelope and prints a human-readable error.
 *
 *    AddPortMapping lease strategy: we first request NewLeaseDuration=3600
 *    (one hour).  Some routers (notably Eero) return SOAP error 402
 *    (InvalidArgs) for any non-zero lease duration — they only accept 0, which
 *    means "router-managed lifetime".  If the first attempt fails we retry
 *    immediately with lease=0.
 *
 * 5. LAN IP detection  ──────────────────────────────────────────────────────
 *
 *    When building the AddPortMapping request we need the internal (LAN) IP
 *    address to give the router as NewInternalClient.  Rather than guessing,
 *    we use a UDP connect() + getsockname() trick: we "connect" a UDP socket
 *    toward the IGD host (no packets sent — UDP connect() just sets the kernel
 *    routing table entry for the socket) then read back the local address the
 *    kernel chose.  This guarantees we use whichever interface the OS would
 *    actually route toward the gateway.
 *
 * ════════════════════════════════════════════════════════════════════════════
 * CLEANUP STRATEGY  (why we don't delete on clean exit)
 * ════════════════════════════════════════════════════════════════════════════
 *
 * DeletePortMapping is sent ONLY on SIGINT/SIGTERM (Ctrl+C), never on normal
 * program exit.  This is intentional.
 *
 * Background: on Windows, Hyper-V's WinNAT service maintains a hairpin NAT
 * translation table for connections that loop back through the public IP
 * (i.e. LAN client → WAN IP → same LAN host).  When a UPnP mapping is
 * deleted and immediately re-added (as happens if we clean up on exit and
 * re-run the listener), WinNAT retains the now-stale translation entry for
 * the old session.  New hairpin connections hit the stale entry and are
 * silently dropped until WinNAT ages it out or the service is restarted.
 *
 * By leaving the mapping on the router across session restarts, the Eero's
 * external port stays open and WinNAT's table entry ages out naturally between
 * sessions, eliminating the staleness window.  The mapping is cleaned up on
 * explicit user termination and on natural Eero lease expiry.
 *
 * ════════════════════════════════════════════════════════════════════════════
 * PLATFORM NOTES
 * ════════════════════════════════════════════════════════════════════════════
 *
 * Windows:  Uses GetAdaptersAddresses (iphlpapi) for interface enumeration.
 *           Signal handling via SetConsoleCtrlHandler (handles Ctrl+C,
 *           Ctrl+Break, and window close events).
 *           The #pragma comment(lib, ...) directives are MSVC hints; gcc
 *           ignores them — pass -lws2_32 -liphlpapi on the command line.
 *
 * POSIX:    Uses getifaddrs for interface enumeration.
 *           Signal handling via sigaction(SIGINT/SIGTERM).
 *           Compiles cleanly on Linux (glibc and Bionic/Termux) and macOS.
 *
 * ════════════════════════════════════════════════════════════════════════════
 * PUBLIC API  (see upnp.h)
 * ════════════════════════════════════════════════════════════════════════════
 *
 *   int  upnp_request_mapping(port, proto, verbosity, &mapping)
 *        Full discovery + AddPortMapping.  Fills UPnPMapping on success.
 *
 *   void upnp_release_mapping(&mapping)
 *        Sends DeletePortMapping.  Safe to call multiple times.
 *
 *   void upnp_register_cleanup(&mapping)
 *        Installs signal handler that calls upnp_release_mapping before exit.
 *        Call once after upnp_request_mapping succeeds.
 */

#include "upnp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  #pragma comment(lib, "iphlpapi.lib")
  #pragma comment(lib, "ws2_32.lib")
  #define sock_close(s)   closesocket(s)
  #define sleep_ms(ms)    Sleep(ms)
#else
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <ifaddrs.h>
  #include <unistd.h>
  #include <signal.h>
  #include <errno.h>
  #define sock_close(s)   close(s)
  #define sleep_ms(ms)    usleep((ms)*1000)
  typedef int SOCKET;
  #define INVALID_SOCKET  (-1)
  #define SOCKET_ERROR    (-1)
#endif

/* ── constants ─────────────────────────────────────────────────────────────── */

#define SSDP_MCAST_ADDR  "239.255.255.250"
#define SSDP_PORT        1900
#define SSDP_TIMEOUT_MS  3000
#define HTTP_TIMEOUT_MS  5000
#define UPNP_LEASE       3600
#define UPNP_BUF_SIZE    8192
#define UPNP_SMALL_BUF   2048

static const char *SSDP_TARGETS[] = {
    "urn:schemas-upnp-org:service:WANIPConnection:2",
    "urn:schemas-upnp-org:service:WANIPConnection:1",
    "urn:schemas-upnp-org:service:WANPPPConnection:1",
    NULL
};

/* ── internal types ─────────────────────────────────────────────────────────── */

/* Simple growable string for HTTP response accumulation */
typedef struct { char *buf; size_t len; size_t cap; } Sbuf;

static int sbuf_init(Sbuf *s)  { s->buf = malloc(UPNP_BUF_SIZE); s->len = 0; s->cap = UPNP_BUF_SIZE; return s->buf ? 0 : -1; }
static void sbuf_free(Sbuf *s) { free(s->buf); s->buf = NULL; s->len = s->cap = 0; }
static int sbuf_append(Sbuf *s, const char *data, size_t n) {
    if (s->len + n + 1 > s->cap) {
        size_t nc = s->cap * 2 + n + 1;
        char *nb = realloc(s->buf, nc);
        if (!nb) return -1;
        s->buf = nb; s->cap = nc;
    }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = '\0';
    return 0;
}

/* ── string helpers ─────────────────────────────────────────────────────────── */

/* Case-insensitive substring search */
static const char *stristr(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < nlen; i++)
            if (tolower((unsigned char)hay[i]) != tolower((unsigned char)needle[i])) break;
        if (i == nlen) return hay;
    }
    return NULL;
}

/* Extract text between <tag> and </tag> into out[out_size]. Returns 0 on success. */
static int xml_extract(const char *xml, const char *tag, char *out, size_t out_size) {
    char open[128], close[128];
    snprintf(open,  sizeof(open),  "<%s>",  tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *s = stristr(xml, open);
    if (!s) return -1;
    s += strlen(open);
    const char *e = stristr(s, close);
    if (!e) return -1;
    size_t len = (size_t)(e - s);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, s, len);
    out[len] = '\0';
    /* trim whitespace */
    while (out[0] && isspace((unsigned char)out[0])) memmove(out, out+1, strlen(out));
    size_t l = strlen(out);
    while (l > 0 && isspace((unsigned char)out[l-1])) out[--l] = '\0';
    return 0;
}

/* Trim leading/trailing whitespace in-place */
static void trim(char *s) {
    size_t l; char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    l = strlen(s);
    while (l > 0 && isspace((unsigned char)s[l-1])) s[--l] = '\0';
}

/* ── socket timeout helper ──────────────────────────────────────────────────── */

static void set_recv_timeout(SOCKET s, int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
#else
    struct timeval tv = { ms/1000, (ms%1000)*1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

/* ── interface enumeration ──────────────────────────────────────────────────── */

/*
 * Fill ips[] with non-loopback, non-link-local IPv4 addresses.
 * Returns number of addresses found.  Falls back to "0.0.0.0" if none.
 */
static int local_ipv4_addrs(char ips[][UPNP_IP_MAX], int max_ips, int verbosity) {
    int count = 0;

#ifdef _WIN32
    /* Windows: use GetAdaptersAddresses */
    ULONG bufsz = 15000;
    IP_ADAPTER_ADDRESSES *addrs = NULL, *a;
    for (int tries = 0; tries < 3 && count == 0; tries++) {
        addrs = (IP_ADAPTER_ADDRESSES*)malloc(bufsz);
        if (!addrs) break;
        ULONG ret = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,
            NULL, addrs, &bufsz);
        if (ret == ERROR_BUFFER_OVERFLOW) { free(addrs); addrs = NULL; continue; }
        if (ret != NO_ERROR) { free(addrs); break; }
        for (a = addrs; a && count < max_ips; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            IP_ADAPTER_UNICAST_ADDRESS *ua;
            for (ua = a->FirstUnicastAddress; ua && count < max_ips; ua = ua->Next) {
                struct sockaddr_in *sin = (struct sockaddr_in*)ua->Address.lpSockaddr;
                if (sin->sin_family != AF_INET) continue;
                unsigned long ip4 = ntohl(sin->sin_addr.s_addr);
                /* skip loopback (127/8) and link-local (169.254/16) */
                if ((ip4 >> 24) == 127) continue;
                if ((ip4 >> 16) == 0xA9FE) continue;
                inet_ntop(AF_INET, &sin->sin_addr, ips[count], UPNP_IP_MAX);
                if (verbosity >= 2) {
                    char wname[256] = {0};
                    WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                        wname, sizeof(wname), NULL, NULL);
                    fprintf(stderr, "[UPnP] will M-SEARCH on interface %s (%s)\n",
                            wname, ips[count]);
                }
                count++;
            }
        }
        free(addrs);
    }
#else
    /* POSIX: use getifaddrs */
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) goto fallback;
    for (ifa = ifap; ifa && count < max_ips; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        struct sockaddr_in *sin = (struct sockaddr_in*)ifa->ifa_addr;
        unsigned long ip4 = ntohl(sin->sin_addr.s_addr);
        if ((ip4 >> 24) == 127) continue;
        if ((ip4 >> 16) == 0xA9FE) continue;  /* 169.254/16 */
        inet_ntop(AF_INET, &sin->sin_addr, ips[count], UPNP_IP_MAX);
        if (verbosity >= 2)
            fprintf(stderr, "[UPnP] will M-SEARCH on interface %s (%s)\n",
                    ifa->ifa_name, ips[count]);
        count++;
    }
    freeifaddrs(ifap);
#endif

    if (count == 0) {
        strncpy(ips[0], "0.0.0.0", UPNP_IP_MAX);
        count = 1;
    }
    return count;
}

/* ── SSDP ───────────────────────────────────────────────────────────────────── */

/*
 * Send M-SEARCH on every local interface, listen for LOCATION header.
 * Returns 0 and fills location[UPNP_URL_MAX] on success.
 */
static int ssdp_discover(char location[UPNP_URL_MAX], int verbosity) {
    char local_ips[16][UPNP_IP_MAX];
    int  n_ips;
    struct sockaddr_in dst;
    char buf[UPNP_SMALL_BUF];

    n_ips = local_ipv4_addrs(local_ips, 16, verbosity);

    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(SSDP_PORT);
    inet_pton(AF_INET, SSDP_MCAST_ADDR, &dst.sin_addr);

    /* Single receive socket on 0.0.0.0 captures replies from any interface */
    SOCKET recv_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (recv_sock == INVALID_SOCKET) return -1;

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = 0;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(recv_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) != 0) {
        sock_close(recv_sock); return -1;
    }
    set_recv_timeout(recv_sock, SSDP_TIMEOUT_MS);

    /* Send M-SEARCH out each interface */
    for (int i = 0; i < n_ips; i++) {
        SOCKET send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (send_sock == INVALID_SOCKET) continue;

        struct sockaddr_in src;
        memset(&src, 0, sizeof(src));
        src.sin_family = AF_INET;
        src.sin_port   = 0;
        inet_pton(AF_INET, local_ips[i], &src.sin_addr);
        bind(send_sock, (struct sockaddr*)&src, sizeof(src));

        for (int t = 0; SSDP_TARGETS[t]; t++) {
            int msglen = snprintf(buf, sizeof(buf),
                "M-SEARCH * HTTP/1.1\r\n"
                "HOST: %s:%d\r\n"
                "MAN: \"ssdp:discover\"\r\n"
                "MX: 2\r\n"
                "ST: %s\r\n"
                "\r\n",
                SSDP_MCAST_ADDR, SSDP_PORT, SSDP_TARGETS[t]);
            sendto(send_sock, buf, msglen, 0,
                   (struct sockaddr*)&dst, sizeof(dst));
        }
        sock_close(send_sock);
    }

    /* Receive loop until timeout */
    while (1) {
        int n = (int)recv(recv_sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;
        buf[n] = '\0';

        /* Case-insensitive scan for LOCATION: header */
        char *line = buf;
        while (line && *line) {
            char *end = strstr(line, "\r\n");
            char save = 0;
            if (end) { save = *end; *end = '\0'; }

            if (strncasecmp(line, "LOCATION:", 9) == 0) {
                char *loc = line + 9;
                while (*loc == ' ') loc++;
                strncpy(location, loc, UPNP_URL_MAX-1);
                location[UPNP_URL_MAX-1] = '\0';
                trim(location);
                if (verbosity >= 2)
                    fprintf(stderr, "[UPnP] SSDP response, location: %s\n", location);
                sock_close(recv_sock);
                return 0;
            }

            if (end) { *end = save; line = end + 2; } else break;
        }
    }

    sock_close(recv_sock);
    return -1;
}

/* ── HTTP client ─────────────────────────────────────────────────────────────── */

/*
 * Parse URL into host, port, and path components.
 * url must start with "http://".
 */
static int parse_url(const char *url, char *host, size_t hsz,
                     int *port, char *path, size_t psz) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8; /* treat as http */

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    /* Extract host:port */
    if (colon && (!slash || colon < slash)) {
        size_t hl = (size_t)(colon - p);
        if (hl >= hsz) hl = hsz-1;
        memcpy(host, p, hl); host[hl] = '\0';
        *port = atoi(colon+1);
    } else {
        size_t hl = slash ? (size_t)(slash - p) : strlen(p);
        if (hl >= hsz) hl = hsz-1;
        memcpy(host, p, hl); host[hl] = '\0';
        *port = 80;
    }

    /* Extract path */
    if (slash) {
        strncpy(path, slash, psz-1); path[psz-1] = '\0';
    } else {
        strncpy(path, "/", psz-1);
    }
    return 0;
}

/*
 * Open a TCP connection to host:port.  Returns INVALID_SOCKET on failure.
 */
static SOCKET tcp_connect(const char *host, int port, int timeout_ms) {
    struct addrinfo hints, *res, *ai;
    char portstr[16];
    SOCKET s = INVALID_SOCKET;
    (void)timeout_ms; /* use blocking connect; IGD is on LAN, should be fast */

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof(portstr), "%d", port);

    if (getaddrinfo(host, portstr, &hints, &res) != 0) return INVALID_SOCKET;
    for (ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        set_recv_timeout(s, timeout_ms);
        if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        sock_close(s); s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return s;
}

/*
 * Perform an HTTP request (GET or POST).
 * Returns 0 and fills resp_body on success.
 * resp_status receives the HTTP status code.
 */
static int http_request(const char *method, const char *url,
                        const char *extra_headers, /* may be NULL */
                        const char *body,          /* may be NULL */
                        Sbuf *resp_body, int *resp_status) {
    char host[UPNP_HOST_MAX], path[UPNP_URL_MAX];
    int  port;
    char req[UPNP_BUF_SIZE];
    char tmp[1024];
    int  n, hdr_done = 0;
    size_t body_len = body ? strlen(body) : 0;

    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0)
        return -1;

    SOCKET s = tcp_connect(host, port, HTTP_TIMEOUT_MS);
    if (s == INVALID_SOCKET) return -1;

    /* Build request */
    n = snprintf(req, sizeof(req),
        "%s %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n",
        method, path, host, port);

    if (extra_headers)
        n += snprintf(req+n, sizeof(req)-n, "%s", extra_headers);

    if (body_len > 0)
        n += snprintf(req+n, sizeof(req)-n,
                      "Content-Length: %lu\r\n", (unsigned long)body_len);

    n += snprintf(req+n, sizeof(req)-n, "\r\n");

    /* Send headers */
    if (send(s, req, n, 0) != n) { sock_close(s); return -1; }
    /* Send body if any */
    if (body_len > 0)
        send(s, body, (int)body_len, 0);

    /* Read response */
    sbuf_init(resp_body);
    while ((n = (int)recv(s, tmp, sizeof(tmp), 0)) > 0)
        sbuf_append(resp_body, tmp, n);
    sock_close(s);

    if (resp_body->len == 0) { sbuf_free(resp_body); return -1; }

    /* Parse status line */
    *resp_status = 0;
    const char *sp = strstr(resp_body->buf, " ");
    if (sp) *resp_status = atoi(sp+1);

    /* Advance past headers to body */
    const char *body_start = strstr(resp_body->buf, "\r\n\r\n");
    if (!body_start) { /* try \n\n */ body_start = strstr(resp_body->buf, "\n\n"); if (body_start) body_start += 2; }
    else body_start += 4;

    if (body_start) {
        size_t body_offset = (size_t)(body_start - resp_body->buf);
        memmove(resp_body->buf, resp_body->buf + body_offset,
                resp_body->len - body_offset + 1);
        resp_body->len -= body_offset;
    }

    return 0;
    /* fall through: do not suppress warnings from hdr_done (unused) */
    (void)hdr_done;
}

/* ── XML: device description parsing ────────────────────────────────────────── */

/*
 * Walk XML text looking for a <serviceType> matching one of SSDP_TARGETS,
 * then extract the sibling <controlURL>.
 * Fills svc->control_url and svc->service_type.
 */
static int find_service_in_xml(const char *xml, const char *base_url, IGDService *svc) {
    const char *p = xml;
    while ((p = stristr(p, "<serviceType>")) != NULL) {
        char stype[128];
        if (xml_extract(p, "serviceType", stype, sizeof(stype)) != 0) { p++; continue; }

        int matched = 0;
        for (int t = 0; SSDP_TARGETS[t]; t++) {
            if (strcmp(stype, SSDP_TARGETS[t]) == 0) { matched = 1; break; }
        }
        if (!matched) { p++; continue; }

        /* Found matching serviceType — extract controlURL from same <service> block */
        /* Search backward for the opening <service> tag */
        const char *service_start = p;
        for (const char *q = p; q > xml; q--) {
            if (strncasecmp(q, "<service>", 9) == 0 || strncasecmp(q, "<service ", 9) == 0) {
                service_start = q; break;
            }
        }
        char ctrl[UPNP_URL_MAX];
        if (xml_extract(service_start, "controlURL", ctrl, sizeof(ctrl)) != 0) { p++; continue; }

        /* Resolve relative controlURL against base_url */
        if (strncmp(ctrl, "http://", 7) == 0 || strncmp(ctrl, "https://", 8) == 0) {
            strncpy(svc->control_url, ctrl, UPNP_URL_MAX-1);
        } else {
            /* base_url is "http://host:port" (no trailing slash) */
            if (ctrl[0] != '/') {
                snprintf(svc->control_url, UPNP_URL_MAX, "%s/%s", base_url, ctrl);
            } else {
                snprintf(svc->control_url, UPNP_URL_MAX, "%s%s", base_url, ctrl);
            }
        }
        svc->control_url[UPNP_URL_MAX-1] = '\0';
        strncpy(svc->service_type, stype, sizeof(svc->service_type)-1);
        svc->service_type[sizeof(svc->service_type)-1] = '\0';

        /* Extract igd_host from base_url: strip "http://", keep "host:port" */
        const char *h = base_url;
        if (strncmp(h, "http://",  7) == 0) h += 7;
        if (strncmp(h, "https://", 8) == 0) h += 8;
        strncpy(svc->igd_host, h, UPNP_HOST_MAX-1);
        svc->igd_host[UPNP_HOST_MAX-1] = '\0';
        /* strip any trailing path from igd_host */
        char *slash = strchr(svc->igd_host, '/');
        if (slash) *slash = '\0';

        return 0;
    }
    return -1;
}

/*
 * HTTP GET desc_url, parse XML, fill svc.
 * Returns 0 on success.
 */
static int fetch_control_url(const char *desc_url, int verbosity, IGDService *svc) {
    Sbuf body;
    int status = 0;

    if (http_request("GET", desc_url, NULL, NULL, &body, &status) != 0) return -1;
    if (status != 200) { sbuf_free(&body); return -1; }

    /* Determine base URL for resolving relative controlURL paths.
     * base = "http://host:port"  (everything up to the first path component). */
    char base[UPNP_URL_MAX];
    const char *p = desc_url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    const char *first_slash = strchr(p, '/');
    if (first_slash) {
        size_t prefix = (size_t)(first_slash - desc_url);
        if (prefix >= UPNP_URL_MAX) prefix = UPNP_URL_MAX-1;
        memcpy(base, desc_url, prefix);
        base[prefix] = '\0';
    } else {
        strncpy(base, desc_url, UPNP_URL_MAX-1);
    }

    /* URLBase element overrides the derived base if present */
    char urlbase[UPNP_URL_MAX];
    if (xml_extract(body.buf, "URLBase", urlbase, sizeof(urlbase)) == 0 && urlbase[0]) {
        /* strip trailing slash */
        size_t l = strlen(urlbase);
        while (l > 0 && urlbase[l-1] == '/') urlbase[--l] = '\0';
        size_t cplen = strlen(urlbase);
        if (cplen >= UPNP_URL_MAX) cplen = UPNP_URL_MAX - 1;
        memcpy(base, urlbase, cplen);
        base[cplen] = '\0';
    }

    int ret = find_service_in_xml(body.buf, base, svc);
    sbuf_free(&body);

    if (ret == 0 && verbosity >= 2)
        fprintf(stderr, "[UPnP] found %s at %s\n", svc->service_type, svc->control_url);

    return ret;
}

/* ── gateway probe ───────────────────────────────────────────────────────────── */

/*
 * Generate candidate IGD description URLs from local addresses.
 * Tries gateway .1 and .254 on each local /24, with common paths/ports.
 * urls must point to an array of at least max_urls char[UPNP_URL_MAX] buffers.
 * Returns count of URLs generated.
 */
static int igd_candidate_urls(char urls[][UPNP_URL_MAX], int max_urls) {
    static const char *PATHS[] = {
        "http://%s:1900/igd.xml",
        "http://%s:1900/rootDesc.xml",
        "http://%s:5000/rootDesc.xml",    /* Synology NAS, QNAP */
        "http://%s:8080/rootDesc.xml",   /* ASUS */
        "http://%s:49000/igddesc.xml",   /* Fritz!Box (AVM) */
        "http://%s:49000/rootDesc.xml",  /* Fritz!Box alternate */
        "http://%s:49152/rootDesc.xml",  /* Netgear, Belkin */
        NULL
    };
    static const unsigned char LAST_OCTETS[] = {1, 254};

    int count = 0;
    char seen_gws[32][UPNP_IP_MAX];
    int  seen_count = 0;

#ifdef _WIN32
    ULONG bufsz = 15000;
    IP_ADAPTER_ADDRESSES *addrs = (IP_ADAPTER_ADDRESSES*)malloc(bufsz);
    if (!addrs) return 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, addrs, &bufsz) != NO_ERROR) {
        free(addrs); return 0;
    }
    for (IP_ADAPTER_ADDRESSES *a = addrs; a && count < max_urls; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        for (IP_ADAPTER_UNICAST_ADDRESS *ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            struct sockaddr_in *sin = (struct sockaddr_in*)ua->Address.lpSockaddr;
            if (sin->sin_family != AF_INET) continue;
            unsigned long ip4 = ntohl(sin->sin_addr.s_addr);
            if ((ip4 >> 24) == 127 || (ip4 >> 16) == 0xA9FE) continue;
            for (int lo = 0; lo < 2; lo++) {
                char gw[UPNP_IP_MAX];
                snprintf(gw, sizeof(gw), "%lu.%lu.%lu.%d",
                         (ip4>>24)&0xff, (ip4>>16)&0xff, (ip4>>8)&0xff, LAST_OCTETS[lo]);
                int dup = 0;
                for (int k = 0; k < seen_count; k++) if (strcmp(seen_gws[k], gw)==0) { dup=1; break; }
                if (dup || seen_count >= 32) continue;
                strncpy(seen_gws[seen_count++], gw, UPNP_IP_MAX-1);
                for (int pi = 0; PATHS[pi] && count < max_urls; pi++)
                    snprintf(urls[count++], UPNP_URL_MAX, PATHS[pi], gw);
            }
        }
    }
    free(addrs);
#else
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) return 0;
    for (ifa = ifap; ifa && count < max_urls; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK || !(ifa->ifa_flags & IFF_UP)) continue;
        struct sockaddr_in *sin = (struct sockaddr_in*)ifa->ifa_addr;
        unsigned long ip4 = ntohl(sin->sin_addr.s_addr);
        if ((ip4>>24)==127 || (ip4>>16)==0xA9FE) continue;
        for (int lo = 0; lo < 2; lo++) {
            char gw[UPNP_IP_MAX];
            snprintf(gw, sizeof(gw), "%lu.%lu.%lu.%d",
                     (ip4>>24)&0xff, (ip4>>16)&0xff, (ip4>>8)&0xff, LAST_OCTETS[lo]);
            int dup = 0;
            for (int k = 0; k < seen_count; k++) if (strcmp(seen_gws[k], gw)==0) { dup=1; break; }
            if (dup || seen_count >= 32) continue;
            strncpy(seen_gws[seen_count++], gw, UPNP_IP_MAX-1);
            for (int pi = 0; PATHS[pi] && count < max_urls; pi++)
                snprintf(urls[count++], UPNP_URL_MAX, PATHS[pi], gw);
        }
    }
    freeifaddrs(ifap);
#endif

    return count;
}

static int probe_gateway_igd(int verbosity, IGDService *svc) {
    char urls[64][UPNP_URL_MAX];
    int n = igd_candidate_urls(urls, 64);
    for (int i = 0; i < n; i++) {
        if (verbosity >= 2) fprintf(stderr, "[UPnP] probing %s\n", urls[i]);
        if (fetch_control_url(urls[i], verbosity, svc) == 0) return 0;
    }
    return -1;
}

/* ── IGD discovery ───────────────────────────────────────────────────────────── */

static int discover_igd(int verbosity, IGDService *svc) {
    if (verbosity >= 2) fprintf(stderr, "[UPnP] discovering IGD via SSDP...\n");

    char location[UPNP_URL_MAX];
    if (ssdp_discover(location, verbosity) == 0) {
        if (fetch_control_url(location, verbosity, svc) == 0)
            return 0;
        if (verbosity >= 2)
            fprintf(stderr, "[UPnP] could not parse %s, falling back\n", location);
    }

    if (verbosity >= 2) fprintf(stderr, "[UPnP] SSDP timeout, trying direct gateway probe...\n");
    return probe_gateway_igd(verbosity, svc);
}

/* ── SOAP ─────────────────────────────────────────────────────────────────────── */

static int soap_call(const IGDService *svc, const char *action,
                     const char *args, Sbuf *resp_body) {
    char soap_body[UPNP_BUF_SIZE];
    snprintf(soap_body, sizeof(soap_body),
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
        " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>",
        action, svc->service_type, args ? args : "", action);

    char headers[512];
    snprintf(headers, sizeof(headers),
        "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \"%s#%s\"\r\n",
        svc->service_type, action);

    int status = 0;
    if (http_request("POST", svc->control_url, headers, soap_body, resp_body, &status) != 0)
        return -1;

    if (status == 500) {
        /* Parse UPnP SOAP fault */
        char code[32]="", desc[128]="";
        xml_extract(resp_body->buf, "errorCode", code, sizeof(code));
        xml_extract(resp_body->buf, "errorDescription", desc, sizeof(desc));
        if (code[0])
            fprintf(stderr, "[UPnP] SOAP %s error %s: %s\n", action, code, desc);
        else
            fprintf(stderr, "[UPnP] SOAP %s: fault (HTTP 500)\n", action);
        sbuf_free(resp_body);
        return -1;
    }
    if (status != 200) {
        fprintf(stderr, "[UPnP] SOAP %s: HTTP %d\n", action, status);
        sbuf_free(resp_body);
        return -1;
    }
    return 0;
}

/* ── LAN IP detection ─────────────────────────────────────────────────────────── */

/*
 * Determine which local IP address is used to reach igd_host.
 * Uses a UDP connect() + getsockname() — no packets sent.
 */
static int lan_ip_toward_host(const char *igd_host, char *out, size_t out_size) {
    /* igd_host may be "host:port" — strip port for connect */
    char host[UPNP_HOST_MAX];
    strncpy(host, igd_host, sizeof(host)-1); host[sizeof(host)-1] = '\0';
    char *colon = strrchr(host, ':');
    int   port  = SSDP_PORT;
    if (colon) { port = atoi(colon+1); *colon = '\0'; }

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        /* fallback: use SSDP multicast address */
        strncpy(portstr, "1900", sizeof(portstr));
        if (getaddrinfo(SSDP_MCAST_ADDR, portstr, &hints, &res) != 0) return -1;
    }

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); return -1; }

    int ok = (connect(s, res->ai_addr, (int)res->ai_addrlen) == 0);
    freeaddrinfo(res);

    if (ok) {
        struct sockaddr_in local;
        socklen_t len = sizeof(local);
        if (getsockname(s, (struct sockaddr*)&local, &len) == 0)
            inet_ntop(AF_INET, &local.sin_addr, out, (socklen_t)out_size);
        else ok = 0;
    }
    sock_close(s);
    return ok ? 0 : -1;
}

/* ── public API ──────────────────────────────────────────────────────────────── */

int upnp_request_mapping(unsigned short port, const char *proto,
                         int verbosity, UPnPMapping *m) {
    memset(m, 0, sizeof(*m));

    if (discover_igd(verbosity, &m->svc) != 0) {
        fprintf(stderr, "[UPnP] failed: no UPnP IGD found on LAN "
                        "(is UPnP enabled on your router?)\n");
        return -1;
    }

    char local_ip[UPNP_IP_MAX];
    if (lan_ip_toward_host(m->svc.igd_host, local_ip, sizeof(local_ip)) != 0) {
        fprintf(stderr, "[UPnP] failed: cannot determine local LAN IP\n");
        return -1;
    }
    if (verbosity >= 2)
        fprintf(stderr, "[UPnP] local LAN IP: %s\n", local_ip);

    /* GetExternalIPAddress */
    Sbuf resp;
    if (soap_call(&m->svc, "GetExternalIPAddress", NULL, &resp) != 0) return -1;
    if (xml_extract(resp.buf, "NewExternalIPAddress", m->ext_ip, sizeof(m->ext_ip)) != 0) {
        fprintf(stderr, "[UPnP] cannot find NewExternalIPAddress in response\n");
        sbuf_free(&resp); return -1;
    }
    sbuf_free(&resp);

    /* AddPortMapping — try finite lease first, then 0 for picky routers (e.g. Eero) */
    m->ext_port = port;
    strncpy(m->proto, proto, sizeof(m->proto)-1);

    char args[512];
    int ok = 0;
    for (int lease = UPNP_LEASE; ; lease = 0) {
        snprintf(args, sizeof(args),
            "<NewRemoteHost></NewRemoteHost>"
            "<NewExternalPort>%u</NewExternalPort>"
            "<NewProtocol>%s</NewProtocol>"
            "<NewInternalPort>%u</NewInternalPort>"
            "<NewInternalClient>%s</NewInternalClient>"
            "<NewEnabled>1</NewEnabled>"
            "<NewPortMappingDescription>zzncat %s/%u</NewPortMappingDescription>"
            "<NewLeaseDuration>%d</NewLeaseDuration>",
            port, proto, port, local_ip, proto, port, lease);
        Sbuf r2;
        if (soap_call(&m->svc, "AddPortMapping", args, &r2) == 0) {
            sbuf_free(&r2); ok = 1; break;
        }
        if (lease == 0) break; /* already tried lease=0, give up */
        if (verbosity >= 2)
            fprintf(stderr, "[UPnP] lease=%d rejected, retrying with lease=0\n", UPNP_LEASE);
    }
    if (!ok) {
        fprintf(stderr, "[UPnP] AddPortMapping failed\n");
        return -1;
    }

    printf("[UPnP] port mapping granted: %s:%u/%s -> %s:%u\n",
           m->ext_ip, port, proto, local_ip, port);
    printf("[UPnP] reachable at %s:%u\n", m->ext_ip, port);
    m->valid = 1;
    return 0;
}

void upnp_release_mapping(UPnPMapping *m) {
    if (!m || !m->valid) return;
    char args[256];
    snprintf(args, sizeof(args),
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>%u</NewExternalPort>"
        "<NewProtocol>%s</NewProtocol>",
        m->ext_port, m->proto);
    Sbuf resp;
    if (soap_call(&m->svc, "DeletePortMapping", args, &resp) == 0) {
        fprintf(stderr, "[UPnP] port mapping %s:%u/%s released\n",
                m->ext_ip, m->ext_port, m->proto);
        sbuf_free(&resp);
    }
    m->valid = 0;
}

/* Global pointer for signal handler */
static UPnPMapping *g_upnp_mapping = NULL;

#ifdef _WIN32
static BOOL WINAPI upnp_console_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT ||
        ctrl_type == CTRL_CLOSE_EVENT) {
        upnp_release_mapping(g_upnp_mapping);
        return FALSE; /* let default handler run (terminates process) */
    }
    return FALSE;
}
#else
static void upnp_signal_handler(int sig) {
    (void)sig;
    upnp_release_mapping(g_upnp_mapping);
    _exit(0);
}
#endif

void upnp_register_cleanup(UPnPMapping *m) {
    if (!m || !m->valid) return;
    g_upnp_mapping = m;
#ifdef _WIN32
    SetConsoleCtrlHandler(upnp_console_handler, TRUE);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = upnp_signal_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif
}
