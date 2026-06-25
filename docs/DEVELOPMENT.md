# Development

## Requirements

- CMake 3.24+
- Ninja
- Qt 6 with Core, Widgets, Charts, Network, and WebSockets
- Protobuf with `protoc`
- A C++20 compiler

## Common Commands

```bash
make build
make run
make screenshot
make screenshot-chart
make screenshot-dom
```

`make build` configures and builds `build/tick-vector`. Screenshot targets run the app with `QT_QPA_PLATFORM=offscreen` and write PNGs to `/tmp`.

The equivalent CMake preset flow is:

```bash
cmake --preset dev
cmake --build --preset dev
```

## Rithmic Development

Rithmic credentials can be supplied through Feed Settings or environment variables:

```bash
export RITHMIC_USER="..."
export RITHMIC_PASSWORD="..."
export RITHMIC_SYSTEM="..."
export RITHMIC_GATEWAY="rituz00100.rithmic.com:443"
export RITHMIC_ACCOUNT="..."
export RITHMIC_APP_NAME="TickVector"
export TICK_VECTOR_USE_RITHMIC=1
```

`TRADING_CLIENT_USE_RITHMIC=1` is still accepted as a legacy development flag.

## Git Hygiene

Build output, screenshots, local runtime state, editor files, and CMake user presets are ignored. Do not commit generated files from `build/` or local credential/profile JSON.
