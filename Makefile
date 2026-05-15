CC65      := cl65
TARGET    := c64
CFLAGS    := -t $(TARGET) -O --cpu 6502

SRC_DIR   := c64/src
BUILD_DIR := c64/build
DRV_DIR   := c64/drv
PRG       := $(BUILD_DIR)/claude64.prg
DRIVER    := $(BUILD_DIR)/c64-swlink.ser

RXTEST := $(BUILD_DIR)/rxtest.prg

SRCS := $(SRC_DIR)/main.c  \
        $(SRC_DIR)/net.c   \
        $(SRC_DIR)/ui.c    \
        $(SRC_DIR)/proto.c \
        $(SRC_DIR)/ascii.c

# VICE flags: filesystem device 8 (serves the driver file),
# plus User Port RS-232 → TCP proxy on 127.0.0.1:25232.
VICE_FLAGS := \
	-default \
	-busdevice8 \
	-fs8 "$(BUILD_DIR)" \
	-fs8savep00 \
	-fs8convertp00 \
	-acia1 \
	-acia1mode 1 \
	-acia1irq 1 \
	-myaciadev 0 \
	-acia1base 0xDE00 \
	-rsdev1 127.0.0.1:25232 \
	-rsdev1baud 2400 \
	-rsdev1ip232 \
	-autostartprgmode 1

.PHONY: all build rxtest run clean

all: build

build: $(PRG) $(DRIVER)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PRG): $(SRCS) | $(BUILD_DIR)
	$(CC65) $(CFLAGS) -o $@ $(SRCS)
	@echo "Built: $@ ($$(wc -c < $@) bytes)"

$(DRIVER): $(DRV_DIR)/c64-swlink.ser | $(BUILD_DIR)
	cp $< $@

$(RXTEST): $(SRC_DIR)/rxtest.c | $(BUILD_DIR)
	$(CC65) $(CFLAGS) -o $@ $<

rxtest: $(RXTEST) $(DRIVER)
	python3 proxy/claude_proxy.py --echo & \
	sleep 1 && \
	GSETTINGS_SCHEMA_DIR=/opt/homebrew/share/glib-2.0/schemas \
	x64sc $(VICE_FLAGS) $(RXTEST); \
	kill %1 2>/dev/null || true

run: build
	GSETTINGS_SCHEMA_DIR=/opt/homebrew/share/glib-2.0/schemas \
	x64sc $(VICE_FLAGS) $(PRG)

clean:
	rm -f $(PRG) $(DRIVER) $(BUILD_DIR)/*.map $(BUILD_DIR)/*.o
