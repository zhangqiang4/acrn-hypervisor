#
# ACRN Hypervisor Static Library for Debug Features
#

SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,$(HV_OBJDIR)/bsp/%.o,$(SRCS))

.PHONY: default
default: lib

lib: $(OBJS)
	$(AR) $(ARFLAGS) $(BSP_MOD) $(OBJS)

.PHONY: clean
clean:
	rm -f $(OBJS)
	rm -f $(OBJS:.o=.d)

-include $(OBJS:.o=.d)

$(HV_OBJDIR)/bsp/%.o: %.c
	[ ! -e $@ ] && mkdir -p $(dir $@) && mkdir -p $(HV_MODDIR); \
	$(CC) $(patsubst %, -I%, $(INCLUDE_PATH)) -I. -c $(CFLAGS) $(ARCH_CFLAGS) $< -o $@ -MMD -MT $@
