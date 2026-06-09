#pragma once

#include "core/MarketDataAdapter.hpp"

#include <QtCore/QObject>
#include <QtCore/QTimer>

namespace tc {

class SimulatedMarketDataAdapter final : public QObject, public ITradingAdapter {
public:
    explicit SimulatedMarketDataAdapter(QObject* parent = nullptr);

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

private:
    void seed();
    void onTimer();
    void rebuildProfile();
    void rebuildDom();
    void emitSnapshot();

    QTimer timer_;
    MarketSnapshot snapshot_;
    SnapshotHandler handler_;
    int sequence_{0};
};

} // namespace tc
