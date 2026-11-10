
TITLE       := PS4 Auto Updater - Read Only Preview
VERSION     := 01.14
TITLE_ID    := BREW00197
CONTENT_ID  := IV0000-BREW00197_00-PS4SCANNERTEST00

TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
TOOLBIN     := $(TOOLCHAIN)/bin/linux
INTDIR      := build

RUNTIME_PRXS := sce_module/libc.prx sce_module/libSceFios2.prx

LIBS        := -lc -lkernel -lc++ -lSceNet -lSceSysmodule -lSceSystemService -lSceVideoOut -lScePad -lSceUserService

# Optional, UNVERIFIED FW14-only diagnostic; off for normal builds:
# add -DPS4_ENABLE_EXPERIMENTAL_WIDE_ART_PROBE to CFLAGS manually on an
# isolated hardware-test build. Never enable this for the release package.
CFLAGS      := -D__ORBIS__ -include stdbool.h -Wall -Wextra -Werror \
               --target=x86_64-pc-freebsd12-elf \
               -fPIC -funwind-tables -c \
               -isysroot $(TOOLCHAIN) \
               -isystem $(TOOLCHAIN)/include

LDFLAGS     := -m elf_x86_64 -pie \
               --script $(TOOLCHAIN)/link.x \
               --eh-frame-hdr \
               -L$(TOOLCHAIN)/lib \
               $(LIBS) $(TOOLCHAIN)/lib/crt1.o

CC          := clang
CXX         := clang++
CXXFLAGS    := -D__ORBIS__ -std=c++17 -Wall -Wextra -Werror \
               --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
               -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include \
               -I$(INTDIR)
LD          := ld.lld

.PHONY: all clean host-check

all: $(CONTENT_ID).pkg

$(INTDIR):
	mkdir -p $(INTDIR)

$(INTDIR)/main.o: src/ftp_scanner_v05.c | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ui_snapshot.o: src/ui_snapshot.c src/ui_snapshot.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ui_source_bridge.o: src/ui_source_bridge.c src/ui_source_bridge.h src/ui_models.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

# Offline, read-only compatibility reviewer. Linked but not activated
# until real metadata/provenance and FW14 hardware gates are verified.
$(INTDIR)/ui_compatibility.o: src/ui_compatibility.c src/ui_compatibility.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ui_models.o: src/ui_models.c src/ui_models.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

# Transport-free Stage 5 queue: built for ABI checks but not activated.
$(INTDIR)/download_queue.o: src/download_queue.c src/download_queue.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

# Metadata-only HTTP response policy. No native sockets or TLS activated.
$(INTDIR)/download_http_policy.o: src/download_http_policy.c src/download_http_policy.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

# Storage lifetime policy uses injected callbacks. No PS4 file writes.
$(INTDIR)/download_storage.o: src/download_storage.c src/download_storage.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

# Portable cryptographic checksum with no OS or FS APIs.
$(INTDIR)/download_sha256.o: src/download_sha256.c src/download_sha256.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

# Experimental backend is intentionally a nonfunctional stub on PS4.
# Host POSIX implementation is tested separately. Never enable filesystem
# writes with an unverified replacement of linkat/unlinkat/fsync.
$(INTDIR)/download_file_backend.o: src/download_file_backend.c src/download_file_backend.h src/download_storage.h src/download_sha256.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ui_navigation.o: src/ui_navigation.c src/ui_navigation.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ui_title_layout.o: src/ui_title_layout.c src/ui_title_layout.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ui_png_transport.o: src/ui_png_transport.c src/ui_png_transport.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ui_image.o: src/ui_image.c src/ui_image.h vendor/stb_image.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/ui_arabic_generated.h: tools/generate_ui_arabic.py | $(INTDIR)
	python3 tools/generate_ui_arabic.py --output $@

$(INTDIR)/ui_icon_cache.o: src/ui_icon_cache.c src/ui_icon_cache.h src/ui_image.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR)/scanner_ui.o: src/scanner_ui.cpp src/scanner_ui.h src/ui_navigation.h src/ui_title_layout.h src/ui_icon_cache.h $(INTDIR)/ui_arabic_generated.h | $(INTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(INTDIR)/ps4_shell_exit.o: src/ps4_shell_exit.c src/ps4_shell_exit.h | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

eboot.bin: $(INTDIR)/main.o $(INTDIR)/ui_snapshot.o $(INTDIR)/ui_navigation.o $(INTDIR)/ui_title_layout.o $(INTDIR)/ui_models.o $(INTDIR)/ui_source_bridge.o $(INTDIR)/ui_compatibility.o $(INTDIR)/download_queue.o $(INTDIR)/download_http_policy.o $(INTDIR)/download_storage.o $(INTDIR)/download_sha256.o $(INTDIR)/download_file_backend.o $(INTDIR)/ui_image.o $(INTDIR)/ui_png_transport.o $(INTDIR)/ui_icon_cache.o $(INTDIR)/scanner_ui.o $(INTDIR)/ps4_shell_exit.o
	$(LD) $(INTDIR)/main.o $(INTDIR)/ui_snapshot.o $(INTDIR)/ui_navigation.o $(INTDIR)/ui_title_layout.o $(INTDIR)/ui_models.o $(INTDIR)/ui_source_bridge.o $(INTDIR)/ui_compatibility.o $(INTDIR)/download_queue.o $(INTDIR)/download_http_policy.o $(INTDIR)/download_storage.o $(INTDIR)/download_sha256.o $(INTDIR)/download_file_backend.o $(INTDIR)/ui_image.o $(INTDIR)/ui_png_transport.o $(INTDIR)/ui_icon_cache.o $(INTDIR)/scanner_ui.o $(INTDIR)/ps4_shell_exit.o -o $(INTDIR)/scanner.elf $(LDFLAGS)
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
	  source_file=$$(find '$(TOOLCHAIN)' -type f -name '$*.prx' -print -quit); \
	  test -n "$$source_file" && test -s "$$source_file" || \
	    { echo 'ERROR: $*.prx not found'; exit 1; }; \
	  cp "$$source_file" "$@"

pkg.gp4: eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx $(RUNTIME_PRXS)
	$(TOOLBIN)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$^"

$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLBIN)/PkgTool.Core pkg_build $< .

host-check: | $(INTDIR)
	cc -std=c99 -Wall -Wextra -Werror -O2 -o $(INTDIR)/scanner-host src/ftp_scanner_v05.c

clean:
	rm -rf $(INTDIR) eboot.bin pkg.gp4 sce_sys/param.sfo $(RUNTIME_PRXS) *.pkg
