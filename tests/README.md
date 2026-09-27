# Tests

Two suites, both run by `tools\test.ps1`:

```powershell
tools\test.ps1                  # everything, about two seconds
tools\test.ps1 -Filter render   # only suites whose name contains "render"
```

| Suite | Count | Needs hardware |
|---|---|---|
| Host unit tests (`tests/host`) | 82 | no |
| Contract checks (`tests/py`) | 33 | no |

There is no on-target test app. The graphics layer talks to the panel through
exactly one function, so stubbing that makes the framebuffer, the drawing
primitives and the whole gauge renderer testable on the desktop. That is where
bugs get caught now.

## How tests register themselves

There is no list to keep in sync. A test declares itself where it lives:

```c
TF_TEST(gauge_render, needle_points_where_the_maths_says)
{
    gauge_render_t *g = make_rpm();
    gauge_render_set_immediate(g, 4000.0f);
    gauge_render_draw(g);
    TF_CHECK_MSG(at(CX, CY - 60) == needle, "needle not straight up at 4000 rpm");
    gauge_render_destroy(g);
}
```

`TF_TEST` expands to the test function plus a constructor that files it with the
registry, so anything compiled into the binary is discovered at startup.

## Layer 1 — host unit tests

Plain C compiled with the system GCC and linked against the **real** firmware
sources. No hardware, no ESP-IDF.

The trick that makes this possible is in `tests/host/stub_bsp.c`: `gfx.c` calls
`bsp_lcd_draw_bitmap()` and nothing else, so replacing that one function gives a
real framebuffer with no SPI. `tests/host/esp_stub/` supplies two tiny headers
(`esp_err.h`, `esp_lcd_types.h`) so `bsp.h` parses without ESP-IDF, and is placed
first on the include path.

| File | Covers |
|---|---|
| `test_gauge_math.c` | angle mapping, tick counts, label formatting, the rail / tick-base / warning-sector radius chain, the needle slew filter |
| `test_gauge_theme.c` | every theme produces a coherent dial: band fits the bezel, ticks hang inside the rail, the warning sector is inboard of the ticks, numerals clear both it and the hub |
| `test_gauge_presets.c` | every preset is renderable: ordered ranges, tick budget fits the label storage, explicit labels match the tick count, generated labels fit their buffer, alarm band inside the range and wide enough to see |
| `test_gfx.c` | the primitives: clipping at all four edges, disc/ring/circle geometry, arc band covers its sweep and nothing else, polygon fill, text ink and advance, blend endpoints |
| `test_gauge_render.c` | the dial itself, rendered and inspected pixel by pixel: rail radius, warning sector inboard of the ticks, hub, needle direction at min/mid/max, needle never leaves the dial, read-out drawn, slew settles, layered geometry without overlaps, dial centred on the 320×240 rectangle |

`test_gauge_render.c` is the one that earns its keep. It found a real bug where
every glyph was drawn one ascent too low — on the panel that showed up as the
bottom of the "6" disappearing into the green band behind it, which is exactly
the sort of thing that is miserable to diagnose from a photograph.

The framework is `tests/host/test_framework.{h,c}`. Assertions come in soft
(`TF_CHECK`, `TF_EQ_INT`, `TF_NEAR`, `TF_STR_EQ`, …) and hard (`TF_REQUIRE`,
which abandons the current test).

## Layer 2 — contract checks

`tests/py/test_contracts.py` guards the assumptions the firmware makes about its
own generated artefacts. These are the assumptions that rot silently.

* **`tools/gen_font.py` must agree with the generated `gfx_font_data.c`** — every
  declared font present, at the declared pixel size, with a contiguous charset
  matching the emitted `first`/`count`.
* **Every character the gauge draws must be inside the font's range.** A stray
  degree sign or en-dash has no glyph and renders as nothing at all.
* **`tools/render_preview.py` must match the firmware** — same preset ids, same
  bezel/band/tick/hub dimensions — so the host previews stay trustworthy.
* **LVGL must not creep back in** — not a managed dependency, no `CONFIG_LV_*`
  in `sdkconfig.defaults`, no `#include "lvgl…"` anywhere.
* **Every relative link in the docs must resolve**, and point at a file that is
  actually tracked by git, so nothing 404s on GitHub.

## Adding a test

1. Put host-testable logic in `gauge_math.c`, `gauge_theme.c`,
   `gauge_presets.c` or `gfx.c` rather than in `bsp.c`.
2. Add a `TF_TEST(...)` anywhere under `tests/host/` — it is picked up
   automatically. If you add a new `.c` file, add it to the `$sources` list in
   `tools/test.ps1`.
3. If you introduce a dependency on a third-party constant or a generated
   asset, add a contract check in `tests/py/test_contracts.py`.

## A note on test expectations

Two of the original tests were wrong, not the code, and it is worth knowing why
so the same mistake is not repeated:

* `gfx_ring(cx, cy, r_outer, r_inner)` leaves `[cx-r_inner, cx+r_inner]` clear.
  Sampling exactly on the inner edge is inside the hole, not on the band.
* `gfx_fill_polygon` uses the standard scanline rule which excludes a polygon's
  topmost row. Sampling the very top row of an arc-band slice finds nothing.
  Sample mid-slice and mid-radius instead.

When a new test fails, check the expectation before changing the code — but
check the code before changing the expectation, too. The text-placement failures
above turned out to be a genuine firmware bug.

## What the 320×240 port changed

The dial is the same 240 px circle, centred in the wider panel. Three test
adjustments came out of that:

* `test_gfx.c`'s arc-band test now draws around `GFX_W/2, GFX_H/2` instead of a
  hard-coded `120,120` (which used to coincide with the centre on the 240×240
  panel).
* `test_gauge_render.c` uses `GAUGE_DIAL_DIAMETER`, not `GFX_W`, for the rail
  radius — passing the framebuffer width would have grown the dial to 320.
* A new test pins the dial to the centre of the rectangle and checks the side
  margins stay face-coloured.
