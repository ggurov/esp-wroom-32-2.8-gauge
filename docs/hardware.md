# Hardware

## Identification

Sold as a **DORHEA "ESP32 Touchscreen 2.8 Inch"** (ASIN B0D56Q7D91), the
standard **ESP32-2432S028R** — the board the community calls the CYD. The
listing describes an ILI9341 panel; the unit that arrived has an **ST7789V**.
Both are supported, and telling them apart is a two-minute job with the
bring-up screens (see "Fault 1" below).

### Enumeration on the host

```
Ports   USB-SERIAL CH340 (COM49)   USB\VID_1A86&PID_7523\6&249D62&0&1
```

`VID_1A86` is QinHeng (WCH), `PID_7523` is the CH340. There is no native
USB-Serial-JTAG interface; the Type-C/micro-USB connector is wired to the
CH340 only.

### Chip probe

```
Chip type:          ESP32-D0WD-V3 (revision v3.1)
Features:           Wi-Fi, BT, Dual Core, 240MHz, Vref calibration in eFuse
Crystal frequency:  40MHz
MAC:                cc:7b:5c:f9:01:98

Flash Memory Information:
Manufacturer: c4            (GigaDevice)
Device:       6016          (GD25Q32, 4 MB)
Detected flash size: 4MB
Flash voltage set by a strapping pin: 3.3V
```

| | |
|---|---|
| Chip | ESP32-D0WD-V3, revision v3.1 |
| Cores | 2× Xtensa LX6 @ 240 MHz |
| Crystal | 40 MHz |
| Flash | 4 MB, GigaDevice GD25Q32, quad |
| PSRAM | none (classic ESP32) |
| MAC | `cc:7b:5c:f9:01:98` |

### The factory demo

The board ships with an Arduino sketch — TFT_eSPI + LVGL 8 (the standard
`LVGL_Arduino` demo, built by vendor "JYC") — on a dual-app + SPIFFS partition
table. The whole 4 MB flash was dumped to `backup/factory-demo-4mb.bin`
(gitignored) before anything was written, so the demo can be restored.

## Pinout

### Display — ST7789V, 240×320, 4-wire SPI (driven landscape 320×240)

| Signal | GPIO | Notes |
|---|---|---|
| LCD_SCK | 14 | |
| LCD_MOSI | 13 | |
| LCD_MISO | 12 | not needed for writes; left unused by the BSP |
| LCD_CS | 15 | |
| LCD_DC | 2 | |
| LCD_RST | — | tied to the board's EN, hence `BSP_LCD_PIN_RST = -1` |
| LCD_BL | 21 | LEDC PWM, active high |

### Touch — XPT2046 resistive (its own SPI bus)

| Signal | GPIO |
|---|---|
| TOUCH_SCK | 25 |
| TOUCH_MOSI | 32 |
| TOUCH_MISO | 39 |
| TOUCH_CS | 33 |
| TOUCH_IRQ | 36 (input only, no internal pull-up) |

### Other peripherals (not used yet)

| Function | GPIO | Notes |
|---|---|---|
| UART0 TXD / RXD | 1 / 3 | → CH340, 115200 console |
| BOOT button | 0 | boot strap, active low |
| RGB LED | 4 / 16 / 17 | R / G / B |
| SD card | 18 / 19 / 23 / 5 | SCK / MISO / MOSI / CS |
| Speaker | 26 | |

## Flashing, and why it is easier than the round board

The CH340's **DTR is wired to GPIO0** and **RTS to EN**, so esptool's normal
reset sequences work:

* `idf.py -p COM49 flash` enters the ROM bootloader by itself,
* and `Hard resetting via RTS pin...` starts the new image afterwards.

No button presses, no console command. The `bootloader` console command is kept
only as a recovery path.

Two notes for the record:

* **Opening the serial port resets the board.** Any terminal that asserts
  DTR/RTS on open (pyserial does, `idf.py monitor` does) produces a boot. This
  is normal here and is *not* a fault — during bring-up a few "phantom resets"
  were chased before this was obvious. Wait ~2 s after opening a port before
  judging the output.
* The 4 MB flash is comfortable: the app is 367 KB and the table keeps a
  factory slot plus 2 MB of SPIFFS.

## Fault 1 — the panel showed a wrapped, frozen image

### Symptom

The panel lit up but showed diagonal repeats of the dial content and a stale
white band across the bottom quarter — the same frozen image on every boot,
unaffected by `test fill` or any other screen. It looked exactly like an SPI
address-window problem: content wrapping every 240 px and the last 80 rows of a
320-row frame memory never written.

### Root cause: the wrong controller driver

The listing says ILI9341, so the BSP started there. The panel is an ST7789V.
The two controllers share the command set that matters (SLPOUT, MADCTL, COLMOD,
CASET/RASET, RAMWR), which is why something *recognisable* appeared at all —
but the ILI9341 init table's power/gamma/vendor commands put the ST7789V into a
state where the address window no longer behaves, producing the wrap.

Selecting the ST7789V driver under `menuconfig → Gauge BSP` produced a clean,
correctly addressed dial immediately. `test fill` then fills the panel edge to
edge, and `test quad` shows four clean quadrants.

**Lesson:** when a panel shows *structured* garbage (repeats, wraps, stale
bands) rather than noise, suspect the controller driver before the clock or the
wiring.

## Fault 2 — red and blue were swapped

With the right controller the dial was coherent but the red needle and warning
sector rendered blue. That is the **RGB vs BGR element order**: a single bit in
MADCTL. This panel wants `LCD_RGB_ELEMENT_ORDER_RGB` (the BSP default for
ST7789V); ILI9341 boards usually want BGR.

Shapes perfect, colours wrong → element order. Colours *inverted* (black shows
white) → inversion.

## Fault 3 — the image was a photographic negative

The IDF ST7789 example turns inversion on (`INVON`); this panel needs it off.
The BSP only ever *sends* INVON when `BSP_LCD_INVERT_COLOR` is set — it never
sends INVOFF to "fix" a panel, because on the round board that cancelled the
vendor init sequence's own INVON and produced exactly this negative image. The
verified setting for this unit is inversion **off**.

## SPI clock

80 MHz was tested clean on this unit (dial and all four test screens, checked
over a webcam) and gives 34.5 fps. 40 MHz is the conservative fallback if a
different unit shows speckle or tearing; the BSP defaults live in
`sdkconfig.defaults`.

An earlier "80 MHz garbles the panel" conclusion was wrong: that garbling was
the ILI9341 driver on an ST7789V panel. With the right driver the clock is fine.

## Power

The board is powered from the PC over USB. The Amazon listing warns to use a 5 V
charger rather than a PC port, and this is worth remembering: a full-white
`test fill` at 60 % backlight pulls enough current to brown out a marginal hub.
The dial itself (mostly black) is far lighter and runs indefinitely.

## Notes for later

* **No PSRAM** on the classic ESP32. The framebuffer is 150 KB static, and
  there is ~190 KB of internal heap free with a 110 KB largest block — enough
  for the gauge, not for a second full-size framebuffer.
* **SD card** and the **RGB LED** are wired but unused; their pins are listed
  above so nothing collides later.
* **Touch calibration** is a linear map in `app_touch.c` with wide defaults;
  `touch watch` prints raw samples if a particular unit needs different
  numbers. NVS persistence is on the roadmap.
