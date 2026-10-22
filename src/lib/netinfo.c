// Reads /run/net.conf, the network state published by flx-netd.
#include "w12.h"

int netinfo_read(NetInfo *ni) {
    memset(ni, 0, sizeof *ni);
    char *s = read_file("/run/net.conf", 0);
    if (!s) return -1;
    for (char *l = strtok(s, "\n"); l; l = strtok(0, "\n")) {
        char *v = strchr(l, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(l, "up")) ni->up = atoi(v);
        else if (!strcmp(l, "ifname")) snprintf(ni->ifname, sizeof ni->ifname, "%s", v);
        else if (!strcmp(l, "ip")) snprintf(ni->ip, sizeof ni->ip, "%s", v);
        else if (!strcmp(l, "mask")) snprintf(ni->mask, sizeof ni->mask, "%s", v);
        else if (!strcmp(l, "gw")) snprintf(ni->gw, sizeof ni->gw, "%s", v);
        else if (!strcmp(l, "dns")) snprintf(ni->dns, sizeof ni->dns, "%s", v);
        else if (!strcmp(l, "mac")) snprintf(ni->mac, sizeof ni->mac, "%s", v);
        else if (!strcmp(l, "since")) ni->since = atof(v);
    }
    free(s);
    return 0;
}
