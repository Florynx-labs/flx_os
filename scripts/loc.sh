#!/bin/sh
# Lines of code per component (own code only; third_party excluded). Used by proof mode.
R="$1"
count() { find "$@" -type f \( -name '*.c' -o -name '*.h' -o -name '*.py' -o -name '*.sh' -o -name 'copilot_bridge' -o -name Makefile \) 2>/dev/null | xargs cat 2>/dev/null | wc -l | tr -d ' '; }
T=0
for c in "init:src/init" "netd:src/netd" "compositor:src/comp" "libflxui:src/lib" "net stack:src/net" "web engine:src/web" "shell:src/sh" "apps:src/apps" "build tools:tools scripts docker build.sh run.sh" "tests:tests" "bridge:bridge"; do
  name=${c%%:*}; dirs=${c#*:}
  n=$(cd "$R" && count $dirs)
  T=$((T + n))
  echo "$name $n"
done
echo "total $T"
