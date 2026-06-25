#pragma once

#include "core/ConnectionConfig.hpp"
#include "core/Types.hpp"

#include <functional>

class QObject;

namespace tc {

class ITradingAdapter {
public:
    using SnapshotHandler = std::function<void(const MarketSnapshot&)>;

    virtual ~ITradingAdapter() = default;

    virtual QString name() const = 0;
    virtual bool connectAdapter(const ConnectionConfig& config) = 0;
    virtual void disconnectAdapter() = 0;
    virtual bool isConnected() const = 0;
    virtual void subscribe(const QString& symbol) = 0;
    virtual MarketSnapshot snapshot() const = 0;
    virtual ExecutionReport submitMarketOrder(const OrderRequest& request) = 0;
    virtual ExecutionReport flatten() = 0;
    virtual void cancelAll() = 0;
    virtual void setSnapshotHandler(SnapshotHandler handler) = 0;
    virtual void addSnapshotHandler(QObject* context, SnapshotHandler handler) = 0;
};

} // namespace tc
