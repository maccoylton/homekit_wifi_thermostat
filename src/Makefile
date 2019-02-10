PROGRAM = main

EXTRA_COMPONENTS = \
	extras/dht \
	extras/http-parser \
	extras/rboot-ota \
	extras/ssd1306 \
	extras/fonts \
	extras/i2c \
	$(abspath ../components/esp-wolfssl) \
	$(abspath ../components/esp-cjson) \
	$(abspath ../components/esp-homekit)\
	$(abspath ../components/esp-homekit-common-functions/button)\
	$(abspath ../components/esp-homekit-common-functions/led_codes)\
	$(abspath ../components/esp-homekit-common-functions/ota)\
	$(abspath ../components/esp-homekit-common-functions/custom_characteristics)

FONTS_TERMINUS_BOLD_8X14_ISO8859_1 = 1
FONTS_TERMINUS_BOLD_14X28_ISO8859_1 = 1

FLASH_SIZE ?= 8
HOMEKIT_SPI_FLASH_BASE_ADDR ?= 0x8c000

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS -DHOMEKIT_DEBUG -DFONTS_TERMINUS_BOLD_8X14_ISO8859_1 -DFONTS_TERMINUS_BOLD_14X28_ISO8859_1

include $(SDK_PATH)/common.mk

signature: 
	$(shell /usr/local/opt/openssl/bin/openssl   sha384 -binary -out firmware/main.bin.sig firmware/main.bin)
	$(shell printf "%08x" `cat firmware/main.bin | wc -c`| xxd -r -p >>firmware/main.bin.sig)
 
monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)
