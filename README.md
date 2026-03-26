# Solarized Statistician

**Your daily health data, visualized like a research paper.**

A minimal, statistics-inspired watchface for Pebble that plots your step count and sleep duration on a bell curve distribution. Built around the Solarized color palette with automatic dark/light theme switching based on time of day — designed to be glanceable without the backlight.

## Features

- **Bell curve visualization** — your daily steps rendered as a filled normal distribution, showing where you fall relative to your personal mean and standard deviation
- **Sleep indicator** — sleep duration marked as a vertical line on the shared curve, protruding above for visibility
- **Heart rate display** (Emery/PT2) — resting and current heart rate flanking the time, read from Pebble Health on the existing 30-minute cycle with zero additional battery cost
- **Auto dark/light theme** — switches between Solarized Dark and Solarized Light at configurable hours (default: dark 8pm–7am)
- **High-contrast, no-backlight readability** — color choices tuned for the Pebble's transflective display
- **Battery optimized** — health data polled every 30 minutes, no animations, minimal redraws
- **At-a-glance info boxes** — date, battery percentage, sleep hours, and step count in bordered panels
- **Axis tick marks** — standard deviation markers on the curve axis at -2σ, -1σ, μ, +1σ, +2σ

## Layout

| Row | Content |
|-----|---------|
| Top | Date (left) / Battery (right) |
| Center | Resting HR (left) / Time (centered) / Current HR (right) |
| Stats | Sleep hours (left) / Step count (right) |
| Bottom | Bell curve with step fill + sleep marker |

Heart rate is shown on Emery (Pebble Time 2) and other platforms with HR sensors and sufficient screen width. On Basalt, the time row shows only the time.

## Default Statistics Values

The bell curve is currently calibrated to the following hardcoded defaults. A settings page for user customization is planned.

| Metric | Mean (μ) | Standard Deviation (σ) |
|--------|----------|------------------------|
| Steps  | 7,500    | 2,500                  |
| Sleep  | 7.5 hrs  | 1.5 hrs                |

The theme switches to light mode at **7:00 AM** and back to dark mode at **8:00 PM**.

## Platforms

- **Basalt** — Pebble Time (144x168)
- **Emery** — Pebble Time 2 (200x228) — enhanced layout with larger elements, bigger fonts, and heart rate display

## Build

Requires the [Pebble SDK](https://developer.rebble.io/developer.pebble.com/sdk/index.html).

```bash
pebble build
pebble install --emulator basalt
pebble install --emulator emery
```
