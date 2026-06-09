#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtCore/QVector>

namespace tc {

enum class AggressorSide {
    Buy,
    Sell,
    Unknown
};

enum class OrderSide {
    Buy,
    Sell
};

struct Candle {
    QDateTime time;
    double open{};
    double high{};
    double low{};
    double close{};
    double volume{};
    double delta{};
};

struct BigTrade {
    int candleIndex{};
    double price{};
    int lots{};
    AggressorSide side{AggressorSide::Unknown};
};

struct ProfileBin {
    double price{};
    double volume{};
    double delta{};
    bool valueAreaHigh{};
    bool valueAreaLow{};
    bool pointOfControl{};
    bool lowVolumeNode{};
};

struct DomLevel {
    double price{};
    int bidSize{};
    int askSize{};
    int delta{};
    int sessionDelta{};
};

struct RecentOrder {
    QDateTime time;
    double price{};
    int lots{};
    AggressorSide side{AggressorSide::Unknown};
    QString route{"Market"};
};

struct PositionState {
    QString symbol{"NQ JUN25"};
    QString side{"FLAT"};
    int quantity{};
    double averagePrice{};
    double unrealizedPnl{};
};

struct RiskState {
    double stopLoss{};
    double takeProfit{};
    double maxDailyLoss{3000.0};
    double usedLossPercent{0.0};
};

struct AccountState {
    QString account{"UNCONFIGURED"};
    double equity{};
    double realizedPnl{};
    double dayPnl{};
    double drawdown{};
};

struct MarketSnapshot {
    QString symbol{"NQ JUN25"};
    QDateTime exchangeTime;
    double last{};
    double open{};
    double high{};
    double low{};
    double close{};
    QVector<Candle> candles;
    QVector<BigTrade> bigTrades;
    QVector<ProfileBin> volumeProfile;
    QVector<DomLevel> dom;
    QVector<RecentOrder> recentOrders;
    PositionState position;
    RiskState risk;
    AccountState account;
    int speedOfTape{};
    int networkLatencyMs{};
    bool connected{};
    QString connectionLabel;
};

struct OrderRequest {
    QString symbol;
    OrderSide side{OrderSide::Buy};
    int quantity{1};
};

struct ExecutionReport {
    bool accepted{};
    QString message;
    PositionState position;
};

} // namespace tc
