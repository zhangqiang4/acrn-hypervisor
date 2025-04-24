INSTALL ?= install

MAJOR_VERSION = 1
MINOR_VERSION = 0

LIBPATH ?= /usr/lib
BINPATH ?= /usr/bin

CAMERA_CONFIG_STATIC_LIB = libcamera_config.a
CAMERA_CONFIG = libcamera_config.so

# List of static libraries
SRC_STATIC_LIBS = libcamera_client.a libcamera_fusion.a libv4l2_camera.a libvcamhal.a

TARGET_SHARED_LIBS = $(patsubst %.a,%.so,$(SRC_STATIC_LIBS))

# Binary target
CAMERA_MANAGER_BIN = camera_manager.bin

# All targets
all_targets = $(CAMERA_CONFIG) $(CAMERA_MANAGER_BIN) $(TARGET_SHARED_LIBS)

LDFLAGS += -L. -lcamhal -lcamera_config


all: $(all_targets)


$(CAMERA_CONFIG): $(CAMERA_CONFIG_STATIC_LIB)
	$(CXX) $(CFLAGS) -shared -o $@.$(MAJOR_VERSION).$(MINOR_VERSION) -Wl,--whole-archive $^ -Wl,--no-whole-archive $(LDFLAGS) -Wl,--hash-style=gnu,-soname,$@.$(MAJOR_VERSION)
	ln -sf $@.$(MAJOR_VERSION).$(MINOR_VERSION) $@.$(MAJOR_VERSION)
	ln -sf $@.$(MAJOR_VERSION) $@

# Unified rule for building shared libraries
$(TARGET_SHARED_LIBS): %.so: %.a $(CAMERA_CONFIG)
	$(CXX) $(CFLAGS) -shared -o $@.$(MAJOR_VERSION).$(MINOR_VERSION) -Wl,--whole-archive $< -Wl,--no-whole-archive $(LDFLAGS) -Wl,--hash-style=gnu,-soname,$@.$(MAJOR_VERSION)

# Rule for building the binary
$(CAMERA_MANAGER_BIN): libcamera_manager.a $(CAMERA_CONFIG)
	$(CXX) $(CFLAGS) -o $@ -Wl,--whole-archive $< -Wl,--no-whole-archive $(LDFLAGS) -Wl,--hash-style=gnu

install: $(all_targets)
	@echo Install LDFLAGS is: $(LDFLAGS)
	@echo Install prefix is: $(prefix)
	$(INSTALL) -D -m 755 $(CAMERA_MANAGER_BIN) $(BINPATH)/$(CAMERA_MANAGER_BIN)
	$(INSTALL) -D -m 755 $(CAMERA_CONFIG).$(MAJOR_VERSION).$(MINOR_VERSION) $(LIBPATH)/$(CAMERA_CONFIG).$(MAJOR_VERSION).$(MINOR_VERSION)
	ln -sf $(CAMERA_CONFIG).$(MAJOR_VERSION).$(MINOR_VERSION) $(LIBPATH)/$(CAMERA_CONFIG).$(MAJOR_VERSION)
	ln -sf $(CAMERA_CONFIG).$(MAJOR_VERSION) $(LIBPATH)/$(CAMERA_CONFIG)
	for lib in $(TARGET_SHARED_LIBS); do \
		$(INSTALL) -D -m 755 $$lib.$(MAJOR_VERSION).$(MINOR_VERSION) $(LIBPATH)/$$lib.$(MAJOR_VERSION).$(MINOR_VERSION); \
		ln -sf $$lib.$(MAJOR_VERSION).$(MINOR_VERSION) $(LIBPATH)/$$lib.$(MAJOR_VERSION); \
		ln -sf $$lib.$(MAJOR_VERSION) $(LIBPATH)/$$lib; \
	done

clean:
	rm -f *.so *.bin *.so.*
.PHONY: all clean install
