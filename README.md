# Trading Client

Native C++/Qt Rithmic trading client foundation.

## Build

```bash
make run
```

`make run` configures CMake, builds the client, and launches the toolbar shell. Use `make build` when you only want to compile.

## Rithmic configuration

Use the toolbar connection dropdown and choose `Feed Settings...` to add or edit feed connections. The first feed source wired in the UI is Rithmic.

Credentials alone do not link the Rithmic API; the Rithmic SDK/proto access path still has to be added to the adapter boundary.

```bash
export RITHMIC_USER="..."
export RITHMIC_PASSWORD="..."
export RITHMIC_SYSTEM="..."
export RITHMIC_GATEWAY="..."
export RITHMIC_ACCOUNT="..."
export RITHMIC_APP_NAME="TradingClient"
export TRADING_CLIENT_USE_RITHMIC=1
```

For now, saved development credentials are stored as plaintext JSON under:

```text
~/Library/Application Support/Trading Client/feed-connections.json
~/Library/Application Support/Trading Client/rithmic-profile.json
```

Keychain support remains in the codebase as an optional backup path and can be enabled with `-DTC_ENABLE_KEYCHAIN_BACKUP=ON`.

Current adapter behavior:

- Uses `SimulatedMarketDataAdapter` as a temporary test adapter.
- The Rithmic boundary is represented by `ITradingAdapter`.
- Add the real implementation by replacing/injecting an adapter that implements `connectAdapter`, `subscribe`, `submitMarketOrder`, `flatten`, and `cancelAll`.

## Current UI

- A compact frameless toolbar is the primary window. It has custom close/minimize controls on the left, logo, `New`, workspace selector, feed label, and connection selector.
- Toolbar styling uses an ImGui-inspired dark command-bar theme with custom SVG chevrons.
- Selecting a complete connection shows `Connecting`, then `Connected`.
- `New > Price Chart` stays disabled until a complete feed connection is connected.
- `New > Price Chart` opens a Select Instrument dialog with exchange filtering, search, and a `Symbol | Description | Exchange` table.
- Price charts open in separate top-level chart windows, not inside the toolbar window.
- Feed selector lists saved connections and opens `Feed Settings...`.
- Feed Settings supports Rithmic connection profiles with workspace name, location, gateway, system, market data mode, username, password, and connect-on-startup toggle.

## Client foundation

- `ConnectionConfig`
- `AccountStore`
- `FeedConnectionStore`
- `ITradingAdapter`
- `MarketSnapshot`, `OrderRequest`, and execution/account data types
- `SimulatedMarketDataAdapter`
- Rithmic SDK/protocol CMake option placeholders
