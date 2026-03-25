# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Design Goals

1. **Solarized theme, statistics-inspired aesthetic** — all colors drawn from the Solarized palette; data visualizations (bell curves, z-score indicators) are first-class UI elements, not decorations.
2. **Optimized for battery life and no-backlight readability** — minimal redraws (health data polled every 30 min, theme switch only on hour boundaries), no animations, high-contrast color choices tuned for glanceable reading without activating the backlight.

## Project Overview

"Solarized Statistician" — a Pebble watchface written in C using the Pebble SDK 4.x. It displays time, date, battery, sleep, and step count with a bell-curve visualization showing where daily steps and sleep fall relative to configurable means/standard deviations. The theme auto-switches between dark and light mode based on time of day.

## Build System

This project uses the Pebble SDK toolchain (waf-based). The project root contains both `appinfo.json` (SDK 4.x style) and `package.json` (SDK 3.x compatibility).

```bash
pebble build            # Build for all target platforms
pebble install --emulator basalt   # Run in Basalt emulator (144x168, Pebble Time)
pebble install --emulator emery    # Run in Emery emulator (200x228, Pebble Time 2)
pebble install --phone <IP>        # Install to physical watch via phone
pebble clean            # Clean build artifacts
```

## Target Platforms

Configured in `appinfo.json` under `targetPlatforms`. Currently targets **basalt** and **emery**.

- **Basalt**: 144x168 color display (Pebble Time) — fully working
- **Emery**: 200x228 color display (Pebble Time 2) — needs layout adaptation

Note: `package.json` lists all platforms (aplite, basalt, chalk, diorite, emery, flint) but `appinfo.json` is the authoritative config for SDK 4.x builds.

## Architecture

The entire watchface is a single C file: `src/c/main.c`. There are no resources (fonts/images) — all rendering uses system fonts and procedural drawing.

### Layout Structure (hardcoded for 144x168 Basalt)

The layout uses absolute pixel coordinates defined as macros:

- **Row 1 (y=6)**: Date box (left) + Battery box (right), each 66px wide
- **Time display (y=32)**: Centered, using ROBOTO_BOLD_SUBSET_49
- **Row 2 (y=90)**: Sleep box (left) + Steps box (right), each 66px wide
- **Bell curve (y=120)**: 100px wide curve at x=22, showing step distribution

### Key Design Patterns

- **Theme system**: `Theme` struct with colors for bg, text, accent, border, fill_steps, line_sleep, axis_dim. Auto-switches dark/light at configurable hours (7am–8pm).
- **Stats visualization**: Steps shown as filled area under a precomputed bell curve (`s_curve_points[]` lookup table, 100 entries). Sleep shown as a vertical indicator line protruding above the curve.
- **Z-score mapping**: `map_z_score()` converts health values to pixel positions on the curve relative to configurable mean/SD constants.
- **Health updates**: Polled every 30 minutes via tick handler, guarded by `PBL_HEALTH`.

## Emery Adaptation Notes

All layout values are hardcoded `#define` macros and `GRect()` literals for the 144x168 Basalt screen. To support Emery (200x228), these need to scale or use `PBL_IF_RECT_ELSE` / platform detection. Key areas:

- Box positions and widths (`DATE_RECT`, `BATT_RECT`, `SLEEP_RECT`, `STEP_RECT`)
- Time layer position and size (`GRect(0, 32, bounds.size.w, 54)`)
- Text layer positions (date, battery, sleep, step labels)
- Bell curve dimensions and position (`CURVE_WIDTH`, `CURVE_HEIGHT`, `CURVE_X`, `CURVE_Y`)
- Consider using `bounds.size.w` and `bounds.size.h` for proportional layout instead of absolute values
