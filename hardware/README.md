# Hardware notes

Everything learned about the panel and the board on the bench: the working driver sequence, the pitfalls, the panel's limits and the sources. The parts and the wiring are in the root `README.md`; the case and assembly are on the page `bom/index.html`. The driver is `firmware/components/epd/epd.c`.

## What has been verified
- Full refresh: the image settles in a single pass, BUSY releases after 2190 ms.
- Partial refresh of the whole frame (Waveshare Display_Partial): BUSY 410 ms consistently, old digits are erased cleanly. The full "press -> new image" cycle is about 1 s (reset 200 ms + transfer of 15000 bytes at 2 MHz with CS per byte + 410 ms).
- Buttons: all four, idle level 1, debounce 20 ms (4 polls of 5 ms each) - no false or double triggers, including during fast bursts.

## Display driver (SSD1683, working sequence)
This is exactly the Waveshare code `Arduino/epd4in2_V2` / `RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_4in2_V2.c`, ported to ESP-IDF. The reference is `firmware/components/epd/epd.c`.

- SPI mode 0, 2 MHz (the datasheet allows up to 20 MHz for writes). CS goes low and high on EVERY byte, as in Waveshare's `EpdIf::SpiTransfer`. We toggle CS manually via GPIO (`spics_io_num = -1`). See the pitfalls below - this is a mandatory condition.
- HW reset: RST 1 (100 ms) -> 0 (2 ms) -> 1 (100 ms), wait for BUSY low.
- Init: `0x12` (SW reset, wait for BUSY ~10 ms) -> `0x21 0x40 0x00` -> `0x3C 0x05` -> window `0x11 0x03`, `0x44 0x00 0x31`, `0x45 0x00 0x00 0x2B 0x01`, `0x4E 0x00`, `0x4F 0x00 0x00`.
- Full (Display_Base): the frame into `0x24` and the same frame into `0x26`, then `0x22 0xF7`, `0x20`, wait for BUSY.
- Partial (Display_Partial): HW reset -> `0x3C 0x80` -> `0x21 0x00 0x00` -> `0x3C 0x80` -> window -> the frame into `0x24` only -> `0x22 0xFF`, `0x20`, wait for BUSY. One full refresh is mandatory before the first partial (it puts the "old" frame into 0x26). After N partials - a full one (Waveshare: "several times", in the FAQ - a clear after 5 rounds; the firmware has 5).
- Partial in a window (PartialDisplay): the same as Display_Partial, with `0x44`/`0x45` set to the window (X in bytes) and `0x4E`/`0x4F` to its corner, and only the window's bytes into `0x24`. Measured: 3744 bytes 0.80 s, 5760 bytes 0.84 s, 11088 bytes 0.95 s, the whole frame (15000) 1.00 s - about 19 ms per kilobyte over a fixed 0.71 s.
- Sleep: `0x10 0x01` (deep sleep mode 1, RAM is retained - needed for partial). Mode 2 (`0x03`) does not retain RAM.
- Frame format: 400x300 -> 50x300 = 15000 bytes, most significant bit on the left, 1 = white, 0 = black.
- BUSY: HIGH = busy. After `0x10` (deep sleep) BUSY stays HIGH forever - it must not be waited on, the only way out of sleep is HW reset. After sleep, image data is ignored until re-initialization.
- Waiting for BUSY is always with a timeout (in the firmware: full 4 s, partial 3 s, other 1 s). On timeout - HW reset and sleep, do not hang.

More modes from the Waveshare code (present in their driver, not properly verified on our bench):
- Fast (1.5 s / 1 s): in init after `0x3C 0x05` -> `0x1A 0x6E` (or `0x5A` for 1 s) -> `0x22 0x91`, `0x20`, wait for BUSY; display - both frames and `0x22 0xC7`. GxEPD2 for the same panel: `0x1A 0x6E` for 2024 panels, `0x64` for 2023; `0x22 0xD7`.
- 4 gray levels: `Init_4Gray` + manual LUT (`0x32`, 227 bytes) + `0x22 0xCF`. Needs a 2-bit frame (two buffers).

`0x22` values per the datasheet (bits: 0x80 clock on, 0x40 analog on, 0x20 load temp, 0x10 load LUT, 0x04 display, 0x02 analog off, 0x01 clock off):
`0xF7` full with temperature read and LUT load; `0xC7` display without LUT load; `0xFF` everything at once (partial in Waveshare); `0x91` load LUT; `0xB1` load temp + LUT; `0xE0`/`0x83` power on/off (GxEPD2); `0xFC` partial in GxEPD2.

Temperature: `0x18` after reset = `0x48` (external sensor, which the module does not have), `0x80` = built-in. Waveshare V2 does not touch this register (it relies on OTP), GxEPD2 sets `0x80`. Datasheet: if the measured temperature falls into none of the OTP ranges, there will be no refresh at all. Fast mode works around this by writing `0x1A` manually.

## Pitfalls (all caught on this bench)
1. CS over the whole data block = does not work. If CS is held low for the whole frame transfer (DMA blocks of 4000 bytes), the refresh starts, but BUSY hangs HIGH for >30 s, the image goes gray and fades with every run. With CS per byte everything is normal. The datasheet requires CS high for at least 100 ns between transfers; why exactly the block breaks the cycle is not established (GxEPD2 holds CS over the block and works on GDEY042T81). Do not economize on this.
2. In ESP-IDF you cannot attach two `spi_bus_add_device` to one CS pin: the second device takes over the GPIO matrix, the first one's CS stops toggling, the display silently stops hearing commands (BUSY 0 everywhere, "no reaction"). One device or manual CS.
3. The default FreeRTOS tick is 100 Hz: `pdMS_TO_TICKS(5) == 0`, a polling task with such a delay does not yield the CPU and hangs everything else (seen as the log cutting off after startup). In `sdkconfig.defaults`: `CONFIG_FREERTOS_HZ=1000`.
4. GPIO16/GPIO17 on the ESP32-C6 are U0TXD/U0RXD, that is, our BUSY and CS. The console must be on USB-JTAG: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y` (set in `sdkconfig.defaults`). The ROM bootloader still prints the boot log to GPIO16 on every reset (tens of ms) - straight into the display's BUSY output. The panel survives it. The efuse that disables ROM printing is irreversible - do not touch.
5. SPI2 IOMUX on the C6: CS0 = GPIO16, MISO = GPIO2 - they coincide with our BUSY and button 3. We are on the GPIO matrix, so it does not interfere, but keep it in mind when rewiring.
6. Buttons: input without internal pull-up (pull-up on the module), active LOW. GPIO0-2 are the LP group.
7. Not broken out to the header and not to be touched: GPIO9 (BOOT), GPIO15 (LED), GPIO3/GPIO14 (antenna switch: GPIO3=LOW enables the switch, GPIO14 LOW=ceramic, HIGH=U.FL), GPIO12/13 (USB).
8. ESP-IDF SPI: `SPI_TRANS_USE_TXDATA` - up to 4 bytes; with `SPI_DMA_DISABLED` a transaction is up to 64 bytes (enough for byte-by-byte transfer); with DMA the buffers must be in DMA memory, otherwise the driver copies.

## Panel limitations (Waveshare wiki + datasheet)
- Do not leave the display powered without sleep: after a refresh `0x10` or removing power is mandatory, otherwise the panel holds high voltage and degrades irreversibly.
- The recommended interval between refreshes is at least 180 s, and at least one refresh per day. You cannot live on partial refreshes indefinitely - a full one periodically.
- Operating temperature 0..50 C, storage -25..70 C, humidity 35-65 %. Not in sunlight and no UV - the particles dry out irreversibly. Clear the screen before long storage.
- Panel supply 2.3-3.6 V (not below 2.5 V), logic 3.3 V. Power during refresh 26.4 mW typ., standby < 0.01 µA. Controller logic ~1 mA in operation.
- Do not bend the flex cable toward the front side or perpendicularly; secure it for the duration of development.

## Power
- On battery the 5V pin is empty; 3V3 up to ~700 mA. Battery voltage is not routed to the ADC.
- XIAO ESP32-C6 deep sleep per Seeed data ~15 µA at 3.8 V; light sleep 3.1 mA; modem sleep 30 mA. For wake-up by button - ext1 on GPIO0-7 (there is no ext0 on the C6); the buttons on GPIO0-2 are suitable, button 4 on GPIO21 is not.

## Open questions
- The cause of the BUSY hang with CS over the block is not established (see pitfall 1). If transfer speed is needed, a controlled A/B is required: enable block CS with everything else equal and watch BUSY.
- Fast and 4Gray modes are not verified (the first fast run was with a clobbered CS and is invalid).

## Sources
- SSD1683 datasheet Rev 1.0 (Solomon Systech, 2021): https://www.laskakit.cz/user/related_files/ssd1683.pdf
- Waveshare, 4.2inch e-Paper Module Manual (V2, precautions, FAQ): https://www.waveshare.com/wiki/4.2inch_e-Paper_Module_Manual
- Waveshare, Arduino V2 driver: https://github.com/waveshareteam/e-Paper/blob/master/Arduino/epd4in2_V2/epd4in2_V2.cpp
- Waveshare, C driver for RPi V2 (Init_Fast, 4Gray, PartialDisplay): https://github.com/waveshareteam/e-Paper/blob/master/RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_4in2_V2.c
- GxEPD2, class GDEY042T81 (SSD1683, timings, fast/partial): https://github.com/ZinggJM/GxEPD2/blob/master/src/gdey/GxEPD2_420_GDEY042T81.cpp
- Seeed, XIAO ESP32-C6 Getting Started (pins, antenna, sleep): https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/
- ESP-IDF 5.5, SPI Master (CS, DMA, polling): https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-reference/peripherals/spi_master.html
- ESP-IDF, `components/soc/esp32c6/include/soc/uart_pins.h` (U0TXD=16, U0RXD=17), `spi_pins.h` (IOMUX SPI2).
