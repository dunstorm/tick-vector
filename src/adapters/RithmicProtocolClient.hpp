#pragma once

#include "core/ConnectionConfig.hpp"
#include "core/Types.hpp"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtWebSockets/QWebSocket>
#include <functional>

namespace google::protobuf {
class MessageLite;
}

namespace tc {

struct RithmicTradeTick {
    QString symbol;
    QString exchange;
    QDateTime time;
    double price{};
    int size{};
    AggressorSide aggressor{AggressorSide::Unknown};
};

struct RithmicBboTick {
    QString symbol;
    QString exchange;
    QDateTime time;
    double bidPrice{};
    int bidSize{};
    double askPrice{};
    int askSize{};
};

class RithmicProtocolClient final : public QObject {
public:
    using StatusHandler = std::function<void(const QString&)>;
    using ErrorHandler = std::function<void(const QString&)>;
    using ConnectedHandler = std::function<void()>;
    using TradeHandler = std::function<void(const RithmicTradeTick&)>;
    using BboHandler = std::function<void(const RithmicBboTick&)>;
    using OrderBookHandler = std::function<void(const QVector<DomLevel>&)>;
    using HistoryBarHandler = std::function<void(const Candle&, int loaded, int expected, qint64 trades)>;
    using HistoryFinishedHandler = std::function<void(bool ok, const QString& message)>;
    using FrontMonthHandler = std::function<void(bool ok, const QString& symbol, const QString& exchange, const QString& message)>;

    explicit RithmicProtocolClient(QObject* parent = nullptr);

    bool connectToTickerPlant(const ConnectionConfig& config);
    void disconnectFromPlant();
    bool isConnected() const;
    bool subscribeMarketData(const QString& symbol, const QString& exchange, quint32 updateBits);
    bool requestFrontMonthContract(const QString& rootSymbol, const QString& exchange);
    bool requestTimeBarBackfill(const QString& symbol, const QString& exchange, int barMinutes, int maxBars);

    void setStatusHandler(StatusHandler handler);
    void setErrorHandler(ErrorHandler handler);
    void setConnectedHandler(ConnectedHandler handler);
    void setTradeHandler(TradeHandler handler);
    void setBboHandler(BboHandler handler);
    void setOrderBookHandler(OrderBookHandler handler);
    void setHistoryBarHandler(HistoryBarHandler handler);
    void setHistoryFinishedHandler(HistoryFinishedHandler handler);
    void setFrontMonthHandler(FrontMonthHandler handler);

private:
    enum class Stage {
        Idle,
        SystemInfo,
        AwaitingSystemInfoClose,
        Login,
        Active,
        Closing
    };

    enum class HistoryStage {
        Idle,
        Login,
        Active,
        Backfilling,
        Closing
    };

    struct PendingSubscription {
        QString symbol;
        QString exchange;
        quint32 updateBits{};
    };

    struct PendingHistoryRequest {
        QString symbol;
        QString exchange;
        int barMinutes{1};
        int maxBars{240};
    };

    void openSocket(Stage stage);
    void openHistorySocket();
    void configureTls(QWebSocket& socket);
    void onSocketConnected();
    void onSocketDisconnected();
    void onHistorySocketConnected();
    void onHistorySocketDisconnected();
    void onBinaryMessageReceived(const QByteArray& frame);
    void onHistoryBinaryMessageReceived(const QByteArray& frame);
    void handlePayload(const QByteArray& payload);
    void handleHistoryPayload(const QByteArray& payload);
    void sendHeartbeat();
    void sendHeartbeat(QWebSocket& socket);
    bool sendMessage(const google::protobuf::MessageLite& message);
    bool sendMessage(QWebSocket& socket, const google::protobuf::MessageLite& message);
    bool sendPendingSubscription();
    bool sendPendingHistoryRequest();
    void finishHistoryRequest(bool ok, const QString& message);
    void scheduleReconnect(const QString& reason);
    void fail(const QString& message);
    void failHistory(const QString& message);
    void publishStatus(const QString& message);

    QString normalizedSystem(QString system) const;
    QString normalizedGatewayUrl(const QString& gateway) const;
    QString responseCodesToString(const google::protobuf::MessageLite& message) const;
    bool responseHasErrorCode(const google::protobuf::MessageLite& message) const;
    QDateTime timestampFromRithmic(int ssboe, int usecs) const;

    QWebSocket socket_;
    QWebSocket historySocket_;
    QTimer heartbeatTimer_;
    QTimer reconnectTimer_;
    QTimer historyCompletionTimer_;
    ConnectionConfig config_;
    QString gatewayUrl_;
    QString resolvedSystemName_;
    Stage stage_{Stage::Idle};
    HistoryStage historyStage_{HistoryStage::Idle};
    bool connected_{false};
    bool historyConnected_{false};
    int reconnectAttempts_{0};
    int historyBarsLoaded_{0};
    PendingSubscription pendingSubscription_;
    PendingHistoryRequest pendingHistoryRequest_;

    StatusHandler statusHandler_;
    ErrorHandler errorHandler_;
    ConnectedHandler connectedHandler_;
    TradeHandler tradeHandler_;
    BboHandler bboHandler_;
    OrderBookHandler orderBookHandler_;
    HistoryBarHandler historyBarHandler_;
    HistoryFinishedHandler historyFinishedHandler_;
    FrontMonthHandler frontMonthHandler_;
};

} // namespace tc
