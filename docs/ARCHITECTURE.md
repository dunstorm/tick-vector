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

## Chart Data

Live subscription and historical chart loading are separate adapter operations:

- `ITradingAdapter::subscribe` attaches the selected instrument to live market data.
- `ITradingAdapter::requestChartData` asks the adapter for heavier historical data.

This keeps DOM/live views from triggering expensive history downloads. Chart windows can request a 10D backfill, reuse cached candles when available, and report progress through `MarketSnapshot` fields such as `chartTicksLoaded`, `chartDaysLoaded`, and `chartBarsLoaded`.

Historical chart candles are persisted through `ChartDataCache` in `src/core`, under the app data `chart-cache` directory. Adapters may read/write this cache, but UI code should continue to request chart data only through `ITradingAdapter`.

## Build Boundaries

CMake mirrors the source architecture with internal targets:

- `tick-vector-core`: `src/app` constants plus `src/core` domain, persistence, and adapter contracts.
- `tick-vector-adapters`: concrete feed adapters and generated Rithmic protocol bindings.
- `tick-vector-ui`: Qt windows and dialogs.
- `tick-vector`: the executable composition root.

The UI target links the adapter target privately because `MainWindow` and `ConnectionTestDialog` currently use `TradingAdapterFactory` as the creation boundary. UI code should not include concrete adapter or protocol headers.

## Architecture Checks

Run:

```bash
make architecture
```

The check fails when:

- `src/app` includes core, adapter, or UI headers.
- `src/core` includes adapter or UI headers.
- `src/adapters` includes UI headers.
- UI includes concrete Rithmic/simulated adapter headers.
- generated protobuf or protobuf library headers leak outside adapters.

The same check runs in GitHub Actions for pushes to `main` and pull requests.

## Persistence

Development feed profiles are currently stored as plaintext JSON under the application data directory. Optional macOS Keychain backup support can be enabled with `-DTC_ENABLE_KEYCHAIN_BACKUP=ON`.

Credential storage is intentionally isolated in `AccountStore`, `FeedConnectionStore`, and `MacKeychain`; UI code should not write secrets directly.
