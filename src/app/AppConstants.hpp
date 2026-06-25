#pragma once

namespace tc::app {

inline constexpr auto kDisplayName = "Tick Vector";
inline constexpr auto kOrganizationName = "Tick Vector";
inline constexpr auto kRithmicAppName = "TickVector";

inline constexpr auto kUseRithmicEnv = "TICK_VECTOR_USE_RITHMIC";
inline constexpr auto kLegacyUseRithmicEnv = "TRADING_CLIENT_USE_RITHMIC";

inline constexpr auto kDataDirectoryFallback = ".tick-vector";
inline constexpr auto kRithmicProfileKeychainService = "com.tickvector.desktop.rithmic";
inline constexpr auto kFeedConnectionsKeychainService = "com.tickvector.desktop.feed-connections";

} // namespace tc::app
