CC ?= gcc
SYSROOT ?=

TARGET_DIR := build
DIST_BIN := dist/bulbagotchi/bin/bulbagotchi
TARGET := $(TARGET_DIR)/bulbagotchi

COMMON_FLAGS := -O2 -Wall -Wextra

ifeq ($(SYSROOT),)
	GTK_CFLAGS := $(shell pkg-config --cflags gtk+-2.0)
	GTK_LIBS   := $(shell pkg-config --libs gtk+-2.0)
	ARCH_FLAGS :=
else
	ARCH_FLAGS := --sysroot=$(SYSROOT)
	GTK_CFLAGS := -I$(SYSROOT)/usr/include/gtk-2.0 \
	              -I$(SYSROOT)/usr/lib/arm-linux-gnueabihf/gtk-2.0/include \
	              -I$(SYSROOT)/usr/include/pango-1.0 \
	              -I$(SYSROOT)/usr/include/glib-2.0 \
	              -I$(SYSROOT)/usr/lib/arm-linux-gnueabihf/glib-2.0/include \
	              -I$(SYSROOT)/usr/include/cairo \
	              -I$(SYSROOT)/usr/include/gdk-pixbuf-2.0 \
	              -I$(SYSROOT)/usr/include/atk-1.0 \
	              -I$(SYSROOT)/usr/include/harfbuzz \
	              -I$(SYSROOT)/usr/include/freetype2 \
	              -I$(SYSROOT)/usr/include/libpng16
	GTK_LIBS   := -L$(SYSROOT)/usr/lib/arm-linux-gnueabihf \
	              -L$(SYSROOT)/usr/lib \
	              -Wl,-rpath-link,$(SYSROOT)/usr/lib/arm-linux-gnueabihf \
	              -Wl,-rpath-link,$(SYSROOT)/usr/lib \
	              -lgtk-x11-2.0 -lgdk-x11-2.0 -lpangocairo-1.0 -latk-1.0 \
	              -lcairo -lgdk_pixbuf-2.0 -lgio-2.0 -lpango-1.0 \
	              -lgobject-2.0 -lglib-2.0
endif

CFLAGS := $(COMMON_FLAGS) $(ARCH_FLAGS) $(GTK_CFLAGS)
LDFLAGS := $(ARCH_FLAGS) $(GTK_LIBS) -s

all: $(TARGET)

$(TARGET): src/main.c
	@mkdir -p $(TARGET_DIR) $(dir $(DIST_BIN))
	$(CC) src/main.c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	cp $(TARGET) $(DIST_BIN)
	chmod +x $(DIST_BIN)

clean:
	rm -rf $(TARGET_DIR) $(DIST_BIN)

.PHONY: all clean