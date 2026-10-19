// Windows 12 net library: DNS, HTTP/1.1 (+TLS via BearSSL), URLs, inflate. (API from Lumen OS 2, MIT.)
// All calls are blocking: run them off the UI thread.
#pragma once
#include "w12.h"

typedef struct { char scheme[8]; char host[256]; int port; char path[4096]; } Url;
int url_parse(const char *s, Url *u);
char *url_resolve(const char *base, const char *rel);
char *url_encode(const char *s);

int net_up(void);                                     // DHCP done (reads /run/net.conf)
int dns_resolve(const char *host, uint32_t *ip);
extern char dns_server[20];

typedef struct {
    int status;
    char *body; size_t len;
    char *content_type;
    char *url;
    char err[128];
    double ms;
} HttpResp;
HttpResp *http_request(const char *method, const char *url, const char *body, const char *ctype, size_t max_body);
HttpResp *http_get(const char *url);
void http_free(HttpResp *r);
typedef void (*http_progress_fn)(void *ctx, size_t got, size_t total);
void http_set_progress(http_progress_fn fn, void *ctx);
void http_set_ua(const char *ua);

typedef struct HttpStream HttpStream;
HttpStream *http_open(const char *url, int *status, char *err, int errn);
HttpStream *http_open_req(const char *method, const char *url, const char *body, const char *ctype, int *status, char *err, int errn);
int http_read(HttpStream *s, void *buf, int n);
void http_close(HttpStream *s);
void http_abort(HttpStream *s);

unsigned char *inflate_any(const unsigned char *in, size_t n, size_t *outn);
