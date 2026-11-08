
# PS4 Auto Updater - FTP Diagnostic v0.3.3

TITLE       := PS4 Game Scanner Test
VERSION     := 01.03
TITLE_ID    := BREW00197
CONTENT_ID  := IV0000-BREW00197_00-PS4SCANNERTEST00

TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
TOOLBIN     := $(TOOLCHAIN)/bin/linux
INTDIR      := build

RUNTIME_PRXS := sce_module/libc.prx sce_module/libSceFios2.prx

LIBS        := -lc -lkernel -lc++ -lSceNet

CFLAGS      := -D__ORBIS__ --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
               -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include

LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
               -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

CC          := clang
LD          := ld.lld

.PHONY: all clean host-check

all: $(CONTENT_ID).pkg

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

clean:
	rm -rf $(INTDIR) eboot.bin pkg.gp4 sce_sys/param.sfo $(RUNTIME_PRXS) *.pkg
