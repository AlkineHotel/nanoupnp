/*
 * test_upnp.c — smoke test for c-upnp-igd
 *
 * Runs the full IGD sequence and exits with a clear pass/fail:
 *   - discovers the IGD (SSDP or gateway probe)
 *   - retrieves the external IP
 *   - adds a TCP port mapping
 *   - immediately deletes it
 *
 * Zero user interaction.  Exit code 0 = all steps passed.
 *
 * Build (Linux/macOS):
 *   gcc -o test/test_upnp test/test_upnp.c upnp.c -I. -lpthread
 *
 * Build (Windows, MinGW):
 *   gcc -o test/test_upnp.exe test/test_upnp.c upnp.c -I. -lws2_32 -liphlpapi
 */

#include "upnp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

#define TEST_PORT  19876   /* unlikely to conflict with anything */
#define VERBOSITY  2       /* set to 4 for full debug */

static int passed = 0;
static int failed = 0;

static void report(const char *name, int ok) {
    if (ok) { printf("  PASS  %s\n", name); passed++; }
    else     { printf("  FAIL  %s\n", name); failed++; }
}

int main(void) {
    printf("c-upnp-igd smoke test\n");
    printf("─────────────────────\n");

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("  FAIL  WSAStartup\n");
        return 1;
    }
#endif

    UPnPMapping m = {0};
    int ok = (upnp_request_mapping(TEST_PORT, "TCP", VERBOSITY, &m) == 0);
    report("upnp_request_mapping (discover + AddPortMapping)", ok);

    if (ok) {
        report("ext_ip populated",  m.ext_ip[0] != '\0');
        report("ext_port matches",  m.ext_port == TEST_PORT);
        report("valid flag set",    m.valid == 1);

        /* Immediately release — this is an automated test, don't leave mappings */
        upnp_release_mapping(&m);
        report("upnp_release_mapping (DeletePortMapping)", m.valid == 0);

        /* Double-release should be a no-op (no crash) */
        upnp_release_mapping(&m);
        report("double release is safe", 1);
    } else {
        printf("  SKIP  (remaining tests require successful mapping)\n");
    }

    printf("─────────────────────\n");
    printf("  %d passed, %d failed\n", passed, failed);

    if (!ok) {
        printf("\nNote: test requires a UPnP-enabled router on the LAN.\n");
        printf("If UPnP is disabled on your router, this test will always fail.\n");
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return (failed == 0) ? 0 : 1;
}
