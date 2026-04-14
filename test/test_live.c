/*
 * test_live.c — interactive port-open test for c-upnp-igd
 *
 * Opens a UPnP port mapping AND binds a local socket on that port so
 * inbound traffic actually has somewhere to land.  Proves end-to-end
 * reachability, not just that the SOAP call succeeded.
 *
 * TCP mode: listens for connections; nping will get SYN-ACK.
 * UDP mode: recvfrom loop; sends an echo reply so nping sees a response.
 *
 * Usage:
 *   test_live [port] [proto]
 *   test_live 9999 TCP       <- default
 *   test_live 9999 UDP
 *
 * Suggested verification (hairpin NAT may work from same LAN; if not, use a different network):
 *   nping --tcp -p <ext_port> <ext_ip>
 *   nping --udp -p <ext_port> <ext_ip>
 *   nc -zv <ext_ip> <ext_port>
 *
 * Build (Linux/macOS):
 *   gcc -o test/test_live test/test_live.c upnp.c -I. -lpthread
 *
 * Build (Windows, MinGW):
 *   gcc -o test/test_live.exe test/test_live.c upnp.c -I. -lws2_32 -liphlpapi
 */

#include "upnp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define sock_close(s) closesocket(s)
  typedef int socklen_t;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define sock_close(s) close(s)
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
#endif

static void run_tcp_listener(unsigned short port) {
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { perror("socket"); return; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind"); sock_close(srv); return;
    }
    listen(srv, 8);

    printf("TCP listener bound on :%u -- waiting for connections...\n", port);
    printf("Press Ctrl+C to release the mapping and exit.\n\n");

    while (1) {
        struct sockaddr_in peer = {0};
        socklen_t plen = sizeof(peer);
        SOCKET conn = accept(srv, (struct sockaddr*)&peer, &plen);
        if (conn == INVALID_SOCKET) break;
        char ipstr[48];
        inet_ntop(AF_INET, &peer.sin_addr, ipstr, sizeof(ipstr));
        printf("  [TCP] connection from %s:%u\n", ipstr, ntohs(peer.sin_port));
        sock_close(conn);
    }
    sock_close(srv);
}

static void run_udp_listener(unsigned short port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) { perror("socket"); return; }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind"); sock_close(s); return;
    }

    printf("UDP listener bound on :%u -- waiting for packets...\n", port);
    printf("Press Ctrl+C to release the mapping and exit.\n\n");

#ifdef _WIN32
    /* Prime the Eero/Hyper-V NAT state table: some routers only open the
     * inbound UDP pinhole after seeing at least one outbound packet from
     * this socket.  Send a harmless zero-byte datagram to a public DNS
     * server — it won't reply, but the NAT table entry gets created. */
    {
        struct sockaddr_in prime = {0};
        prime.sin_family = AF_INET;
        prime.sin_port   = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &prime.sin_addr);
        sendto(s, "", 0, 0, (struct sockaddr*)&prime, sizeof(prime));
    }
#endif
    char buf[512];
    while (1) {
        struct sockaddr_in peer = {0};
        socklen_t plen = sizeof(peer);
        int n = (int)recvfrom(s, buf, sizeof(buf)-1, 0,
                              (struct sockaddr*)&peer, &plen);
        if (n < 0) {
#ifdef _WIN32
            /* Windows returns WSAECONNRESET when a previous send triggered
             * an ICMP port-unreachable.  Not fatal — just ignore and loop. */
            if (WSAGetLastError() == WSAECONNRESET) continue;
#endif
            break;
        }
        /* n == 0 is a valid empty UDP datagram (e.g. nping default) */
        buf[n] = '\0';
        char ipstr[48];
        inet_ntop(AF_INET, &peer.sin_addr, ipstr, sizeof(ipstr));
        printf("  [UDP] packet from %s:%u (%d payload bytes)\n", ipstr, ntohs(peer.sin_port), n);
        /* echo back so nping sees a reply */
        sendto(s, buf, n, 0, (struct sockaddr*)&peer, plen);
    }
    sock_close(s);
}

int main(int argc, char *argv[]) {
    unsigned short port  = 9999;
    const char    *proto = "TCP";

    if (argc >= 2) port  = (unsigned short)atoi(argv[1]);
    if (argc >= 3) proto = argv[2];

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    printf("c-upnp-igd live test\n");
    printf("--------------------\n");
    printf("Requesting %s/%u mapping...\n\n", proto, port);

    UPnPMapping m = {0};
    if (upnp_request_mapping(port, proto, 2, &m) != 0) {
        fprintf(stderr, "\nFailed to open port mapping.\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("\n--------------------\n");
    printf("Mapping is OPEN: %s:%u/%s\n\n", m.ext_ip, m.ext_port, m.proto);
    printf("Verify reachability (hairpin NAT works from same LAN):\n");
    printf("  nping --tcp -p %u %s\n", m.ext_port, m.ext_ip);
    printf("  nping --udp -p %u %s\n", m.ext_port, m.ext_ip);
    printf("  nc -zv %s %u\n\n", m.ext_ip, m.ext_port);

    upnp_register_cleanup(&m);

    if (strcmp(proto, "UDP") == 0)
        run_udp_listener(port);
    else
        run_tcp_listener(port);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
