# Development

## Toolchain

Everything is installed under `C:\Espressif`:

| | |
|---|---|
| ESP-IDF | `v5.5.5` at `C:\Espressif\frameworks\esp-idf-v5.5.5` |
| Tools | `C:\Espressif\tools` |
| Python venv | `C:\Espressif\python_env\idf5.5_py3.13_env` |
| Host Python | 3.13.14 (used by `install.bat` to build the venv) |

The same `xtensa-esp-elf` toolchain serves both `esp32` and `esp32s3`, so the
install used for the round board works unchanged — only the project target
changed:

```
xtensa-esp-elf        esp-14.2.0        compiler
xtensa-esp-elf-gdb                      debugger
openocd-esp32                           on-chip debug
cmake, ninja, ccache, idf-exe
```

Nothing is added to `PATH`. Every command goes through
[`tools/idf.bat`](../tools/idf.bat), which sources `export.bat` and forwards its
arguments to `idf.py`:

```powershell
tools\idf.bat build
tools\idf.bat -p COM49 flash monitor
tools\idf.bat menuconfig
tools\idf.bat size
```

To reproduce the install from scratch:

```powershell
git clone --depth 1 --shallow-submodules --recursive --branch v5.5.5 `
    https://github.com/espressif/esp-idf.git C:\Espressif\frameworks\esp-idf-v5.5.5
$env:IDF_TOOLS_PATH = 'C:\Espressif'
cd C:\Espressif\frameworks\esp-idf-v5.5.5
.\install.bat esp32
```

## Managed components

| Component | Version | Why |
|---|---|---|
| `espressif/esp_lcd_ili9341` | 2.x | ILI9341 variant of the board only; the ST7789V uses IDF's built-in `esp_lcd_panel_st7789` |

LVGL is deliberately **not** a dependency — see
[`hardware.md`](hardware.md#fault-1--the-panel-showed-a-wrapped-frozen-image)
and the README. A contract check fails if it creeps back in.

## Build and flash cycle

```powershell
tools\idf.bat build
tools\idf.bat -p COM49 flash monitor   # Ctrl+] exits the monitor
```

`tools\flash.ps1` wraps that into one command, and
`tools\flash.ps1 -NoMonitor` skips the monitor.

Flashing is hands-free: the CH340's DTR/RTS lines are wired to IO0/EN, so
esptool resets the chip into the ROM bootloader and restarts it afterwards.
**Opening the serial port resets the board** — that is the wiring, not a fault;
wait a couple of seconds after connecting before reading the console.

## Testing

```powershell
tools\test.ps1                  # host unit tests + contract checks (~2 s)
tools\test.ps1 -Filter render   # only suites whose name contains "render"
```

Run this before flashing anything. It is fast, needs no hardware, and covers
the whole renderer.

The key enabler: **`gfx` talks to the panel through exactly one function**
(`bsp_lcd_draw_bitmap`). Stubbing that lets `gfx.c`, `gfx_text.c` and
`gauge_render.c` be compiled and exercised on the desktop with a real
framebuffer. That is where most bugs get caught now — the host tests found a
real one where every glyph was drawn one ascent too low, which on the panel
showed up as the bottom of the "6" vanishing into the green band behind it.

See [`tests/README.md`](../tests/README.md).

The host tests need a native compiler. They default to MSYS2's
`C:\msys64\mingw64\bin\gcc.exe`; override with `-Gcc <path>`. Nothing else in
the project needs it.

## Configuration

Board settings live under **`menuconfig → Gauge BSP`**:

| Option | Default | Notes |
|---|---|---|
| `BSP_PANEL_*` | ST7789V | controller choice; see hardware.md for the symptoms of each wrong answer |
| `BSP_LCD_RGB_ORDER_BGR` | n | wrong value swaps red and blue |
| `BSP_LCD_INVERT_COLOR` | n | wrong value gives a photographic negative |
| `BSP_LCD_SWAP_XY` / `BSP_LCD_MIRROR_*` | y / y / n | landscape orientation |
| `BSP_LCD_SPI_CLK_MHZ` | 80 | frame-rate ceiling; 40 is the safe fallback |
| `BSP_BACKLIGHT_DEFAULT_PERCENT` | 60 | raise with `backlight 100` |
| `BSP_TOUCH_*` | XPT2046 pins | `BSP_TOUCH_PIN_IRQ=-1` polls instead |

## Design notes

### Layering, and why it matters for tests

| File | Depends on | Tested by |
|---|---|---|
| `gauge_math.c` | libm only | host unit tests |
| `gauge_theme.c` | nothing | host unit tests |
| `gauge_presets.c` | string.h | host unit tests |
| `gfx.c` | one BSP call | host unit tests, panel stubbed |
| `gfx_text.c` | `gfx.c` | host unit tests |
| `gauge_render.c` | `gfx` | host unit tests |
| `bsp.c` | ESP-IDF | hardware only |

Resist moving arithmetic into `gauge_render.c` or `bsp.c`.

### Frame budget

A full 320×240 RGB565 frame is 150 KB. Measured on the dial:

| | |
|---|---|
| render (CPU → framebuffer) | 7.0 ms |
| panel flush (SPI, 80 MHz) | 19.3 ms |
| delivered | **34.5 fps** |

Three things got the render to 7 ms, in order of how much they mattered:

1. **Clearing only the dial's bounding square** after the first full clear —
   the 40 px side margins never change, so a third of the pixels stopped being
   repainted every frame.
2. **32-bit clears and fills**, and a byte-swap that processes two pixels per
   store. The SPI path wants the high byte of each pixel first, and swapping
   150 KB a frame in 16-bit steps was measurable.
3. **An integer-degree sine table** (`gfx_cos_deg`/`gfx_sin_deg`). The dial
   calls trig about a thousand times per frame and ESP32 libm is soft-float;
   the glow band alone cost 2.8 ms before this.

The frame loop does not sleep a fixed period: it measures the real frame
interval and passes it to the slew filter, so the panel sets the rate.

`gfx_flush_rect()` exists for partial updates if a future design needs them,
but the current renderer always redraws the whole dial: there is no cached
state to fall out of step with the panel.

### Why the fonts are generated

`tools/gen_font.py` rasterises a system TTF into 8-bit coverage masks, emitted
as `gfx_font_data.c`. No font library runs on the device, the glyphs are
anti-aliased, and the whole set is 64 KB of flash.

Two details that are easy to get wrong and are both covered by tests:

* **`bearing_y` is measured from the baseline**, not the ascender. PIL places
  the text origin on the ascender line, so the ascent has to be subtracted
  again. Getting this wrong draws every glyph one ascent too low.
* **The charset is a contiguous `0x20..0x7E`**, so the device can index glyphs
  with `(c - first)` and needs no lookup table.

Text is positioned on **cap height**, not line height: use
`gfx_text_cap_centered()`.

### Why the needle slews

`gauge_render_set_value()` only stores a target. The task moves `displayed`
toward it with an exponential approach capped by a maximum slew rate, so the
needle accelerates off a stop and settles like a real moving-coil movement.
`slew_time` in the config is the time for a full-scale ramp — 0.30 s for a
tachometer, 2.0 s for a temperature gauge.

### Why the console comes first

`app_main()` starts the UART REPL *before* `bsp_display_init()`. If the panel
fails to come up the app logs the error and returns, leaving a working console —
so a bad pin assignment or an unstable SPI clock costs you a `menuconfig` edit,
not a button-press recovery.

### The test screens and the gauge task

`app_gauge_stop()` waits for the frame in flight before returning. The test
screens drive the same SPI panel, and two tasks inside
`esp_lcd_panel_draw_bitmap()` at once is a reset waiting to happen. If a test
screen ever refuses to appear, that handshake is the first thing to check.

## Debugging

The console is the primary channel, and `ESP_LOGI` output is interleaved with it
on UART0 at 115200.

`idf.py monitor` decodes panics and backtraces automatically
(`esp-idf-panic-decoder` is installed).

For display problems there is a second channel: the bring-up screens. `test
fill` distinguishes a panel fault from a drawing fault, `test quad` shows
orientation, `test bars` shows colour order, `test grid` shows missing regions.
When the board is not next to you, a webcam pointed at the panel plus
`tools`-style pyserial scripts is enough to verify a change — a 30-line capture
script is worth more than a guess.

**JTAG** is not brought out; the console and the screens are the practical
debug channel.

## Regenerating the fonts

```powershell
python tools\gen_font.py
```

Writes `firmware/components/gfx/gfx_font_data.c` and its header. A contract
check fails if the generated file drifts from the generator's declared sizes.
