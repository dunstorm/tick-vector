#pragma once

#include "adapters/RithmicProtocolClient.hpp"
#include "core/MarketDataAdapter.hpp"

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QVector>
#include <memory>

namespace tc {

class RithmicMarketDataAdapter final : public QObject, public ITradingAdapter {
public:
    explicit RithmicMarketDataAdapter(QObject* parent = nullptr);

    QString name() const override;
    bool connectAdapter(const ConnectionConfig& config) override;
    void disconnectAdapter() override;
    bool isConnected() const override;
    void subscribe(const QString& symbol) override;
    MarketSnapshot snapshot() const override;
    ExecutionReport submitMarketOrder(const OrderRequest& request) override;
    ExecutionReport flatten() override;
    void cancelAll() override;
    void setSnapshotHandler(SnapshotHandler handler) override;
    void addSnapshotHandler(QObject* context, SnapshotHandler handler) override;

private:
    struct SnapshotListener {
        QPointer<QObject> context;
        SnapshotHandler handler;
        bool hasContext{};
    };

    void handleStatus(const QString& message);
    void handleError(const QString& message);
    void handleConnected();
    void handleTrade(const RithmicTradeTick& trade);
    void handleBbo(const RithmicBboTick& bbo);
    void handleOrderBook(const QVector<DomLevel>& levels);
    void handleHistoricalBar(const Candle& candle, int loaded, int expected);
    void handleHistoryFinished(bool ok, const QString& message);
    void handleFrontMonth(bool ok, const QString& symbol, const QString& exchange, const QString& message);
    void resolvePendingSymbol();
    void subscribePendingSymbol();
    void startChartBackfill();
    void restoreCachedCandles();
    void mergeCandle(const Candle& candle);
    void upsertCandle(const RithmicTradeTick& trade);
    void updateProfileFromTrade(const RithmicTradeTick& trade);
    void updateProfileFromCandle(const Candle& candle);
    void recomputeProfileFlags();
    void emitSnapshot();

    std::unique_ptr<RithmicProtocolClient> protocol_;
    MarketSnapshot snapshot_;
    QVector<SnapshotListener> handlers_;
    QString pendingSymbol_;
    QString pendingExchange_;
    QString requestedRootSymbol_;
    QHash<QString, QVector<Candle>> candleCache_;
    QHash<QString, QDateTime> candleCacheRefreshedAt_;
    bool resolvingFrontMonth_{false};
    bool connectionStarted_{false};
};

} // namespace tc
