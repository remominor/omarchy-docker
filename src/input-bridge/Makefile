CC ?= cc
CFLAGS ?= -O2 -pipe
CPPFLAGS += -D_GNU_SOURCE
WARNINGS := -Wall -Wextra -Wpedantic -Werror

BUILD_DIR ?= build
WLROOTS_PROTOCOL_COMMIT ?= 5adf325333602d5b1e7ccdeb122633bbc8040ace
VIRTUAL_KEYBOARD_PROTOCOL_SHA256 ?= 7ad7870003ecd592cae47dc19d277a609b7f18fd7b7be012623cf3225a7294f5
VIRTUAL_POINTER_PROTOCOL_SHA256 ?= 3ff6d540be0bc5228195bf072bde42117ea17945a5c2061add5d3cf97d6bb524
VIRTUAL_KEYBOARD_XML ?= $(BUILD_DIR)/protocol/virtual-keyboard-unstable-v1.xml
VIRTUAL_POINTER_XML ?= $(BUILD_DIR)/protocol/wlr-virtual-pointer-unstable-v1.xml
WLROOTS_PROTOCOL_URL := https://raw.githubusercontent.com/swaywm/wlroots/$(WLROOTS_PROTOCOL_COMMIT)/protocol

PROTOCOL_HEADERS := \
	$(BUILD_DIR)/virtual-keyboard-unstable-v1-client-protocol.h \
	$(BUILD_DIR)/wlr-virtual-pointer-unstable-v1-client-protocol.h
PROTOCOL_SOURCES := \
	$(BUILD_DIR)/virtual-keyboard-unstable-v1-protocol.c \
	$(BUILD_DIR)/wlr-virtual-pointer-unstable-v1-protocol.c

PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner
LDLIBS := $(shell $(PKG_CONFIG) --libs wayland-client xkbcommon)
INCLUDES := $(shell $(PKG_CONFIG) --cflags wayland-client xkbcommon) -I$(BUILD_DIR)

.PHONY: all clean install

all: $(BUILD_DIR)/omarchy-input-bridge

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/protocol/virtual-keyboard-unstable-v1.xml:
	mkdir -p $(@D)
	curl -fsSL $(WLROOTS_PROTOCOL_URL)/virtual-keyboard-unstable-v1.xml -o $@.tmp
	printf '%s  %s\n' $(VIRTUAL_KEYBOARD_PROTOCOL_SHA256) $@.tmp | sha256sum -c -
	mv $@.tmp $@

$(BUILD_DIR)/protocol/wlr-virtual-pointer-unstable-v1.xml:
	mkdir -p $(@D)
	curl -fsSL $(WLROOTS_PROTOCOL_URL)/wlr-virtual-pointer-unstable-v1.xml -o $@.tmp
	printf '%s  %s\n' $(VIRTUAL_POINTER_PROTOCOL_SHA256) $@.tmp | sha256sum -c -
	mv $@.tmp $@

$(BUILD_DIR)/virtual-keyboard-unstable-v1-client-protocol.h: $(VIRTUAL_KEYBOARD_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(BUILD_DIR)/virtual-keyboard-unstable-v1-protocol.c: $(VIRTUAL_KEYBOARD_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(BUILD_DIR)/wlr-virtual-pointer-unstable-v1-client-protocol.h: $(VIRTUAL_POINTER_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(BUILD_DIR)/wlr-virtual-pointer-unstable-v1-protocol.c: $(VIRTUAL_POINTER_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(BUILD_DIR)/omarchy-input-bridge: omarchy-input-bridge.c $(PROTOCOL_HEADERS) $(PROTOCOL_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(INCLUDES) \
		-o $@ omarchy-input-bridge.c $(PROTOCOL_SOURCES) $(LDLIBS)

install: all
	install -Dm0755 $(BUILD_DIR)/omarchy-input-bridge \
		$(DESTDIR)/usr/local/bin/omarchy-input-bridge

clean:
	rm -rf $(BUILD_DIR)
