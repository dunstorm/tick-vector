# Architecture

Tick Vector is a native Qt desktop workstation with a small application shell, market-data adapters, and top-level tool windows.

## Source Layout

- `src/app`: application identity and process-wide constants.
- `src/core`: feed configuration, credential/profile persistence, domain types, and adapter contracts.
- `src/adapters`: market-data and trading adapter implementations.
- `src/ui`: Qt widgets, dialogs, toolbar shell, charts, and DOM windows.
- `src/assets`: SVG and other UI resources compiled through `src/resources.qrc`.
- `third_party`: vendored protocol references used by the build.

## Runtime Boundaries

`MainWindow` owns the toolbar shell, saved feed profiles, and the selected shared adapter. Chart and DOM windows receive the selected `FeedConnection` plus a shared `ITradingAdapter` pointer so they render the same live market snapshot stream.

`ITradingAdapter` is the boundary between UI and venue/feed implementation. UI code should consume `MarketSnapshot`, `ExecutionReport`, and domain structs from `src/core` rather than depending on a specific broker protocol.

`RithmicProtocolClient` handles the wire protocol, websocket lifecycle, login/system validation, heartbeats, subscriptions, and protobuf parsing. `RithmicMarketDataAdapter` translates protocol callbacks into Tick Vector snapshots, candles, and DOM levels.

## Persistence

Development feed profiles are currently stored as plaintext JSON under the application data directory. Optional macOS Keychain backup support can be enabled with `-DTC_ENABLE_KEYCHAIN_BACKUP=ON`.

Credential storage is intentionally isolated in `AccountStore`, `FeedConnectionStore`, and `MacKeychain`; UI code should not write secrets directly.
