
# PS4 Auto Updater - scanner v0.3.7 + local HTTP bridge v0.7
# Default build produces TWO separate PKGs.
# Neither installs game updates.

TITLE       := PS4 Game Scanner Test
VERSION     := 01.04
TITLE_ID    := BREW00197
CONTENT_ID  := IV0000-BREW00197_00-PS4SCANNERTEST00

HTTP_TITLE      := PS4 Local Bridge Test
HTTP_VERSION    := 01.07
HTTP_TITLE_ID   := BREW00198
HTTP_CONTENT_ID := IV0000-BREW00198_00-PS4LOCALBRIDGE00
HTTP_PKG        := $(HTTP_CONTENT_ID).pkg

TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
TOOLBIN     := $(TOOLCHAIN)/bin/linux

INTDIR      := build
HTTP_DIR    := $(INTDIR)/local-http
HTTP_STAGE  := $(HTTP_DIR)/stage

RUNTIME_PRXS := sce_module/libc.prx sce_module/libSceFios2.prx

HTTP_RUNTIME_PRXS := \
    $(HTTP_STAGE)/sce_module/libc.prx \
    $(HTTP_STAGE)/sce_module/libSceFios2.prx

LIBS := -lc -lkernel -lc++ -lSceNet \
        -lSceSysmodule -lSceAppInstUtil

CFLAGS := -D__ORBIS__ \
          --target=x86_64-pc-freebsd12-elf \
          -fPIC -funwind-tables -c \
          -isysroot $(TOOLCHAIN) \
          -isystem $(TOOLCHAIN)/include

LDFLAGS := -m elf_x86_64 -pie \
           --script $(TOOLCHAIN)/link.x \
           --eh-frame-hdr \
           -L$(TOOLCHAIN)/lib \
           $(LIBS) \
           $(TOOLCHAIN)/lib/crt1.o

CC := clang
LD := ld.lld

.PHONY: all clean host-check http

# Builds both packages using the existing workflow.
all: $(CONTENT_ID).pkg $(HTTP_PKG)

# Optional isolated HTTP build.
http: $(HTTP_PKG)


# ==========================================
# ORIGINAL PS4 SCANNER
# ==========================================

$(INTDIR):
	mkdir -p $(INTDIR)

$(INTDIR)/main.o: src/ftp_probe.c | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

eboot.bin: $(INTDIR)/main.o
	$(LD) $(INTDIR)/main.o -o $(INTDIR)/scanner.elf $(LDFLAGS)
	$(TOOLBIN)/create-fself -in=$(INTDIR)/scanner.elf -out=$(INTDIR)/scanner.oelf --eboot eboot.bin --paid 0x3800000000000011

sce_sys/param.sfo: Makefile
	$(TOOLBIN)/PkgTool.Core sfo_new $@
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

sce_sys/about/right.sprx:
	mkdir -p sce_sys/about
	@f=$$(find '$(TOOLCHAIN)/samples' -name right.sprx -print -quit); \
	  test -n "$$f" || { echo 'right.sprx missing'; exit 1; }; \
	  cp "$$f" '$@'

sce_module/%.prx:
	@set -eu; \
	  mkdir -p sce_module; \
	  source_file=''; \
	  for p in "$(TOOLCHAIN)/src/modules/$*.prx" "$(TOOLCHAIN)/bin/data/modules/$*.prx"; do \
	    if [ -s "$$p" ]; then source_file="$$p"; break; fi; \
	  done; \
	  if [ -z "$$source_file" ]; then \
	    source_file="$$(find '$(TOOLCHAIN)' -type f -name '$*.prx' -print -quit)"; \
	  fi; \
	  if [ -z "$$source_file" ] || [ ! -s "$$source_file" ]; then \
	    echo 'ERROR: Missing runtime $*.prx' >&2; \
	    exit 1; \
	  fi; \
	  cp "$$source_file" "$@"

pkg.gp4: eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx $(RUNTIME_PRXS)
	$(TOOLBIN)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$^"

$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLBIN)/PkgTool.Core pkg_build $< .

host-check: | $(INTDIR)
	cc -std=c99 -Wall -Wextra -Werror -O2 -o $(INTDIR)/scanner-host src/main.c


# ==========================================
# NEW PS4 LOCAL HTTP BRIDGE v0.7
# Separate title ID and staging directory
# ==========================================

$(HTTP_DIR)/http.o: src/local_http_probe.c | $(INTDIR)
	mkdir -p $(HTTP_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(HTTP_DIR)/http.elf: $(HTTP_DIR)/http.o
	$(LD) $< -o $@ $(LDFLAGS)

$(HTTP_STAGE)/eboot.bin: $(HTTP_DIR)/http.elf
	mkdir -p $(HTTP_STAGE)
	$(TOOLBIN)/create-fself -in=$(HTTP_DIR)/http.elf -out=$(HTTP_DIR)/http.oelf --eboot $@ --paid 0x3800000000000011

$(HTTP_STAGE)/sce_sys/param.sfo: Makefile
	mkdir -p $(HTTP_STAGE)/sce_sys
	$(TOOLBIN)/PkgTool.Core sfo_new $@
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(HTTP_VERSION)'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(HTTP_CONTENT_ID)'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(HTTP_TITLE)'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(HTTP_TITLE_ID)'
	$(TOOLBIN)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(HTTP_VERSION)'

$(HTTP_STAGE)/sce_sys/icon0.png: sce_sys/icon0.png
	mkdir -p $(HTTP_STAGE)/sce_sys
	cp $< $@

$(HTTP_STAGE)/sce_sys/about/right.sprx: sce_sys/about/right.sprx
	mkdir -p $(HTTP_STAGE)/sce_sys/about
	cp $< $@

$(HTTP_STAGE)/sce_module/%.prx: sce_module/%.prx
	mkdir -p $(HTTP_STAGE)/sce_module
	cp $< $@

$(HTTP_STAGE)/pkg.gp4: $(HTTP_STAGE)/eboot.bin $(HTTP_STAGE)/sce_sys/param.sfo $(HTTP_STAGE)/sce_sys/icon0.png $(HTTP_STAGE)/sce_sys/about/right.sprx $(HTTP_RUNTIME_PRXS)
	cd $(HTTP_STAGE) && $(TOOLBIN)/create-gp4 -out pkg.gp4 --content-id=$(HTTP_CONTENT_ID) --files "eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx sce_module/libc.prx sce_module/libSceFios2.prx"

$(HTTP_PKG): $(HTTP_STAGE)/pkg.gp4
	cd $(HTTP_STAGE) && $(TOOLBIN)/PkgTool.Core pkg_build pkg.gp4 .
	cp $(HTTP_STAGE)/$(HTTP_PKG) $@


# ==========================================
# CLEAN
# ==========================================

clean:
	rm -rf $(INTDIR) eboot.bin pkg.gp4 sce_sys/param.sfo $(RUNTIME_PRXS) *.pkg
