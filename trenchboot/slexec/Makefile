# Copyright (c) 2006-2010, Intel Corporation
# All rights reserved.

# -*- mode: Makefile; -*-

#
# slexec makefile
#

# TODO make slexec PIE

include $(CURDIR)/Config.mk

TARGET := $(CURDIR)/slexec

# boot.o must be first
obj-y := src/boot.o
obj-y += src/cmdline.o src/com.o src/e820.o
obj-y += src/linux.o src/loader.o
obj-y += src/misc.o src/pci.o src/printk.o
obj-y += src/string.o src/slexec.o
obj-y += src/sha1.o src/sha256.o
obj-y += src/tpm.o src/tpm_12.o src/tpm_20.o
obj-y += src/vga.o src/acpi.o
obj-y += src/skinit/skinit.o src/skinit/skl.o
obj-y += src/txt/heap.o src/txt/errors.o

OBJS := $(obj-y)


TARGET_LDS := $(CURDIR)/src/slexec.lds

$(TARGET).gz : $(TARGET)
	gzip -n -f -9 < $< > $@

$(TARGET) : $(OBJS) $(TARGET_LDS)
	$(LD) $(LDFLAGS) -T $(TARGET_LDS) -N $(OBJS) -o $(@D)/.$(@F).0
	$(NM) -n $(@D)/.$(@F).0 >$(TARGET)-syms
	$(LD) $(LDFLAGS) -T $(TARGET_LDS) $(LDFLAGS_STRIP) $(@D)/.$(@F).0 -o $(TARGET)
	rm -f $(@D)/.$(@F).0

#$(TARGET_LDS) : $(TARGET_LDS).x $(HDRS)
#	$(CPP) -P -E -Ui386 $(AFLAGS) -o $@ $<

#$(TARGET_LDS).x : FORCE

#.PHONY: FORCE
#FORCE :
#	@: # do nothing
#
# universal rules
#
dist : install


build : $(TARGET).gz


install : $(DISTDIR)/boot/$(TARGET).gz

$(DISTDIR)/boot/$(TARGET).gz : $(TARGET).gz
	[ -d $(DISTDIR)/boot ] || $(INSTALL_DIR) $(DISTDIR)/boot
	$(INSTALL_DATA) $(TARGET).gz $(DISTDIR)/boot/$(notdir $(TARGET)).gz
	$(INSTALL_DATA) $(TARGET)-syms $(DISTDIR)/boot/$(notdir $(TARGET))-syms
	[ -d $(DISTDIR)/etc/grub.d ] || $(INSTALL_DIR) $(DISTDIR)/etc/grub.d
	$(INSTALL) -m755 -t $(DISTDIR)/etc/grub.d 20*


clean :
	rm -f $(TARGET)* *~ include/*~ include/txt/*~ *.o src/*~ txt/*~ src/*.o txt/*.o
	rm -f tags TAGS cscope.files cscope.in.out cscope.out cscope.po.out


distclean : clean


#
#    TAGS / tags
#
define all_sources
    ( find . -name '*.[chS]' -print )
endef
define set_exuberant_flags
    exuberant_flags=`$1 --version 2>/dev/null | grep -iq exuberant && \
	echo "-I __initdata,__exitdata,__acquires,__releases \
	    -I EXPORT_SYMBOL \
	    --extra=+f --c-kinds=+px"`
endef

.PHONY: TAGS
TAGS :
	rm -f TAGS; \
	$(call set_exuberant_flags,etags); \
	$(all_sources) | xargs etags $$exuberant_flags -a

.PHONY: tags
tags :
	rm -f tags; \
	$(call set_exuberant_flags,ctags); \
	$(all_sources) | xargs ctags $$exuberant_flags -a

#
#    cscope
#
.PHONY: cscope
cscope :
	$(all_sources) > cscope.files
	cscope -k -b -q

#
#    MAP
#
.PHONY: MAP
MAP :
	$(NM) -n $(TARGET)-syms | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aUw] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)' > System.map

#
# implicit rules
#

HDRS := $(wildcard $(CURDIR)/include/*.h)
HDRS += $(wildcard $(CURDIR)/include/txt/*.h)

BUILD_DEPS := $(CURDIR)/Config.mk $(CURDIR)/Makefile

# fix case where gcc doesn't use builtin memcmp() when built w/o optimizations
ifeq ($(debug),y)
CFLAGS += -O2
endif

%.o : %.c $(HDRS) $(BUILD_DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o : %.S $(HDRS) $(BUILD_DEPS)
	$(CC) $(AFLAGS) -c $< -o $@

%.i : %.c $(HDRS) $(BUILD_DEPS)
	$(CPP) $(CFLAGS) $< -o $@

# -std=gnu{89,99} gets confused by # as an end-of-line comment marker
%.s : %.S $(HDRS)  $(BUILD_DEPS)
	$(CPP) $(AFLAGS) $< -o $@
