// Own JSON parser (recursive descent) and string escaper. Used for weather data.
#include "w12.h"
#include <ctype.h>

typedef struct { const char *p; int depth; } P;
static void ws(P *p) { while (*p->p && isspace((unsigned char)*p->p)) p->p++; }
static Json *val(P *p);
static Json *jnew(int t) { Json *j = calloc(1, sizeof *j); j->type = t; return j; }

static char *str(P *p) {   // p->p at opening quote
    p->p++;
    size_t cap = 32, n = 0;
    char *o = malloc(cap);
    while (*p->p && *p->p != '"') {
        if (n + 8 > cap) o = realloc(o, cap *= 2);
        char c = *p->p++;
        if (c != '\\') { o[n++] = c; continue; }
        c = *p->p++;
        switch (c) {
        case 'n': o[n++] = '\n'; break;
        case 't': o[n++] = '\t'; break;
        case 'r': o[n++] = '\r'; break;
        case 'b': o[n++] = '\b'; break;
        case 'f': o[n++] = '\f'; break;
        case 'u': {
            unsigned cp = 0;
            for (int i = 0; i < 4 && isxdigit((unsigned char)*p->p); i++) cp = cp * 16 + (isdigit((unsigned char)*p->p) ? *p->p - '0' : (tolower(*p->p) - 'a' + 10)), p->p++;
            if (cp >= 0xD800 && cp < 0xDC00 && p->p[0] == '\\' && p->p[1] == 'u') {   // surrogate pair
                unsigned lo = 0;
                p->p += 2;
                for (int i = 0; i < 4 && isxdigit((unsigned char)*p->p); i++) lo = lo * 16 + (isdigit((unsigned char)*p->p) ? *p->p - '0' : (tolower(*p->p) - 'a' + 10)), p->p++;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            n += utf8_put(o + n, cp);
            break;
        }
        case 0: p->p--; break;
        default: o[n++] = c;
        }
    }
    if (*p->p == '"') p->p++;
    o[n] = 0;
    return o;
}

static Json *val(P *p) {
    if (++p->depth > 64) return 0;
    ws(p);
    Json *j = 0;
    char c = *p->p;
    if (c == '{' || c == '[') {
        int obj = c == '{';
        j = jnew(obj ? J_OBJ : J_ARR);
        p->p++;
        Json **tail = &j->kids;
        for (;;) {
            ws(p);
            if (*p->p == (obj ? '}' : ']')) { p->p++; break; }
            if (!*p->p) break;
            char *key = 0;
            if (obj) {
                if (*p->p != '"') break;
                key = str(p);
                ws(p);
                if (*p->p == ':') p->p++;
            }
            Json *k = val(p);
            if (!k) { free(key); break; }
            k->key = key;
            *tail = k; tail = &k->next;
            ws(p);
            if (*p->p == ',') p->p++;
        }
    } else if (c == '"') { j = jnew(J_STR); j->str = str(p); }
    else if (c == 't' && !strncmp(p->p, "true", 4)) { j = jnew(J_BOOL); j->num = 1; p->p += 4; }
    else if (c == 'f' && !strncmp(p->p, "false", 5)) { j = jnew(J_BOOL); p->p += 5; }
    else if (c == 'n' && !strncmp(p->p, "null", 4)) { j = jnew(J_NULL); p->p += 4; }
    else if (c == '-' || isdigit((unsigned char)c)) { char *e; j = jnew(J_NUM); j->num = strtod(p->p, &e); p->p = e; }
    p->depth--;
    return j;
}

Json *json_parse(const char *s) { P p = { s, 0 }; return s ? val(&p) : 0; }
void json_free(Json *j) {
    while (j) { Json *n = j->next; json_free(j->kids); free(j->str); free(j->key); free(j); j = n; }
}
Json *json_get(Json *o, const char *key) {
    if (!o || o->type != J_OBJ) return 0;
    for (Json *k = o->kids; k; k = k->next) if (k->key && !strcmp(k->key, key)) return k;
    return 0;
}
Json *json_at(Json *a, int i) {
    if (!a || a->type != J_ARR) return 0;
    for (Json *k = a->kids; k; k = k->next) if (i-- == 0) return k;
    return 0;
}
double json_num(Json *j, double def) { return j && (j->type == J_NUM || j->type == J_BOOL) ? j->num : def; }
const char *json_str(Json *j, const char *def) { return j && j->type == J_STR ? j->str : def; }
char *json_escape(const char *s) {
    size_t n = strlen(s);
    char *o = malloc(n * 6 + 3), *q = o;
    *q++ = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { *q++ = '\\'; *q++ = *p; }
        else if (*p == '\n') { *q++ = '\\'; *q++ = 'n'; }
        else if (*p == '\r') { *q++ = '\\'; *q++ = 'r'; }
        else if (*p == '\t') { *q++ = '\\'; *q++ = 't'; }
        else if (*p < 32) q += sprintf(q, "\\u%04x", *p);
        else *q++ = *p;
    }
    *q++ = '"'; *q = 0;
    return o;
}

#ifdef JSON_SELFTEST
int main(void) {
    Json *j = json_parse("{\"a\": [1, 2.5, -3e2], \"s\": \"h\\u00e9\\n\\\"x\\\"\", \"o\": {\"t\": true, \"n\": null}}");
    if (json_num(json_at(json_get(j, "a"), 2), 0) != -300) return 1;
    if (strcmp(json_str(json_get(j, "s"), ""), "h\xc3\xa9\n\"x\"")) return 2;
    if (json_num(json_get(json_get(j, "o"), "t"), 0) != 1) return 3;
    char *e = json_escape("a\"b\n");
    if (strcmp(e, "\"a\\\"b\\n\"")) return 4;
    puts("json ok");
    return 0;
}
#endif
