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
- **Emery**: 200x228 color display (Pebble Time 2) — enhanced layout with HR display

Note: `package.json` lists all platforms (aplite, basalt, chalk, diorite, emery, flint) but `appinfo.json` is the authoritative config for SDK 4.x builds.

## Architecture

The entire watchface is a single C file: `src/c/main.c`. There are no resources (fonts/images) — all rendering uses system fonts and procedural drawing.

### Layout System

Layout is computed at runtime from screen bounds via `compute_layout(GRect bounds)` in a `Layout` struct. All positions, box sizes, and curve dimensions derive from `bounds.size.w` and `bounds.size.h` using proportional scaling from Basalt (144x168) as the reference.

- **Width threshold (`w > 144`)** controls `is_large` (bigger boxes/fonts) and `show_hr` (heart rate display)
- **Emery enhancements**: taller info boxes (32px vs 26), GOTHIC_24_BOLD labels (vs 18), taller bell curve, HR flanking time
- **HR layers**: created conditionally; resting HR left of time, current HR right of time

### Key Design Patterns

- **Theme system**: `Theme` struct with colors for bg, text, accent, border, fill_steps, line_sleep, axis_dim, hr. Auto-switches dark/light at configurable hours (7am–8pm). HR colors: GColorRed (dark), GColorDarkCandyAppleRed (light).
- **Stats visualization**: Steps shown as filled area under a precomputed bell curve (`s_curve_points[]` lookup table, 100 entries). Sleep shown as a vertical indicator line protruding above the curve.
- **Z-score mapping**: `map_z_score()` converts health values to pixel positions on the curve relative to configurable mean/SD constants.
- **Health updates**: Polled every 30 minutes via tick handler, guarded by `PBL_HEALTH`.

## Platform Notes

- **Basalt** (144x168): Reference platform. Compact layout, GOTHIC_18_BOLD labels, no HR display.
- **Emery** (200x228): Enhanced layout triggered by `w > 144`. Taller boxes, GOTHIC_24_BOLD labels, heart rate flanking time, taller bell curve. HR read via `health_service_peek_current_value()` with accessibility guards.
- **Bell curve scaling**: The 100-point LUT (`s_curve_points[]`) is fixed. Drawing scales via integer interpolation: `lut_i = (pixel * 100) / curve_width`. Heights scale by `(point * curve_h) / 30`.
