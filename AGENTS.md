# Repository Instructions

## Architecture

Tick Vector is split by source and CMake target boundaries:

- `src/app`: application identity and process-wide constants only.
- `src/core`: domain types, feed configuration, persistence, and adapter contracts.
- `src/adapters`: concrete market-data/trading adapters and Rithmic protocol code.
- `src/ui`: Qt windows, dialogs, chart, DOM, and toolbar shell.
- `third_party`: vendored protocol references used by the build.

Internal CMake targets mirror the source layout:

- `tick-vector-core`
- `tick-vector-adapters`
- `tick-vector-ui`
- `tick-vector`

Do not leak dependencies upward:

- `src/core` must not include `src/adapters` or `src/ui`.
- `src/adapters` must not include `src/ui`.
- UI code should consume `ITradingAdapter`, `MarketSnapshot`, and core domain types.
- UI must not include concrete Rithmic/protobuf headers. `TradingAdapterFactory` is currently the only allowed adapter include in UI.
- Rithmic/protobuf implementation details stay in `src/adapters`.

Run `make architecture` after changing includes or moving files.

## Chart Data Workflow

Opening a chart should subscribe to live market data first. Historical chart loading is explicit through `ITradingAdapter::requestChartData`.

Do not make every feed subscription download history. DOM/live views should be able to subscribe without triggering chart backfill. Heavy 10D chart downloads should reuse cached data when available and report progress through `MarketSnapshot` fields rather than exposing adapter-specific types to UI.

Historical chart candles are cached by `src/core/ChartDataCache` under the app data `chart-cache` directory. Keep cache file paths and serialization in core; adapters may call the cache service, but UI should not read/write cache files directly.

## Local Checks

Before committing, run:

```bash
make check
git diff --check
```

For UI changes, also run an offscreen smoke screenshot:

```bash
make screenshot
```

## Git

Use conventional commits, for example:

- `feat: add chart data loading workflow`
- `fix: avoid reconnecting unchanged feed selection`
- `build: enforce source architecture boundaries`

Do not commit build output, generated local screenshots, credentials, or local profile JSON.
