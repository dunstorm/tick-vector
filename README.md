# Tick Vector

Native C++/Qt trading workstation for live tick data, charts, DOM, and Rithmic market connectivity.

## Build

Requirements: CMake 3.24+, Ninja, Qt 6 Widgets/Charts/Network/WebSockets, protobuf/protoc, and a C++20 compiler.

```bash
make run
```

`make run` configures CMake, builds the client, and launches the toolbar shell. Use `make build` when you only want to compile. The current Rithmic Protocol bridge requires Qt WebSockets and protobuf/protoc.

## Project structure

```text
src/app        Application identity and process-wide constants
src/core       Domain types, feed configuration, persistence, adapter contracts
src/adapters   Rithmic and simulated market-data adapter implementations
src/ui         Qt windows, dialogs, chart, DOM, and toolbar shell
src/assets     Compiled Qt resources
third_party    Vendored protocol references
docs           Architecture and development notes
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for the maintained engineering notes.

## Rithmic configuration

Use the toolbar connection dropdown and choose `Feed Settings...` to add or edit feed connections. The first feed source wired in the UI is Rithmic over the R | Protocol API shape.

The current protocol files are unofficial MIT-licensed development references vendored from `rundef/async_rithmic` under `third_party/rithmic-protocol`. Replace them with the official Rithmic dev-kit files when available.

```bash
export RITHMIC_USER="..."
export RITHMIC_PASSWORD="..."
export RITHMIC_SYSTEM="..."
export RITHMIC_GATEWAY="rituz00100.rithmic.com:443"
export RITHMIC_ACCOUNT="..."
export RITHMIC_APP_NAME="TickVector"
export RITHMIC_APP_VERSION="1.0"
export TICK_VECTOR_USE_RITHMIC=1
```

For now, saved development credentials are stored as plaintext JSON under:

```text
~/Library/Application Support/Tick Vector/feed-connections.json
~/Library/Application Support/Tick Vector/rithmic-profile.json
```

Keychain support remains in the codebase as an optional backup path and can be enabled with `-DTC_ENABLE_KEYCHAIN_BACKUP=ON`.

Current adapter behavior:

- Rithmic connections use `RithmicMarketDataAdapter`.
- `RithmicMarketDataAdapter` opens a Rithmic Protocol WebSocket, validates system info, logs into the ticker plant, sends heartbeats, subscribes to live last-trade/BBO/order-book updates, and aggregates live trade ticks into chart candles.
- It does not synthesize candles, DOM, orders, or account data. If Rithmic sends nothing, the chart stays empty.
- Order routing, P&L/account state, historical replay, and symbol lookup still need dedicated plant implementations.
- `SimulatedMarketDataAdapter` remains only for non-Rithmic development paths.
- The adapter boundary is represented by `ITradingAdapter`; Rithmic callbacks are translated into `MarketSnapshot` updates.

## Current UI

- A compact frameless toolbar is the primary window. It has custom close/minimize controls on the left, logo, `New`, workspace selector, feed label, and connection selector.
- Toolbar styling uses an ImGui-inspired dark command-bar theme with custom SVG chevrons.
- Selecting a complete connection shows `Connecting`, then `Connected` only after the Rithmic Protocol adapter reports a live session.
- `New > Price Chart` stays disabled until the selected feed adapter is connected.
- `New > Price Chart` opens a Select Instrument dialog with exchange filtering, search, and a `Symbol | Description | Exchange` table.
- Price charts open in separate top-level chart windows, not inside the toolbar window.
- Feed selector lists saved connections and opens `Feed Settings...`.
- Feed Settings supports Rithmic connection profiles with workspace name, location, gateway URL, system, market data mode, username, password, and connect-on-startup toggle.

## Client foundation

- `ConnectionConfig`
- `AccountStore`
- `FeedConnectionStore`
- `ITradingAdapter`
- `MarketSnapshot`, `OrderRequest`, and execution/account data types
- `RithmicProtocolClient`
- `SimulatedMarketDataAdapter`
- Generated Rithmic protobuf ticker/login messages
