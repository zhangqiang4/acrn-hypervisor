include ../../../paths.make

T := $(CURDIR)
OUT_DIR ?= $(shell mkdir -p $(T)/build;cd $(T)/build;pwd)
CC ?= gcc

CFLAGS := -g -O0 -std=gnu11
CFLAGS += -D_GNU_SOURCE
CFLAGS += -DNO_OPENSSL
CFLAGS += -m64
CFLAGS += -Wall -ffunction-sections
CFLAGS += -Werror
CFLAGS += -O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
CFLAGS += -Wformat -Wformat-security -fno-strict-aliasing
CFLAGS += -fpie -fpic
CFLAGS += -lnuma
CFLAGS += $(CFLAGS)

GCC_MAJOR=$(shell echo __GNUC__ | $(CC) -E -x c - | tail -n 1)
GCC_MINOR=$(shell echo __GNUC_MINOR__ | $(CC) -E -x c - | tail -n 1)

#enable stack overflow check
STACK_PROTECTOR := 1

ifdef STACK_PROTECTOR
ifeq (true, $(shell [ $(GCC_MAJOR) -gt 4 ] && echo true))
CFLAGS += -fstack-protector-strong
else
ifeq (true, $(shell [ $(GCC_MAJOR) -eq 4 ] && [ $(GCC_MINOR) -ge 9 ] && echo true))
CFLAGS += -fstack-protector-strong
else
CFLAGS += -fstack-protector
endif
endif
endif

ITS_LDFLAGS := -Wl,-z,noexecstack
ITS_LDFLAGS += -Wl,-z,relro,-z,now
ITS_LDFLAGS += -pie
ITS_LDFLAGS += $(LDFLAGS)

all:
	$(CC) -o $(OUT_DIR)/ivshmem-timesync ivshmem-timesync.c $(CFLAGS) $(ITS_LDFLAGS)
clean:
	rm -f $(OUT_DIR)/ivshmem-timesync
ifneq ($(OUT_DIR),.)
	rm -rf $(OUT_DIR)
endif

install: $(OUT_DIR)/ivshmem-timesync
	install -d $(DESTDIR)$(bindir)
	install -t $(DESTDIR)$(bindir) $(OUT_DIR)/ivshmem-timesync
