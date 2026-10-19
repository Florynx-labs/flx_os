// w12-netd: brings the network up with our own DHCP client (raw AF_PACKET frames,
// hand-built Ethernet/IPv4/UDP/BOOTP) and rtnetlink, then publishes the result in
// /run/net.conf for the shell, Settings, Terminal and Task Manager.
// DHCP + rtnetlink code ported from Lumen OS 2 (MIT).
#include "w12.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static char ifname[IFNAMSIZ] = "eth0";
static int ifindex;
static unsigned char mac[6];
static uint32_t my_ip, my_mask, my_gw, my_dns;
static char dns_server[20];

// ---- rtnetlink -------------------------------------------------------------------------------
static int nl_talk(struct nlmsghdr *h) {
    int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (s < 0) return -1;
    struct sockaddr_nl k = { .nl_family = AF_NETLINK };
    h->nlmsg_flags |= NLM_F_REQUEST | NLM_F_ACK;
    h->nlmsg_seq = (uint32_t)time(0);
    int r = -1;
    if (sendto(s, h, h->nlmsg_len, 0, (struct sockaddr *)&k, sizeof k) >= 0) {
        char buf[4096];
        int n = recv(s, buf, sizeof buf, 0);
        struct nlmsghdr *a = (struct nlmsghdr *)buf;
        if (n > 0 && NLMSG_OK(a, (unsigned)n) && a->nlmsg_type == NLMSG_ERROR) {
            r = ((struct nlmsgerr *)NLMSG_DATA(a))->error;
            if (r == -EEXIST) r = 0;
        }
    }
    close(s);
    return r;
}
static void rta_add(struct nlmsghdr *h, int type, const void *d, int len) {
    struct rtattr *a = (struct rtattr *)((char *)h + NLMSG_ALIGN(h->nlmsg_len));
    a->rta_type = type;
    a->rta_len = RTA_LENGTH(len);
    memcpy(RTA_DATA(a), d, len);
    h->nlmsg_len = NLMSG_ALIGN(h->nlmsg_len) + RTA_ALIGN(a->rta_len);
}
static int link_set(int up) {
    struct { struct nlmsghdr h; struct ifinfomsg i; char pad[64]; } m = {0};
    m.h.nlmsg_len = NLMSG_LENGTH(sizeof m.i);
    m.h.nlmsg_type = RTM_NEWLINK;
    m.i.ifi_family = AF_UNSPEC;
    m.i.ifi_index = ifindex;
    m.i.ifi_change = IFF_UP;
    m.i.ifi_flags = up ? IFF_UP : 0;
    return nl_talk(&m.h);
}
static int addr_op(int type, uint32_t ip, int prefix) {
    struct { struct nlmsghdr h; struct ifaddrmsg a; char pad[128]; } m = {0};
    m.h.nlmsg_len = NLMSG_LENGTH(sizeof m.a);
    m.h.nlmsg_type = type;
    if (type == RTM_NEWADDR) m.h.nlmsg_flags = NLM_F_CREATE | NLM_F_REPLACE;
    m.a.ifa_family = AF_INET;
    m.a.ifa_prefixlen = prefix;
    m.a.ifa_scope = RT_SCOPE_UNIVERSE;
    m.a.ifa_index = ifindex;
    rta_add(&m.h, IFA_LOCAL, &ip, 4);
    rta_add(&m.h, IFA_ADDRESS, &ip, 4);
    return nl_talk(&m.h);
}
static int route_add(uint32_t gw) {
    struct { struct nlmsghdr h; struct rtmsg r; char pad[128]; } m = {0};
    m.h.nlmsg_len = NLMSG_LENGTH(sizeof m.r);
    m.h.nlmsg_type = RTM_NEWROUTE;
    m.h.nlmsg_flags = NLM_F_CREATE | NLM_F_REPLACE;
    m.r.rtm_family = AF_INET;
    m.r.rtm_table = RT_TABLE_MAIN;
    m.r.rtm_protocol = RTPROT_BOOT;
    m.r.rtm_scope = RT_SCOPE_UNIVERSE;
    m.r.rtm_type = RTN_UNICAST;
    rta_add(&m.h, RTA_GATEWAY, &gw, 4);
    rta_add(&m.h, RTA_OIF, &ifindex, 4);
    return nl_talk(&m.h);
}

static int find_iface(void) {
    DIR *d = opendir("/sys/class/net");
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.' && strcmp(e->d_name, "lo")) { snprintf(ifname, sizeof ifname, "%s", e->d_name); break; }
    closedir(d);
    ifindex = if_nametoindex(ifname);
    int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    struct ifreq ifr = {0};
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
    if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    close(s);
    return ifindex ? 0 : -1;
}

// ---- DHCP --------------------------------------------------------------------------------------
static uint16_t csum(const void *p, int n) {
    const uint8_t *b = p;
    uint32_t s = 0;
    for (int i = 0; i + 1 < n; i += 2) s += b[i] << 8 | b[i + 1];
    if (n & 1) s += b[n - 1] << 8;
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return htons(~s);
}

static int dhcp_send(int s, uint32_t xid, int type, uint32_t req_ip, uint32_t server) {
    unsigned char f[600] = {0};
    unsigned char *eth = f, *ip = f + 14, *udp = ip + 20, *bp = udp + 8;
    memset(eth, 0xff, 6); memcpy(eth + 6, mac, 6); eth[12] = 0x08; eth[13] = 0x00;
    // BOOTP
    bp[0] = 1; bp[1] = 1; bp[2] = 6;
    memcpy(bp + 4, &xid, 4);
    bp[10] = 0x80;                     // broadcast flag: we have no address yet
    memcpy(bp + 28, mac, 6);
    unsigned char *o = bp + 236;
    *o++ = 99; *o++ = 130; *o++ = 83; *o++ = 99;     // magic cookie
    *o++ = 53; *o++ = 1; *o++ = type;
    *o++ = 61; *o++ = 7; *o++ = 1; memcpy(o, mac, 6); o += 6;
    *o++ = 12; *o++ = 5; memcpy(o, "w12pc", 5); o += 5;
    if (req_ip) { *o++ = 50; *o++ = 4; memcpy(o, &req_ip, 4); o += 4; }
    if (server) { *o++ = 54; *o++ = 4; memcpy(o, &server, 4); o += 4; }
    *o++ = 55; *o++ = 4; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 51;
    *o++ = 255;
    int bl = o - bp;
    if (bl < 300) bl = 300;
    // UDP 68 -> 67 (checksum 0 = none, allowed for IPv4)
    udp[0] = 0; udp[1] = 68; udp[2] = 0; udp[3] = 67;
    udp[4] = (8 + bl) >> 8; udp[5] = (8 + bl) & 255;
    // IPv4 0.0.0.0 -> 255.255.255.255
    ip[0] = 0x45; ip[2] = (20 + 8 + bl) >> 8; ip[3] = (20 + 8 + bl) & 255;
    ip[8] = 64; ip[9] = 17;
    memset(ip + 16, 0xff, 4);
    uint16_t c = csum(ip, 20);
    memcpy(ip + 10, &c, 2);
    struct sockaddr_ll to = { .sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_IP), .sll_ifindex = ifindex, .sll_halen = 6 };
    memset(to.sll_addr, 0xff, 6);
    return sendto(s, f, 14 + 20 + 8 + bl, 0, (struct sockaddr *)&to, sizeof to) > 0 ? 0 : -1;
}

typedef struct { int type; uint32_t yiaddr, mask, router, dns, server, lease; } Offer;
static int dhcp_recv(int s, uint32_t xid, Offer *of, double deadline) {
    unsigned char f[1600];
    while (now() < deadline) {
        struct timeval tv = { 0, 200000 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        int n = recv(s, f, sizeof f, 0);
        if (n < 14 + 20 + 8 + 240) continue;
        unsigned char *ip = f + 14;
        if (f[12] != 0x08 || f[13] != 0 || ip[9] != 17) continue;
        int ihl = (ip[0] & 15) * 4;
        unsigned char *udp = ip + ihl, *bp = udp + 8;
        if (udp[3] != 68 || bp[0] != 2 || memcmp(bp + 4, &xid, 4)) continue;
        memset(of, 0, sizeof *of);
        memcpy(&of->yiaddr, bp + 16, 4);
        unsigned char *o = bp + 240, *end = f + n;
        while (o < end && *o != 255) {
            if (*o == 0) { o++; continue; }
            int t = o[0], l = o[1];
            unsigned char *v = o + 2;
            if (v + l > end) break;
            if (t == 53) of->type = v[0];
            else if (t == 1 && l >= 4) memcpy(&of->mask, v, 4);
            else if (t == 3 && l >= 4) memcpy(&of->router, v, 4);
            else if (t == 6 && l >= 4) memcpy(&of->dns, v, 4);
            else if (t == 54 && l >= 4) memcpy(&of->server, v, 4);
            else if (t == 51 && l >= 4) of->lease = ntohl(*(uint32_t *)v);
            o += 2 + l;
        }
        return 0;
    }
    return -1;
}

static int dhcp(void) {
    int s = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_IP));
    if (s < 0) { LOG("dhcp: AF_PACKET socket failed (%s)", strerror(errno)); return -1; }
    struct sockaddr_ll b = { .sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_IP), .sll_ifindex = ifindex };
    bind(s, (struct sockaddr *)&b, sizeof b);
    uint32_t xid;
    getentropy(&xid, 4);
    Offer of, ack;
    int r = -1;
    for (int tries = 0; tries < 4; tries++) {
        LOG("dhcp: DISCOVER (xid %08x)", xid);
        dhcp_send(s, xid, 1, 0, 0);
        if (dhcp_recv(s, xid, &of, now() + 2 + tries) || of.type != 2) continue;
        struct in_addr a = { of.yiaddr };
        LOG("dhcp: OFFER %s", inet_ntoa(a));
        dhcp_send(s, xid, 3, of.yiaddr, of.server);
        LOG("dhcp: REQUEST");
        if (dhcp_recv(s, xid, &ack, now() + 3) || ack.type != 5) { LOG("dhcp: no ACK"); continue; }
        my_ip = ack.yiaddr ? ack.yiaddr : of.yiaddr;
        my_mask = ack.mask ? ack.mask : of.mask;
        my_gw = ack.router ? ack.router : of.router;
        my_dns = ack.dns ? ack.dns : of.dns;
        r = 0;
        break;
    }
    close(s);
    if (r) return -1;
    int prefix = __builtin_popcount(my_mask);
    char ips[20], gws[20];
    struct in_addr a = { my_ip };
    snprintf(ips, sizeof ips, "%s", inet_ntoa(a));
    a.s_addr = my_gw;
    snprintf(gws, sizeof gws, "%s", inet_ntoa(a));
    a.s_addr = my_dns;
    snprintf(dns_server, sizeof dns_server, "%s", inet_ntoa(a));
    int e1 = addr_op(RTM_NEWADDR, my_ip, prefix), e2 = route_add(my_gw);
    LOG("dhcp: ACK %s/%d gw %s dns %s (addr %d, route %d)", ips, prefix, gws, dns_server, e1, e2);
    return e1 ? -1 : 0;
}


static void publish(int up) {
    char ips[20] = "", masks[20] = "", gws[20] = "";
    struct in_addr a;
    a.s_addr = my_ip; snprintf(ips, sizeof ips, "%s", inet_ntoa(a));
    a.s_addr = my_mask; snprintf(masks, sizeof masks, "%s", inet_ntoa(a));
    a.s_addr = my_gw; snprintf(gws, sizeof gws, "%s", inet_ntoa(a));
    char *s = strfmt("up=%d\nifname=%s\nip=%s\nmask=%s\ngw=%s\ndns=%s\nmac=%02x:%02x:%02x:%02x:%02x:%02x\nsince=%.1f\n",
                     up, ifname, up ? ips : "", up ? masks : "", up ? gws : "", up ? dns_server : "",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], now());
    write_file("/run/net.conf.tmp", s, strlen(s));
    rename("/run/net.conf.tmp", "/run/net.conf");
    free(s);
}

int main(void) {
    // loopback first (127.0.0.1 for local tools)
    int lo = if_nametoindex("lo");
    if (lo) { int keep = ifindex; ifindex = lo; link_set(1); ifindex = keep; }
    if (find_iface()) { LOG("netd: no network interface"); publish(0); for (;;) pause(); }
    LOG("netd: interface %s index %d", ifname, ifindex);
    publish(0);
    for (;;) {
        link_set(1);
        usleep(150000);
        double t0 = now();
        if (dhcp() == 0) {
            publish(1);
            LOG("netd: connected in %.0f ms", (now() - t0) * 1000);
            MARK("NET up %s", dns_server);
            // stay resident; re-check the link every few seconds
            for (;;) {
                sleep(5);
                int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
                struct ifreq ifr = {0};
                snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
                int up = ioctl(s, SIOCGIFFLAGS, &ifr) == 0 && (ifr.ifr_flags & IFF_RUNNING);
                close(s);
                if (!up) { LOG("netd: link lost"); publish(0); break; }
            }
        } else {
            LOG("netd: DHCP failed, retrying");
            publish(0);
            sleep(2);
        }
    }
}
