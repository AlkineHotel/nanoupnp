#ifndef UPNP_H
#define UPNP_H

/*
 * upnp.h — UPnP IGD port mapping, zero external dependencies.
 *
 * Protocol summary:
 *  1. SSDP M-SEARCH  UDP multicast → router replies with description URL
 *  2. HTTP GET       fetch device description XML → find WANIPConnection controlURL
 *  3. SOAP POST      GetExternalIPAddress  → WAN IP
 *  4. SOAP POST      AddPortMapping        → open external port
 *  5. SOAP POST      DeletePortMapping     → close on Ctrl+C
 *
 * Usage:
 *   UPnPMapping m = {0};
 *   if (upnp_request_mapping(9999, "TCP", verbosity, &m) == 0) {
 *       upnp_register_cleanup(&m);   // installs SIGINT/SIGTERM handler
 *       // listen...
 *   }
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum lengths for internal strings */
#define UPNP_URL_MAX    512
#define UPNP_HOST_MAX   128
#define UPNP_IP_MAX      48

typedef struct {
    char control_url[UPNP_URL_MAX];
    char service_type[128];
    char igd_host[UPNP_HOST_MAX];  /* host:port of the IGD HTTP server */
} IGDService;

typedef struct {
    IGDService svc;
    char       proto[8];     /* "TCP" or "UDP" */
    char       ext_ip[UPNP_IP_MAX];
    unsigned short ext_port;
    int        valid;        /* 0 = already released or never set */
} UPnPMapping;

/*
 * Discover IGD, get external IP, open port mapping.
 * Returns 0 on success; fills *m.  verbosity: 0=silent, 2=info, 4=debug.
 */
int  upnp_request_mapping(unsigned short port, const char *proto,
                          int verbosity, UPnPMapping *m);

/*
 * Send DeletePortMapping and invalidate *m.
 * Safe to call multiple times.
 */
void upnp_release_mapping(UPnPMapping *m);

/*
 * Install a SIGINT/SIGTERM handler that calls upnp_release_mapping before exit.
 * Must be called after upnp_request_mapping succeeds.
 */
void upnp_register_cleanup(UPnPMapping *m);

#ifdef __cplusplus
}
#endif

#endif /* UPNP_H */
