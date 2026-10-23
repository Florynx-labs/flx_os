#include "auth_client.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define AUTH_SOCKET "/run/flx-auth.sock"

int flx_auth_request(const char *request, char *response, size_t response_size) {
    if (!request || !response || response_size < 2) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", AUTH_SOCKET);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { close(fd); return -1; }
    size_t n = strlen(request);
    if (write(fd, request, n) != (ssize_t)n || write(fd, "\n", 1) != 1) { close(fd); return -1; }
    ssize_t got = read(fd, response, response_size - 1);
    close(fd);
    if (got <= 0) return -1;
    response[got] = 0;
    response[strcspn(response, "\r\n")] = 0;
    return 0;
}

int flx_auth_status(void) {
    char response[32];
    if (flx_auth_request("STATUS", response, sizeof response)) return -1;
    if (!strcmp(response, "EMPTY")) return 0;
    if (!strcmp(response, "ENROLLED")) return 1;
    return -1;
}
