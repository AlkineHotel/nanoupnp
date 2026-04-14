/*
 * map_port.c — minimal example using c-upnp-igd
 *
 * Opens a TCP port mapping, prints the public address, waits for Ctrl+C,
 * then releases the mapping before exit.
 *
 * Build (Linux/macOS):
 *   gcc -o map_port map_port.c ../upnp.c -I.. -lpthread
 *
 * Build (Windows, MinGW):
 *   gcc -o map_port.exe map_port.c ../upnp.c -I.. -lws2_32 -liphlpapi
 */

#include "upnp.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

int main(int argc, char *argv[]) {
    unsigned short port = 9999;
    int verbosity = 2;

    if (argc >= 2) port      = (unsigned short)atoi(argv[1]);
    if (argc >= 3) verbosity = atoi(argv[2]);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    UPnPMapping m = {0};
    if (upnp_request_mapping(port, "TCP", verbosity, &m) != 0) {
        fprintf(stderr, "UPnP port mapping failed\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    upnp_register_cleanup(&m);  /* releases mapping on Ctrl+C / SIGTERM */

    printf("Listening externally at %s:%u/TCP\n", m.ext_ip, m.ext_port);
    printf("Press Ctrl+C to release the mapping and exit.\n");

    /* In a real program you would start your server here.
     * For demonstration, just block until signal. */
#ifdef _WIN32
    Sleep(INFINITE);
#else
    pause();
#endif

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
