# Copyright (c) 2006-2010, Intel Corporation
# All rights reserved.

# -*- mode: Makefile; -*-

# tboot needs too many customized compiler settings to use system CFLAGS,
# so if environment wants to set any compiler flags, it must use TBOOT_CFLAGS
CFLAGS		:= $(TBOOT_CFLAGS)

# debug build
debug ?= n

# cc-option: Check if compiler supports first option, else fall back to second.
# Usage: cflags-y += $(call cc-option,$(CC),-march=winchip-c6,-march=i586)
cc-option = $(shell if test -z "`$(1) $(2) -S -o /dev/null -xc \
              /dev/null 2>&1`"; then echo "$(2)"; else echo "$(3)"; fi ;)

CFLAGS_WARN       = -Wall -Wformat-security -Werror -Wstrict-prototypes \
	            -Wextra -Winit-self -Wswitch-default -Wunused-parameter \
	            -Wwrite-strings \
	            $(call cc-option,$(CC),-Wlogical-op,) \
	            -Wno-missing-field-initializers

AS         = as
LD         = ld
CC         = gcc
CPP        = cpp
AR         = ar
RANLIB     = ranlib
NM         = nm
STRIP      = strip
OBJCOPY    = objcopy
OBJDUMP    = objdump

CFLAGS += $(CFLAGS_WARN) -fno-strict-aliasing -std=gnu99
# due to bug in gcc v4.2,3,?
CFLAGS += $(call cc-option,$(CC),-Wno-array-bounds,)


ifeq ($(debug),y)
CFLAGS += -g -DDEBUG
else
CFLAGS += -O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
endif

#
# slboot-specific build settings
#
RELEASEVER  := "1.0.0"
RELEASETIME := "2019-01-15 15:00 +0800"
#ROOTDIR ?= $(CURDIR)

#include $(ROOTDIR)/Config.mk

# if target arch is 64b, then convert -m64 to -m32 (tboot is always 32b)
CFLAGS		+= -m32
CFLAGS		+= -march=i686
CFLAGS		+= -nostdinc
CFLAGS		+= -fno-builtin -fno-common -fno-strict-aliasing
CFLAGS		+= -fomit-frame-pointer
CFLAGS		+= -pipe
CFLAGS		+= -iwithprefix include
CFLAGS		+= -I$(CURDIR)/include
# ensure no floating-point variables
CFLAGS		+= -msoft-float
# Disable PIE/SSP if GCC supports them. They can break us.
CFLAGS		+= $(call cc-option,$(CC),-nopie,)
CFLAGS		+= $(call cc-option,$(CC),-fno-stack-protector,)
CFLAGS		+= $(call cc-option,$(CC),-fno-stack-protector-all,)
CFLAGS		+= $(call cc-option,$(CC),-fno-stack-check,)

# changeset variable for banner
CFLAGS		+= -DTBOOT_CHANGESET=\""$(shell ((hg parents --template "{isodate|isodate} {rev}:{node|short}" >/dev/null && hg parents --template "{isodate|isodate} {rev}:{node|short}") || echo "$(RELEASETIME) $(RELEASEVER)") 2>/dev/null)"\"


AFLAGS		+= -D__ASSEMBLY__

# Most CFLAGS are safe for assembly files:
#  -std=gnu{89,99} gets confused by #-prefixed end-of-line comments
AFLAGS		+= $(patsubst -std=gnu%,,$(CFLAGS))


# LDFLAGS are only passed directly to $(LD)
LDFLAGS		= -melf_i386
