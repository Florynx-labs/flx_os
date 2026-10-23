# FLX OS userland. Everything here is our own code; links statically against musl.
# Called by docker/build-inside.sh with O= (object dir) KINC= (kernel headers) GEN= BEAR=
O ?= /kbuild/obj
CC = gcc
CFLAGS = -O2 -g0 -std=gnu11 -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-format-truncation \
         -idirafter $(KINC) -I. -Ilib -I../third_party -I$(BEAR)/inc -pthread -MMD
LDFLAGS = -static -pthread
LIBC = $(patsubst %.c,$(O)/%.o,$(wildcard lib/*.c))
NETC = $(patsubst %.c,$(O)/%.o,$(wildcard net/*.c)) $(O)/ta.o
WEBC = $(patsubst %.c,$(O)/%.o,$(wildcard web/*.c))
COMPC = $(patsubst %.c,$(O)/%.o,$(wildcard comp/*.c))
APPS = $(patsubst apps/%.c,%,$(wildcard apps/*.c))
LIBS = $(O)/libweb.a $(O)/libnet.a $(O)/libw12.a $(BEAR)/build/libbearssl.a -lm

all: $(O)/bin/init $(O)/bin/flx-netd $(O)/bin/flx-compositor $(O)/bin/flxsh $(addprefix $(O)/bin/,$(APPS)) $(O)/tools/gen_wallpaper rust-apps

# Rust executables call the existing libflxui C ABI. Keep C implementations as
# buildable rollback targets; the staged /bin names are supplied by Rust.
rust-apps: $(O)/libw12.a
	cd rust && RUSTFLAGS="-C target-feature=+crt-static -L native=$(O) -L native=$(BEAR)/build -l static=w12 -l static=bearssl -l m -l pthread" cargo build --offline --release --workspace --target-dir $(O)/rust
	cp $(O)/rust/release/explorer $(O)/bin/explorer
	cp $(O)/rust/release/taskmgr $(O)/bin/taskmgr
	cp $(O)/rust/release/flx-authd $(O)/bin/flx-authd

$(O)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(O)/ta.o: $(GEN)/ta.c
	$(CC) -O2 -I$(BEAR)/inc -c $< -o $@
$(O)/libw12.a: $(LIBC)
	rm -f $@; ar rcs $@ $^
$(O)/libnet.a: $(NETC)
	rm -f $@; ar rcs $@ $^
$(O)/libweb.a: $(WEBC)
	rm -f $@; ar rcs $@ $^

$(O)/bin/init: $(O)/init/init.o
	@mkdir -p $(dir $@); $(CC) $(LDFLAGS) -o $@ $^
$(O)/bin/flx-netd: $(O)/netd/netd.o $(O)/libw12.a
	@mkdir -p $(dir $@); $(CC) $(LDFLAGS) -o $@ $^ -lm
$(O)/bin/flx-compositor: $(COMPC) $(LIBS:-lm=) 
	@mkdir -p $(dir $@); $(CC) $(LDFLAGS) -o $@ $(COMPC) $(LIBS)
$(O)/bin/flxsh: $(O)/sh/w12sh.o $(LIBS:-lm=)
	@mkdir -p $(dir $@); $(CC) $(LDFLAGS) -o $@ $< $(LIBS)
$(O)/bin/%: $(O)/apps/%.o $(LIBS:-lm=)
	@mkdir -p $(dir $@); $(CC) $(LDFLAGS) -o $@ $< $(LIBS)
$(O)/tools/gen_wallpaper: ../tools/gen_wallpaper.c
	@mkdir -p $(dir $@); $(CC) -O2 -o $@ $< -lm

-include $(shell find $(O) -name '*.d' 2>/dev/null)
.PHONY: all rust-apps
