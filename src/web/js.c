// Sandboxed ECMAScript support for FLX's native HTML renderer.
// QuickJS is MIT licensed; no filesystem, module loader, process or native I/O
// globals are installed in page contexts.
#include "web.h"
#include "js.h"
#include "../../third_party/quickjs/quickjs.h"
#include <ctype.h>
#include <stdarg.h>

#define JS_OUT_MAX (128 * 1024)
#define JS_SCRIPT_MAX (512 * 1024)
#define JS_TOTAL_MAX (2 * 1024 * 1024)
#define JS_SCRIPT_COUNT 32
#define JS_PAGE_BUDGET_MS 1500

typedef struct {
    Doc *doc;
    char *out;
    size_t len, cap, bytes;
    double deadline;
} Page;
static JSClassID node_class_id;
static Node *find_id(Node *n, const char *id) {
    if (!n || !id) return NULL;
    if (n->type == N_ELEM) { const char *v = attr(n, "id"); if (v && !strcmp(v, id)) return n; }
    for (Node *c = n->first; c; c = c->next) { Node *found = find_id(c, id); if (found) return found; }
    return NULL;
}
static Node *find_selector(Node *n, const char *sel) {
    if (!n || !sel || !*sel) return NULL;
    if (n->type == N_ELEM) {
        int match = 0;
        if (*sel == '#') { const char *id = attr(n, "id"); match = id && !strcmp(id, sel + 1); }
        else if (*sel == '.') match = has_class(n, sel + 1);
        else match = !strcmp(n->tag, sel);
        if (match) return n;
    }
    for (Node *c = n->first; c; c = c->next) { Node *found = find_selector(c, sel); if (found) return found; }
    return NULL;
}
static void set_attr(Page *p, Node *n, const char *name, const char *value) {
    if (!p || !p->doc || !n || !name || !*name) return;
    for (int i = 0; i < n->nattr; i++) if (!strcmp(n->attr[i*2], name)) { n->attr[i*2+1] = ar_strndup(&p->doc->ar, value, strlen(value)); return; }
    char **a = ar_alloc(&p->doc->ar, sizeof(char*) * (n->nattr + 1) * 2);
    if (n->attr) memcpy(a, n->attr, sizeof(char*) * n->nattr * 2);
    a[n->nattr*2] = ar_strndup(&p->doc->ar, name, strlen(name));
    a[n->nattr*2+1] = ar_strndup(&p->doc->ar, value, strlen(value));
    n->attr = a; n->nattr++;
}
static JSValue node_text(JSContext *ctx, JSValueConst thisv) {
    Node *n = JS_GetOpaque2(ctx, thisv, node_class_id); if (!n) return JS_EXCEPTION;
    char text[4096]; text_content(n, text, sizeof text); return JS_NewString(ctx, text);
}
static JSValue node_set_text(JSContext *ctx, JSValueConst thisv, JSValueConst value) {
    Node *n = JS_GetOpaque2(ctx, thisv, node_class_id); if (!n) return JS_EXCEPTION;
    Page *p = JS_GetContextOpaque(ctx); const char *s = JS_ToCString(ctx, value);
    if (!s || !p || !p->doc) { if (s) JS_FreeCString(ctx, s); return JS_EXCEPTION; }
    n->first = n->last = NULL;
    size_t len = strlen(s);
    if (len) {
        Node *t = ar_alloc(&p->doc->ar, sizeof *t); t->type = N_TEXT;
        t->text = ar_strndup(&p->doc->ar, s, len); t->tlen = (int)len;
        t->parent = n; n->first = n->last = t;
    }
    JS_FreeCString(ctx, s); return JS_UNDEFINED;
}
static JSValue node_get_id(JSContext *ctx, JSValueConst thisv) {
    Node *n = JS_GetOpaque2(ctx, thisv, node_class_id); if (!n) return JS_EXCEPTION;
    const char *id = attr(n, "id"); return JS_NewString(ctx, id ? id : "");
}
static JSValue node_set_id(JSContext *ctx, JSValueConst thisv, JSValueConst value) {
    Node *n = JS_GetOpaque2(ctx, thisv, node_class_id); Page *p = JS_GetContextOpaque(ctx);
    const char *s = JS_ToCString(ctx, value); if (!n || !s) { if (s) JS_FreeCString(ctx, s); return JS_EXCEPTION; }
    set_attr(p, n, "id", s); JS_FreeCString(ctx, s); return JS_UNDEFINED;
}
static JSValue node_get_attribute(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    Node *n = JS_GetOpaque2(ctx, thisv, node_class_id); if (!n || !argc) return JS_NULL;
    const char *name = JS_ToCString(ctx, argv[0]); if (!name) return JS_EXCEPTION;
    const char *value = attr(n, name); JSValue ret = value ? JS_NewString(ctx, value) : JS_NULL; JS_FreeCString(ctx, name); return ret;
}
static JSValue node_add_event(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    (void)ctx; (void)thisv; (void)argc; (void)argv; return JS_UNDEFINED;
}
static JSValue node_set_attribute(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    Node *n = JS_GetOpaque2(ctx, thisv, node_class_id); Page *p = JS_GetContextOpaque(ctx);
    if (!n || argc < 2) return JS_EXCEPTION;
    const char *a = JS_ToCString(ctx, argv[0]), *v = JS_ToCString(ctx, argv[1]);
    if (a && v) set_attr(p, n, a, v);
    if (a) JS_FreeCString(ctx, a); if (v) JS_FreeCString(ctx, v);
    return JS_UNDEFINED;
}
static JSValue node_append(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    Node *parent = JS_GetOpaque2(ctx, thisv, node_class_id);
    Node *child = argc ? JS_GetOpaque2(ctx, argv[0], node_class_id) : NULL;
    if (!parent || !child || parent == child) return JS_EXCEPTION;
    for (Node *p = parent; p; p = p->parent) if (p == child) return JS_ThrowTypeError(ctx, "DOM cycle");
    if (child->parent) {
        Node *old = child->parent;
        Node **link = &old->first;
        while (*link && *link != child) link = &(*link)->next;
        if (*link) *link = child->next;
        if (old->last == child) { old->last = NULL; for (Node *q = old->first; q; q = q->next) old->last = q; }
    }
    child->parent = parent; child->next = NULL;
    if (parent->last) parent->last->next = child; else parent->first = child;
    parent->last = child;
    return JS_DupValue(ctx, argv[0]);
}
static JSValue make_node(JSContext *ctx, Node *n) {
    if (!n) return JS_NULL;
    JSValue obj = JS_NewObjectClass(ctx, node_class_id); JS_SetOpaque(obj, n); return obj;
}
static JSValue doc_get_element(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    (void)thisv; if (!argc) return JS_NULL;
    Page *p = JS_GetContextOpaque(ctx); const char *id = JS_ToCString(ctx, argv[0]);
    Node *n = p && p->doc && id ? find_id(p->doc->root, id) : NULL;
    JSValue result = make_node(ctx, n); if (id) JS_FreeCString(ctx, id); return result;
}
static JSValue doc_query(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    (void)thisv; if (!argc) return JS_NULL;
    Page *p = JS_GetContextOpaque(ctx); const char *sel = JS_ToCString(ctx, argv[0]);
    Node *n = p && p->doc && sel ? find_selector(p->doc->root, sel) : NULL;
    JSValue result = make_node(ctx, n); if (sel) JS_FreeCString(ctx, sel); return result;
}
static JSValue doc_create_element(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    (void)thisv; if (!argc) return JS_EXCEPTION;
    Page *p = JS_GetContextOpaque(ctx); const char *tag = JS_ToCString(ctx, argv[0]);
    if (!p || !p->doc || !tag || strlen(tag) > 64) { if (tag) JS_FreeCString(ctx, tag); return JS_EXCEPTION; }
    Node *n = ar_alloc(&p->doc->ar, sizeof *n); n->type = N_ELEM;
    char lower[65]; snprintf(lower, sizeof lower, "%s", tag); for (char *q = lower; *q; q++) *q = (char)tolower((unsigned char)*q);
    n->tag = ar_strndup(&p->doc->ar, lower, strlen(lower)); JS_FreeCString(ctx, tag);
    return make_node(ctx, n);
}

static int interrupted(JSRuntime *rt, void *opaque) {
    (void)rt;
    Page *p = opaque;
    return now() > p->deadline;
}
static void append(Page *p, const char *s, size_t n) {
    if (p->len >= JS_OUT_MAX || n > JS_OUT_MAX - p->len) return;
    if (p->len + n + 1 > p->cap) {
        size_t cap = p->cap ? p->cap : 256;
        while (cap < p->len + n + 1) cap *= 2;
        if (cap > JS_OUT_MAX + 1) cap = JS_OUT_MAX + 1;
        char *q = realloc(p->out, cap);
        if (!q) return;
        p->out = q; p->cap = cap;
    }
    memcpy(p->out + p->len, s, n);
    p->len += n; p->out[p->len] = 0;
}
static JSValue js_write(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    (void)thisv;
    Page *p = JS_GetContextOpaque(ctx);
    for (int i = 0; p && i < argc; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (!s) continue;
        append(p, s, strlen(s));
        JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}
static JSValue js_log(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    (void)thisv;
    char line[512] = "";
    size_t used = 0;
    for (int i = 0; i < argc && used < sizeof line - 2; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (!s) continue;
        int n = snprintf(line + used, sizeof line - used, "%s%s", i ? " " : "", s);
        if (n > 0) used += (size_t)n < sizeof line - used ? (size_t)n : sizeof line - used - 1;
        JS_FreeCString(ctx, s);
    }
    LOG("browser js: %s", line);
    return JS_UNDEFINED;
}
static JSValue js_timeout(JSContext *ctx, JSValueConst thisv, int argc, JSValueConst *argv) {
    (void)thisv;
    if (!argc || !JS_IsFunction(ctx, argv[0])) return JS_UNDEFINED;
    // Timers are run once, after the current script, without spawning threads.
    JSValue r = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
    if (JS_IsException(r)) return r;
    JS_FreeValue(ctx, r);
    return JS_NewInt32(ctx, 1);
}
static void run_script(JSContext *ctx, Page *p, const char *src, size_t len, const char *name) {
    if (!src || !len || p->bytes >= JS_TOTAL_MAX || len > JS_SCRIPT_MAX || p->deadline <= now()) return;
    p->bytes += len;
    JSValue v = JS_Eval(ctx, src, len, name, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JSValue e = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, e);
        LOG("browser js error (%s): %s", name, msg ? msg : "script error");
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, e);
    }
    JS_FreeValue(ctx, v);
    while (JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx) > 0) {}
}
static void walk_scripts(Node *n, JSContext *ctx, Page *p, int *count) {
    for (Node *c = n ? n->first : NULL; c && *count < JS_SCRIPT_COUNT && p->bytes < JS_TOTAL_MAX; c = c->next) {
        if (c->type != N_ELEM) continue;
        if (!strcmp(c->tag, "script")) {
            (*count)++;
            const char *type = attr(c, "type");
            if (type && *type && strcasecmp(type, "text/javascript") && strcasecmp(type, "application/javascript") && strcasecmp(type, "module")) continue;
            if (attr(c, "src")) {
                char *url = url_resolve(p->doc->base, attr(c, "src"));
                HttpResp *r = http_request("GET", url, NULL, NULL, JS_SCRIPT_MAX);
                if (r->status == 200 && r->body)
                    run_script(ctx, p, (const char *)r->body, r->len, url);
                else LOG("browser js: script fetch failed (%s)", url);
                http_free(r); free(url);
                continue;
            }
            if (c->first && c->first->type == N_TEXT)
                run_script(ctx, p, c->first->text, c->first->tlen, "inline-script");
        } else walk_scripts(c, ctx, p, count);
    }
}
char *web_js_run(Doc *doc) {
    if (!doc || !doc->root) return NULL;
    Page p = { .doc = doc, .deadline = now() + JS_PAGE_BUDGET_MS / 1000.0 };
    JSRuntime *rt = JS_NewRuntime();
    if (!rt) return NULL;
    JS_SetMemoryLimit(rt, 24 * 1024 * 1024);
    JS_SetMaxStackSize(rt, 128 * 1024);
    JS_SetInterruptHandler(rt, interrupted, &p);
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return NULL; }
    JS_SetContextOpaque(ctx, &p);

    JS_NewClassID(&node_class_id);
    JSClassDef node_def = { .class_name = "FLXNode" };
    JS_NewClass(rt, node_class_id, &node_def);
    JSValue node_proto = JS_NewObject(ctx);
    static const JSCFunctionListEntry node_props[] = {
        JS_CGETSET_DEF("textContent", node_text, node_set_text),
        JS_CGETSET_DEF("innerText", node_text, node_set_text),
        JS_CGETSET_DEF("id", node_get_id, node_set_id),
        JS_CFUNC_DEF("getAttribute", 1, node_get_attribute),
        JS_CFUNC_DEF("setAttribute", 2, node_set_attribute),
        JS_CFUNC_DEF("addEventListener", 2, node_add_event),
        JS_CFUNC_DEF("appendChild", 1, node_append),
    };
    JS_SetPropertyFunctionList(ctx, node_proto, node_props, sizeof node_props / sizeof node_props[0]);
    JS_SetClassProto(ctx, node_class_id, node_proto);

    JSValue docobj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, docobj, "write", JS_NewCFunction(ctx, js_write, "write", 1));
    JS_SetPropertyStr(ctx, docobj, "writeln", JS_NewCFunction(ctx, js_write, "writeln", 1));
    JS_SetPropertyStr(ctx, docobj, "getElementById", JS_NewCFunction(ctx, doc_get_element, "getElementById", 1));
    JS_SetPropertyStr(ctx, docobj, "querySelector", JS_NewCFunction(ctx, doc_query, "querySelector", 1));
    JS_SetPropertyStr(ctx, docobj, "createElement", JS_NewCFunction(ctx, doc_create_element, "createElement", 1));
    JS_SetPropertyStr(ctx, docobj, "body", make_node(ctx, doc->body));
    JS_SetPropertyStr(ctx, docobj, "URL", JS_NewString(ctx, doc->base ? doc->base : ""));
    JS_SetPropertyStr(ctx, docobj, "readyState", JS_NewString(ctx, "complete"));
    JSValue win = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, win, "document", JS_DupValue(ctx, docobj));
    JS_SetPropertyStr(ctx, win, "window", JS_DupValue(ctx, win));
    JS_SetPropertyStr(ctx, win, "self", JS_DupValue(ctx, win));
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_log, "log", 1));
    JS_SetPropertyStr(ctx, console, "warn", JS_NewCFunction(ctx, js_log, "warn", 1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, js_log, "error", 1));
    JS_SetPropertyStr(ctx, win, "console", JS_DupValue(ctx, console));
    JS_SetPropertyStr(ctx, win, "setTimeout", JS_NewCFunction(ctx, js_timeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, win, "setInterval", JS_NewCFunction(ctx, js_timeout, "setInterval", 2));
    JS_SetPropertyStr(ctx, win, "clearTimeout", JS_NewCFunction(ctx, js_timeout, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, win, "clearInterval", JS_NewCFunction(ctx, js_timeout, "clearInterval", 1));
    JSValue glob = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, glob, "window", JS_DupValue(ctx, win));
    JS_SetPropertyStr(ctx, glob, "self", JS_DupValue(ctx, win));
    JS_SetPropertyStr(ctx, glob, "document", JS_DupValue(ctx, docobj));
    JS_SetPropertyStr(ctx, glob, "console", console);
    JS_SetPropertyStr(ctx, glob, "setTimeout", JS_NewCFunction(ctx, js_timeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, glob, "setInterval", JS_NewCFunction(ctx, js_timeout, "setInterval", 2));
    JS_FreeValue(ctx, glob); JS_FreeValue(ctx, win); JS_FreeValue(ctx, docobj);
    int count = 0;
    walk_scripts(doc->root, ctx, &p, &count);
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    return p.out;
}
