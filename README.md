# mini-terminal

[English](README.md) | [Русский](README.ru.md)

A small terminal for Binance USDT-M Futures. One window with a candlestick chart and a side panel with a screener, nothing else. You can't trade from it and no API keys are needed: it only uses public data.

Written in C++17. GLFW handles the window and input (X11 and Wayland), Dear ImGui draws the interface, IXWebSocket does the networking and nlohmann/json parses the responses. It was built with tiling window managers in mind (sway, hyprland, i3), so the window gets the app_id and class `mini-terminal`, and the current price and percent change are written to the window title.

## What it does

Chart: candles, SMA 288, a volume panel split into buys and sells, a measuring ruler, and a legend for the candle under the cursor. History loads to the left as you scroll back. The last candle updates live from the socket.

Screener: every few minutes it polls all USDT pairs with at least 1M of 24h turnover, computes how far the price is from its SMA 288 and shows the result in a table. If you set a threshold, crossing it sends a desktop notification through `notify-send`.

## Building

You need a C++17 compiler, CMake 3.16+, git, OpenSSL, nlohmann-json and GLFW 3.4 or newer. If the system has no GLFW 3.4, CMake downloads it itself, but then the Wayland and X11 dev packages are required.

Debian / Ubuntu:

```sh
sudo apt install build-essential cmake git pkg-config libssl-dev nlohmann-json3-dev \
  libgl1-mesa-dev libwayland-dev libxkbcommon-dev wayland-protocols \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libnotify-bin
```

Arch:

```sh
sudo pacman -S base-devel cmake git openssl nlohmann-json glfw libnotify
```

Then:

```sh
git clone --recurse-submodules https://github.com/repixey/binance-mini-terminal.git mini-terminal
cd mini-terminal
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

The binary ends up in `build/mini_terminal`. To install it system-wide: `sudo cmake --install build --prefix /usr/local`.

If you cloned without `--recurse-submodules`, fetch the dependencies with `git submodule update --init --recursive`.

## Running

```sh
mini_terminal [TICKER] [1m|5m|15m|1h|12h|24h] [--vsync] [--x11|--wayland]
```

Everything is optional. With no arguments you get BTCUSDT on 1m candles. The order of arguments doesn't matter.

`--vsync` turns on vertical sync. It is off by default, because on Wayland a window hidden behind another one can otherwise hang waiting for a frame. `--x11` and `--wayland` force a backend; without them GLFW picks one and falls back to the others if that fails.

## Controls

| Action | Effect |
|---|---|
| Mouse wheel | zoom the time axis |
| LMB drag | scroll |
| Double click | jump back to the latest candle |
| Shift + LMB drag | ruler: price change, percent, number of bars, time |
| Esc or a plain click | remove the ruler |
| V | show or hide the volume panel |
| Field at the top left | change ticker, Enter |

In the screener, clicking a row opens that pair on the chart and right click removes it from the list. Table headers sort. The screener has its own interval, independent of the chart. It defaults to 5m, so SMA 288 covers the last 24 hours.

In the add-ticker field you can type just `SOL`, the USDT suffix is appended for you. Pairs added by hand are kept when the list is refreshed by volume.

## Alerts

The `alert SMA %` field takes a threshold. `+5` fires when the price is 5% or more above the SMA, `-3` when it is 3% or more below. An empty field turns alerts off.

A pair won't notify again until it has moved back from the threshold by at least a fifth of it. The first load after startup or after changing the interval is always silent, so you don't get a burst of notifications at launch.

## Layout

```
src/
  main.cpp         window, main loop, argument parsing
  chart.cpp        chart rendering
  screener_ui.cpp  screener panel
  screener.cpp     background REST polling and alert logic
  net.cpp          REST for history, WebSocket for live data
  session.cpp      ticker/interval switching, applying data to candles
  state.h          shared state
  types.h          data structures
  config.h         constants and limits
  util.cpp         formatting and small helpers
  worker.h         thread with a task queue
```

Network threads never touch the UI directly. They push events into a queue under a mutex and wake the main thread, which drains the queue once per frame.

The chart uses two threads: one for REST and one that manages the WebSocket lifecycle. The screener polls REST only, in its own thread, and has nothing to do with the sockets.

## About Binance

History comes from `/fapi/v1/klines`, live data from `@aggTrade` and `@kline_<interval>`. Futures streams are currently split across paths, and the code tries `/market` first. If no message arrives within 20 seconds, it switches to `/public` and reconnects.

The screener tries not to hit rate limits: requests are spread evenly over time, and on a 429 or 418 the whole queue pauses according to the `Retry-After` header. One full pass over the list costs a few hundred request weight, so don't run several copies from the same IP.

## Deploying

`scripts/deploy.sh` pulls changes, updates submodules, rebuilds and installs the binary to `/usr/local/bin`. Clone the repository on the target machine first, then run the script after every `git push`.

```sh
./scripts/deploy.sh
PREFIX=$HOME/.local ./scripts/deploy.sh
```

## Limitations

It needs a graphical session. This is not a console program and won't start on a bare server without a display.

If `notify-send` isn't installed, alerts are silently skipped and everything else keeps working.

Times on the chart are shown in the system's local time zone.
