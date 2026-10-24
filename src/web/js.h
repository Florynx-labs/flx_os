#pragma once
#include "web.h"

// Executes inline and same-page JavaScript using the isolated QuickJS runtime.
// The returned string contains document.write() output, or NULL when unused.
char *web_js_run(Doc *doc);
