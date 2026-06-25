#include "adapters/RithmicProtocolClient.hpp"

#include "app/AppConstants.hpp"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QTimeZone>
#include <QtCore/QUrl>
#include <QtNetwork/QAbstractSocket>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslConfiguration>
#include <QtWebSockets/QWebSocketProtocol>
#include <algorithm>
#include <google/protobuf/message.h>

#include "base.pb.h"
#include "best_bid_offer.pb.h"
#include "forced_logout.pb.h"
#include "last_trade.pb.h"
#include "order_book.pb.h"
#include "reject.pb.h"
#include "request_front_month_contract.pb.h"
#include "request_heartbeat.pb.h"
#include "request_login.pb.h"
#include "request_market_data_update.pb.h"
#include "request_rithmic_system_info.pb.h"
#include "request_time_bar_replay.pb.h"
#include "response_heartbeat.pb.h"
#include "response_front_month_contract.pb.h"
#include "response_login.pb.h"
#include "response_market_data_update.pb.h"
#include "response_rithmic_system_info.pb.h"
#include "response_time_bar_replay.pb.h"

namespace tc {

namespace {

constexpr int kRequestLoginTemplate = 10;
constexpr int kResponseLoginTemplate = 11;
constexpr int kRequestHeartbeatTemplate = 18;
constexpr int kRequestMarketDataUpdateTemplate = 100;
constexpr int kRequestFrontMonthContractTemplate = 113;
constexpr int kResponseFrontMonthContractTemplate = 114;
constexpr int kRequestRithmicSystemInfoTemplate = 16;
constexpr int kRequestTimeBarReplayTemplate = 202;
constexpr int kResponseTimeBarReplayTemplate = 203;

constexpr quint32 kLastTradeBit = 1;
constexpr quint32 kBboBit = 2;
constexpr quint32 kOrderBookBit = 4;

QString withProtocolPrefix(QString url)
{
    url = url.trimmed();
    if (url.isEmpty()) {
        return {};
    }
    if (!url.contains("://")) {
        url = "wss://" + url;
    }
    return url;
}

QString firstErrorCode(const google::protobuf::MessageLite& message)
{
    const auto* reflected = dynamic_cast<const google::protobuf::Message*>(&message);
    if (!reflected) {
        return {};
    }

    const auto* descriptor = reflected->GetDescriptor();
    const auto* reflection = reflected->GetReflection();
    const auto* rpCode = descriptor ? descriptor->FindFieldByName("rp_code") : nullptr;
    if (!rpCode || !rpCode->is_repeated() || rpCode->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
        return {};
    }

    if (reflection->FieldSize(*reflected, rpCode) <= 0) {
        return {};
    }

    return QString::fromStdString(reflection->GetRepeatedString(*reflected, rpCode, 0)).trimmed();
}

QString allResponseCodes(const google::protobuf::MessageLite& message)
{
    const auto* reflected = dynamic_cast<const google::protobuf::Message*>(&message);
    if (!reflected) {
        return {};
    }

    const auto* descriptor = reflected->GetDescriptor();
    const auto* reflection = reflected->GetReflection();
    const auto* rpCode = descriptor ? descriptor->FindFieldByName("rp_code") : nullptr;
    if (!rpCode || !rpCode->is_repeated() || rpCode->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
        return {};
    }

    QStringList codes;
    const int count = reflection->FieldSize(*reflected, rpCode);
    for (int i = 0; i < count; ++i) {
        const QString code = QString::fromStdString(reflection->GetRepeatedString(*reflected, rpCode, i)).trimmed();
        if (!code.isEmpty()) {
            codes << code;
        }
    }
    return codes.join(", ");
}

bool hasErrorResponseCode(const google::protobuf::MessageLite& message)
{
    const QString code = firstErrorCode(message);
    return !code.isEmpty() && code != "0";
}

AggressorSide aggressorFromRithmic(rti::LastTrade::TransactionType type)
{
    if (type == rti::LastTrade::BUY) {
        return AggressorSide::Buy;
    }
    if (type == rti::LastTrade::SELL) {
        return AggressorSide::Sell;
    }
    return AggressorSide::Unknown;
}

} // namespace

RithmicProtocolClient::RithmicProtocolClient(QObject* parent)
    : QObject(parent)
{
    heartbeatTimer_.setTimerType(Qt::PreciseTimer);
    QObject::connect(&heartbeatTimer_, &QTimer::timeout, this, [this] { sendHeartbeat(); });
    reconnectTimer_.setSingleShot(true);
    QObject::connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        if (stage_ != Stage::Closing && !gatewayUrl_.isEmpty()) {
            publishStatus("Reconnecting to Rithmic ticker plant...");
            openSocket(resolvedSystemName_.isEmpty() ? Stage::SystemInfo : Stage::Login);
        }
    });
    historyCompletionTimer_.setSingleShot(true);
    QObject::connect(&historyCompletionTimer_, &QTimer::timeout, this, [this] {
        if (historyStage_ == HistoryStage::Backfilling) {
            finishHistoryRequest(historyBarsLoaded_ > 0, historyBarsLoaded_ > 0 ? "Historical backfill completed." : "No historical bars returned.");
        }
    });
    QObject::connect(&socket_, &QWebSocket::connected, this, [this] { onSocketConnected(); });
    QObject::connect(&socket_, &QWebSocket::disconnected, this, [this] { onSocketDisconnected(); });
    QObject::connect(&socket_, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& frame) { onBinaryMessageReceived(frame); });
    QObject::connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        fail(socket_.errorString().trimmed().isEmpty() ? "Rithmic WebSocket error." : socket_.errorString());
    });
    QObject::connect(&historySocket_, &QWebSocket::connected, this, [this] { onHistorySocketConnected(); });
    QObject::connect(&historySocket_, &QWebSocket::disconnected, this, [this] { onHistorySocketDisconnected(); });
    QObject::connect(&historySocket_, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray& frame) { onHistoryBinaryMessageReceived(frame); });
    QObject::connect(&historySocket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        failHistory(historySocket_.errorString().trimmed().isEmpty() ? "Rithmic history WebSocket error." : historySocket_.errorString());
    });
}

bool RithmicProtocolClient::connectToTickerPlant(const ConnectionConfig& config)
{
    config_ = config;
    gatewayUrl_ = normalizedGatewayUrl(config.gateway);
    resolvedSystemName_.clear();
    connected_ = false;
    historyConnected_ = false;
    reconnectAttempts_ = 0;
    heartbeatTimer_.stop();
    reconnectTimer_.stop();
    historyCompletionTimer_.stop();

    if (config_.username.trimmed().isEmpty() || config_.password.isEmpty() || config_.system.trimmed().isEmpty()) {
        fail("Rithmic username, password, and system are required.");
        return false;
    }

    if (gatewayUrl_.isEmpty()) {
        fail("Rithmic Protocol needs a WebSocket gateway URL. Use the Gateway field for a value like rituz00100.rithmic.com:443.");
        return false;
    }

    publishStatus("Connecting to Rithmic Protocol gateway...");
    openSocket(Stage::SystemInfo);
    return true;
}

void RithmicProtocolClient::disconnectFromPlant()
{
    stage_ = Stage::Closing;
    historyStage_ = HistoryStage::Closing;
    connected_ = false;
    historyConnected_ = false;
    heartbeatTimer_.stop();
    reconnectTimer_.stop();
    historyCompletionTimer_.stop();
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.close(QWebSocketProtocol::CloseCodeNormal, "Closing Rithmic connection");
    }
    if (historySocket_.state() != QAbstractSocket::UnconnectedState) {
        historySocket_.close(QWebSocketProtocol::CloseCodeNormal, "Closing Rithmic history connection");
    }
}

bool RithmicProtocolClient::isConnected() const
{
    return connected_ && stage_ == Stage::Active && socket_.state() == QAbstractSocket::ConnectedState;
}

bool RithmicProtocolClient::subscribeMarketData(const QString& symbol, const QString& exchange, quint32 updateBits)
{
    pendingSubscription_ = {symbol.trimmed(), exchange.trimmed(), updateBits};
    if (pendingSubscription_.symbol.isEmpty() || pendingSubscription_.exchange.isEmpty()) {
        fail("Rithmic subscription requires both symbol and exchange.");
        return false;
    }

    if (!isConnected()) {
        publishStatus("Queued Rithmic subscription for " + pendingSubscription_.exchange + ":" + pendingSubscription_.symbol + ".");
        return true;
    }

    return sendPendingSubscription();
}

bool RithmicProtocolClient::requestFrontMonthContract(const QString& rootSymbol, const QString& exchange)
{
    const QString cleanedSymbol = rootSymbol.trimmed();
    const QString cleanedExchange = exchange.trimmed();
    if (cleanedSymbol.isEmpty() || cleanedExchange.isEmpty()) {
        if (frontMonthHandler_) {
            frontMonthHandler_(false, {}, {}, "Front-month lookup requires both symbol and exchange.");
        }
        return false;
    }
    if (!isConnected()) {
        if (frontMonthHandler_) {
            frontMonthHandler_(false, {}, {}, "Rithmic ticker plant must be connected before front-month lookup.");
        }
        return false;
    }

    rti::RequestFrontMonthContract request;
    request.set_template_id(kRequestFrontMonthContractTemplate);
    request.add_user_msg("front_month");
    request.set_symbol(cleanedSymbol.toStdString());
    request.set_exchange(cleanedExchange.toStdString());
    request.set_need_updates(false);
    publishStatus("Resolving best contract for " + cleanedExchange + ":" + cleanedSymbol + "...");
    return sendMessage(request);
}

bool RithmicProtocolClient::requestTimeBarBackfill(const QString& symbol, const QString& exchange, int barMinutes, int maxBars)
{
    pendingHistoryRequest_ = {
        symbol.trimmed(),
        exchange.trimmed(),
        std::max(barMinutes, 1),
        std::clamp(maxBars, 1, 2000)
    };
    historyBarsLoaded_ = 0;

    if (pendingHistoryRequest_.symbol.isEmpty() || pendingHistoryRequest_.exchange.isEmpty()) {
        failHistory("Rithmic historical backfill requires both symbol and exchange.");
        return false;
    }

    if (gatewayUrl_.isEmpty()) {
        failHistory("Rithmic historical backfill needs a WebSocket gateway URL.");
        return false;
    }

    if (historyConnected_ && historySocket_.state() == QAbstractSocket::ConnectedState) {
        return sendPendingHistoryRequest();
    }

    publishStatus("Connecting to Rithmic history plant...");
    openHistorySocket();
    return true;
}

void RithmicProtocolClient::setStatusHandler(StatusHandler handler)
{
    statusHandler_ = std::move(handler);
}

void RithmicProtocolClient::setErrorHandler(ErrorHandler handler)
{
    errorHandler_ = std::move(handler);
}

void RithmicProtocolClient::setConnectedHandler(ConnectedHandler handler)
{
    connectedHandler_ = std::move(handler);
}

void RithmicProtocolClient::setTradeHandler(TradeHandler handler)
{
    tradeHandler_ = std::move(handler);
}

void RithmicProtocolClient::setBboHandler(BboHandler handler)
{
    bboHandler_ = std::move(handler);
}

void RithmicProtocolClient::setOrderBookHandler(OrderBookHandler handler)
{
    orderBookHandler_ = std::move(handler);
}

void RithmicProtocolClient::setHistoryBarHandler(HistoryBarHandler handler)
{
    historyBarHandler_ = std::move(handler);
}

void RithmicProtocolClient::setHistoryFinishedHandler(HistoryFinishedHandler handler)
{
    historyFinishedHandler_ = std::move(handler);
}

void RithmicProtocolClient::setFrontMonthHandler(FrontMonthHandler handler)
{
    frontMonthHandler_ = std::move(handler);
}

void RithmicProtocolClient::openSocket(Stage stage)
{
    stage_ = stage;
    if (socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.abort();
    }
    configureTls(socket_);
    socket_.open(QUrl(gatewayUrl_));
}

void RithmicProtocolClient::openHistorySocket()
{
    historyStage_ = HistoryStage::Login;
    historyConnected_ = false;
    historyCompletionTimer_.stop();
    if (historySocket_.state() != QAbstractSocket::UnconnectedState) {
        historySocket_.abort();
    }
    configureTls(historySocket_);
    historySocket_.open(QUrl(gatewayUrl_));
}

void RithmicProtocolClient::configureTls(QWebSocket& socket)
{
    QFile certFile(":/certificates/rithmic_ssl_cert_auth_params");
    if (!certFile.open(QIODevice::ReadOnly)) {
        return;
    }

    const auto certificates = QSslCertificate::fromData(certFile.readAll(), QSsl::Pem);
    if (certificates.isEmpty()) {
        return;
    }

    auto ssl = QSslConfiguration::defaultConfiguration();
    ssl.addCaCertificates(certificates);
    socket.setSslConfiguration(ssl);
}

void RithmicProtocolClient::onSocketConnected()
{
    if (stage_ == Stage::SystemInfo) {
        publishStatus("Requesting Rithmic system information...");
        rti::RequestRithmicSystemInfo request;
        request.set_template_id(kRequestRithmicSystemInfoTemplate);
        sendMessage(request);
        return;
    }

    if (stage_ == Stage::Login) {
        publishStatus("Logging in to Rithmic ticker plant...");
        rti::RequestLogin request;
        request.set_template_id(kRequestLoginTemplate);
        request.set_template_version("3.9");
        request.set_user(config_.username.trimmed().toStdString());
        request.set_password(config_.password.toStdString());
        request.set_system_name((resolvedSystemName_.isEmpty() ? config_.system.trimmed() : resolvedSystemName_).toStdString());
        request.set_app_name(config_.appName.trimmed().isEmpty() ? app::kRithmicAppName : config_.appName.trimmed().toStdString());
        request.set_app_version(config_.appVersion.trimmed().isEmpty() ? "1.0" : config_.appVersion.trimmed().toStdString());
        request.set_infra_type(static_cast<rti::RequestLogin::SysInfraType>(1));
        request.set_aggregated_quotes(false);
        sendMessage(request);
    }
}

void RithmicProtocolClient::onHistorySocketConnected()
{
    if (historyStage_ != HistoryStage::Login) {
        return;
    }

    publishStatus("Logging in to Rithmic history plant...");
    rti::RequestLogin request;
    request.set_template_id(kRequestLoginTemplate);
    request.set_template_version("3.9");
    request.set_user(config_.username.trimmed().toStdString());
    request.set_password(config_.password.toStdString());
    request.set_system_name((resolvedSystemName_.isEmpty() ? config_.system.trimmed() : resolvedSystemName_).toStdString());
    request.set_app_name(config_.appName.trimmed().isEmpty() ? app::kRithmicAppName : config_.appName.trimmed().toStdString());
    request.set_app_version(config_.appVersion.trimmed().isEmpty() ? "1.0" : config_.appVersion.trimmed().toStdString());
    request.set_infra_type(static_cast<rti::RequestLogin::SysInfraType>(3));
    sendMessage(historySocket_, request);
}

void RithmicProtocolClient::onSocketDisconnected()
{
    if (stage_ == Stage::AwaitingSystemInfoClose) {
        openSocket(Stage::Login);
        return;
    }

    if (stage_ == Stage::Closing || stage_ == Stage::Idle) {
        stage_ = Stage::Idle;
        connected_ = false;
        return;
    }

    if (connected_) {
        connected_ = false;
        const QString reason = socket_.closeReason().trimmed().isEmpty() ? "Rithmic Protocol connection closed." : socket_.closeReason().trimmed();
        scheduleReconnect(reason);
    }
}

void RithmicProtocolClient::onHistorySocketDisconnected()
{
    historyCompletionTimer_.stop();
    historyConnected_ = false;
    if (historyStage_ == HistoryStage::Closing || historyStage_ == HistoryStage::Idle) {
        historyStage_ = HistoryStage::Idle;
        return;
    }

    const QString reason = historySocket_.closeReason().trimmed().isEmpty() ? "Rithmic history connection closed." : historySocket_.closeReason().trimmed();
    failHistory(reason);
    if (!connected_) {
        heartbeatTimer_.stop();
    }
}

void RithmicProtocolClient::onBinaryMessageReceived(const QByteArray& frame)
{
    int offset = 0;
    while (offset + 4 <= frame.size()) {
        const auto b0 = static_cast<quint8>(frame.at(offset));
        const auto b1 = static_cast<quint8>(frame.at(offset + 1));
        const auto b2 = static_cast<quint8>(frame.at(offset + 2));
        const auto b3 = static_cast<quint8>(frame.at(offset + 3));
        const int length = static_cast<int>((b0 << 24) | (b1 << 16) | (b2 << 8) | b3);
        if (length <= 0 || offset + 4 + length > frame.size()) {
            fail("Received malformed Rithmic protobuf frame.");
            return;
        }

        handlePayload(frame.mid(offset + 4, length));
        offset += 4 + length;
    }
}

void RithmicProtocolClient::onHistoryBinaryMessageReceived(const QByteArray& frame)
{
    int offset = 0;
    while (offset + 4 <= frame.size()) {
        const auto b0 = static_cast<quint8>(frame.at(offset));
        const auto b1 = static_cast<quint8>(frame.at(offset + 1));
        const auto b2 = static_cast<quint8>(frame.at(offset + 2));
        const auto b3 = static_cast<quint8>(frame.at(offset + 3));
        const int length = static_cast<int>((b0 << 24) | (b1 << 16) | (b2 << 8) | b3);
        if (length <= 0 || offset + 4 + length > frame.size()) {
            failHistory("Received malformed Rithmic history protobuf frame.");
            return;
        }

        handleHistoryPayload(frame.mid(offset + 4, length));
        offset += 4 + length;
    }
}

void RithmicProtocolClient::handlePayload(const QByteArray& payload)
{
    rti::Base base;
    if (!base.ParseFromArray(payload.constData(), payload.size())) {
        fail("Could not parse Rithmic base message.");
        return;
    }

    switch (base.template_id()) {
    case kResponseLoginTemplate: {
        rti::ResponseLogin response;
        if (!response.ParseFromArray(payload.constData(), payload.size())) {
            fail("Could not parse Rithmic login response.");
            return;
        }
        if (hasErrorResponseCode(response)) {
            fail("Rithmic login failed: " + allResponseCodes(response));
            return;
        }
        connected_ = true;
        stage_ = Stage::Active;
        const int heartbeatMs = response.heartbeat_interval() > 0 ? static_cast<int>(response.heartbeat_interval() * 1000.0) : 30000;
        heartbeatTimer_.start(std::max(heartbeatMs - 1000, 1000));
        reconnectAttempts_ = 0;
        sendHeartbeat();
        publishStatus("Rithmic ticker plant connected.");
        if (connectedHandler_) {
            connectedHandler_();
        }
        sendPendingSubscription();
        break;
    }
    case 17: {
        rti::ResponseRithmicSystemInfo response;
        if (!response.ParseFromArray(payload.constData(), payload.size())) {
            fail("Could not parse Rithmic system info response.");
            return;
        }
        if (hasErrorResponseCode(response)) {
            fail("Rithmic system info failed: " + allResponseCodes(response));
            return;
        }

        bool systemFound = false;
        const QString requestedSystem = normalizedSystem(config_.system);
        for (int i = 0; i < response.system_name_size(); ++i) {
            const QString offeredSystem = QString::fromStdString(response.system_name(i)).trimmed();
            if (normalizedSystem(offeredSystem) == requestedSystem) {
                systemFound = true;
                resolvedSystemName_ = offeredSystem;
                break;
            }
        }
        if (!systemFound) {
            fail("Rithmic system was not offered by this gateway: " + config_.system.trimmed() + ".");
            return;
        }

        publishStatus("Rithmic system accepted. Reconnecting for ticker login...");
        stage_ = Stage::AwaitingSystemInfoClose;
        socket_.close(QWebSocketProtocol::CloseCodeNormal, "Reconnect for login");
        break;
    }
    case 19:
        break;
    case 75: {
        rti::Reject reject;
        reject.ParseFromArray(payload.constData(), payload.size());
        fail("Rithmic rejected request: " + allResponseCodes(reject));
        break;
    }
    case 77:
        fail("Rithmic forced logout. Check concurrent sessions for this user.");
        break;
    case 101: {
        rti::ResponseMarketDataUpdate response;
        if (!response.ParseFromArray(payload.constData(), payload.size())) {
            fail("Could not parse Rithmic market data response.");
            return;
        }
        if (hasErrorResponseCode(response)) {
            publishStatus("Rithmic market data subscription failed: " + allResponseCodes(response));
            return;
        }
        publishStatus("Subscribed to " + pendingSubscription_.exchange + ":" + pendingSubscription_.symbol + ".");
        break;
    }
    case kResponseFrontMonthContractTemplate: {
        rti::ResponseFrontMonthContract response;
        if (!response.ParseFromArray(payload.constData(), payload.size())) {
            if (frontMonthHandler_) {
                frontMonthHandler_(false, {}, {}, "Could not parse Rithmic front-month response.");
            }
            return;
        }
        if (hasErrorResponseCode(response)) {
            if (frontMonthHandler_) {
                frontMonthHandler_(false, {}, {}, "Rithmic front-month lookup failed: " + allResponseCodes(response));
            }
            return;
        }

        QString resolvedSymbol = QString::fromStdString(response.trading_symbol()).trimmed();
        if (resolvedSymbol.isEmpty() && response.is_front_month_symbol()) {
            resolvedSymbol = QString::fromStdString(response.symbol()).trimmed();
        }
        const QString resolvedExchange = QString::fromStdString(response.trading_exchange()).trimmed();
        if (frontMonthHandler_) {
            frontMonthHandler_(
                !resolvedSymbol.isEmpty(),
                resolvedSymbol,
                resolvedExchange.isEmpty() ? QString::fromStdString(response.exchange()).trimmed() : resolvedExchange,
                resolvedSymbol.isEmpty() ? "Rithmic did not return a front-month trading symbol." : QString{}
            );
        }
        break;
    }
    case 150: {
        rti::LastTrade trade;
        if (!trade.ParseFromArray(payload.constData(), payload.size())) {
            return;
        }
        if (trade.is_snapshot() || (trade.presence_bits() & kLastTradeBit) == 0 || trade.trade_price() <= 0.0 || trade.trade_size() <= 0) {
            return;
        }
        if (tradeHandler_) {
            tradeHandler_({
                QString::fromStdString(trade.symbol()),
                QString::fromStdString(trade.exchange()),
                timestampFromRithmic(trade.ssboe(), trade.usecs()),
                trade.trade_price(),
                trade.trade_size(),
                aggressorFromRithmic(trade.aggressor())
            });
        }
        break;
    }
    case 151: {
        rti::BestBidOffer bbo;
        if (!bbo.ParseFromArray(payload.constData(), payload.size())) {
            return;
        }
        if (bboHandler_) {
            bboHandler_({
                QString::fromStdString(bbo.symbol()),
                QString::fromStdString(bbo.exchange()),
                timestampFromRithmic(bbo.ssboe(), bbo.usecs()),
                bbo.bid_price(),
                bbo.bid_size(),
                bbo.ask_price(),
                bbo.ask_size()
            });
        }
        break;
    }
    case 156: {
        rti::OrderBook book;
        if (!book.ParseFromArray(payload.constData(), payload.size())) {
            return;
        }
        QVector<DomLevel> levels;
        for (int i = 0; i < book.bid_price_size(); ++i) {
            levels.push_back({book.bid_price(i), i < book.bid_size_size() ? book.bid_size(i) : 0, 0, 0, 0});
        }
        for (int i = 0; i < book.ask_price_size(); ++i) {
            levels.push_back({book.ask_price(i), 0, i < book.ask_size_size() ? book.ask_size(i) : 0, 0, 0});
        }
        std::sort(levels.begin(), levels.end(), [](const DomLevel& left, const DomLevel& right) {
            return left.price > right.price;
        });
        if (orderBookHandler_) {
            orderBookHandler_(levels);
        }
        break;
    }
    default:
        break;
    }
}

void RithmicProtocolClient::handleHistoryPayload(const QByteArray& payload)
{
    rti::Base base;
    if (!base.ParseFromArray(payload.constData(), payload.size())) {
        failHistory("Could not parse Rithmic history base message.");
        return;
    }

    switch (base.template_id()) {
    case kResponseLoginTemplate: {
        rti::ResponseLogin response;
        if (!response.ParseFromArray(payload.constData(), payload.size())) {
            failHistory("Could not parse Rithmic history login response.");
            return;
        }
        if (hasErrorResponseCode(response)) {
            failHistory("Rithmic history login failed: " + allResponseCodes(response));
            return;
        }

        historyConnected_ = true;
        historyStage_ = HistoryStage::Active;
        const int heartbeatMs = response.heartbeat_interval() > 0 ? static_cast<int>(response.heartbeat_interval() * 1000.0) : 30000;
        if (!heartbeatTimer_.isActive()) {
            heartbeatTimer_.start(std::max(heartbeatMs - 1000, 1000));
        }
        sendHeartbeat(historySocket_);
        publishStatus("Rithmic history plant connected.");
        sendPendingHistoryRequest();
        break;
    }
    case kResponseTimeBarReplayTemplate: {
        rti::ResponseTimeBarReplay response;
        if (!response.ParseFromArray(payload.constData(), payload.size())) {
            failHistory("Could not parse Rithmic historical bar response.");
            return;
        }

        const bool hasBar = response.marker() > 0
            && (response.open_price() != 0.0 || response.high_price() != 0.0 || response.low_price() != 0.0 || response.close_price() != 0.0);
        if (hasBar) {
            ++historyBarsLoaded_;
            const int periodSeconds = std::max(pendingHistoryRequest_.barMinutes, 1) * 60;
            const qint64 bucketStart = static_cast<qint64>(response.marker()) - periodSeconds;
            const double delta = static_cast<double>(response.ask_volume()) - static_cast<double>(response.bid_volume());
            const Candle candle{
                QDateTime::fromSecsSinceEpoch(bucketStart, QTimeZone::UTC),
                response.open_price(),
                response.high_price(),
                response.low_price(),
                response.close_price(),
                static_cast<double>(response.volume()),
                delta
            };
            if (historyBarHandler_) {
                historyBarHandler_(candle, historyBarsLoaded_, pendingHistoryRequest_.maxBars);
            }
            historyCompletionTimer_.start(2500);
            break;
        }

        if (hasErrorResponseCode(response)) {
            failHistory("Rithmic historical backfill failed: " + allResponseCodes(response));
            return;
        }

        if (response.rp_code_size() > 0 || response.rq_handler_rp_code_size() > 0) {
            finishHistoryRequest(true, "Historical backfill completed.");
        }
        break;
    }
    case 19:
        break;
    case 75: {
        rti::Reject reject;
        reject.ParseFromArray(payload.constData(), payload.size());
        failHistory("Rithmic rejected historical request: " + allResponseCodes(reject));
        break;
    }
    case 77:
        failHistory("Rithmic forced logout from history plant. Check concurrent sessions for this user.");
        break;
    default:
        break;
    }
}

void RithmicProtocolClient::sendHeartbeat()
{
    if (socket_.state() == QAbstractSocket::ConnectedState) {
        sendHeartbeat(socket_);
    }
    if (historySocket_.state() == QAbstractSocket::ConnectedState) {
        sendHeartbeat(historySocket_);
    }
}

void RithmicProtocolClient::sendHeartbeat(QWebSocket& socket)
{
    if (socket.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    const qint64 ms = now.toMSecsSinceEpoch();
    rti::RequestHeartbeat request;
    request.set_template_id(kRequestHeartbeatTemplate);
    request.set_ssboe(static_cast<int>(ms / 1000));
    request.set_usecs(static_cast<int>((ms % 1000) * 1000));
    sendMessage(socket, request);
}

bool RithmicProtocolClient::sendMessage(const google::protobuf::MessageLite& message)
{
    return sendMessage(socket_, message);
}

bool RithmicProtocolClient::sendMessage(QWebSocket& socket, const google::protobuf::MessageLite& message)
{
    if (socket.state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    std::string serialized;
    if (!message.SerializeToString(&serialized)) {
        fail("Could not serialize Rithmic protobuf message.");
        return false;
    }

    const auto length = static_cast<quint32>(serialized.size());
    QByteArray frame;
    frame.reserve(static_cast<qsizetype>(serialized.size() + 4));
    frame.append(static_cast<char>((length >> 24) & 0xff));
    frame.append(static_cast<char>((length >> 16) & 0xff));
    frame.append(static_cast<char>((length >> 8) & 0xff));
    frame.append(static_cast<char>(length & 0xff));
    frame.append(serialized.data(), static_cast<qsizetype>(serialized.size()));
    socket.sendBinaryMessage(frame);
    return true;
}

bool RithmicProtocolClient::sendPendingSubscription()
{
    if (!isConnected() || pendingSubscription_.symbol.isEmpty() || pendingSubscription_.exchange.isEmpty()) {
        return false;
    }

    rti::RequestMarketDataUpdate request;
    request.set_template_id(kRequestMarketDataUpdateTemplate);
    request.set_symbol(pendingSubscription_.symbol.toStdString());
    request.set_exchange(pendingSubscription_.exchange.toStdString());
    request.set_request(static_cast<rti::RequestMarketDataUpdate::Request>(1));
    request.set_update_bits(pendingSubscription_.updateBits == 0 ? kLastTradeBit | kBboBit | kOrderBookBit : pendingSubscription_.updateBits);
    publishStatus("Subscribing to " + pendingSubscription_.exchange + ":" + pendingSubscription_.symbol + "...");
    return sendMessage(request);
}

bool RithmicProtocolClient::sendPendingHistoryRequest()
{
    if (!historyConnected_ || historySocket_.state() != QAbstractSocket::ConnectedState || pendingHistoryRequest_.symbol.isEmpty() || pendingHistoryRequest_.exchange.isEmpty()) {
        return false;
    }

    const QDateTime finish = QDateTime::currentDateTimeUtc();
    const qint64 lookbackSeconds = static_cast<qint64>(pendingHistoryRequest_.barMinutes) * 60 * pendingHistoryRequest_.maxBars;
    const QDateTime start = finish.addSecs(-lookbackSeconds);

    historyBarsLoaded_ = 0;
    historyStage_ = HistoryStage::Backfilling;
    rti::RequestTimeBarReplay request;
    request.set_template_id(kRequestTimeBarReplayTemplate);
    request.add_user_msg("chart_backfill");
    request.set_symbol(pendingHistoryRequest_.symbol.toStdString());
    request.set_exchange(pendingHistoryRequest_.exchange.toStdString());
    request.set_bar_type(rti::RequestTimeBarReplay::MINUTE_BAR);
    request.set_bar_type_period(pendingHistoryRequest_.barMinutes);
    request.set_start_index(static_cast<int>(start.toSecsSinceEpoch()));
    request.set_finish_index(static_cast<int>(finish.toSecsSinceEpoch()));
    request.set_user_max_count(pendingHistoryRequest_.maxBars);
    request.set_direction(rti::RequestTimeBarReplay::FIRST);
    request.set_time_order(rti::RequestTimeBarReplay::FORWARDS);
    request.set_resume_bars(false);
    publishStatus("Building chart data: 0 / " + QString::number(pendingHistoryRequest_.maxBars) + " bars.");
    historyCompletionTimer_.start(15000);
    return sendMessage(historySocket_, request);
}

void RithmicProtocolClient::finishHistoryRequest(bool ok, const QString& message)
{
    historyCompletionTimer_.stop();
    if (historyStage_ != HistoryStage::Closing) {
        historyStage_ = historyConnected_ ? HistoryStage::Active : HistoryStage::Idle;
    }
    if (historyFinishedHandler_) {
        historyFinishedHandler_(ok, message);
    }
}

void RithmicProtocolClient::scheduleReconnect(const QString& reason)
{
    if (!historyConnected_) {
        heartbeatTimer_.stop();
    }
    if (stage_ == Stage::Closing || stage_ == Stage::Idle) {
        return;
    }

    ++reconnectAttempts_;
    const int delayMs = std::min(1000 * reconnectAttempts_, 5000);
    publishStatus(reason + " Reconnecting in " + QString::number(delayMs / 1000.0, 'f', 1) + "s...");
    reconnectTimer_.start(delayMs);
}

void RithmicProtocolClient::fail(const QString& message)
{
    heartbeatTimer_.stop();
    reconnectTimer_.stop();
    connected_ = false;
    if (stage_ != Stage::Closing && socket_.state() != QAbstractSocket::UnconnectedState) {
        socket_.abort();
    }
    stage_ = Stage::Idle;
    if (errorHandler_) {
        errorHandler_(message.trimmed().isEmpty() ? "Rithmic Protocol connection failed." : message);
    }
}

void RithmicProtocolClient::failHistory(const QString& message)
{
    historyCompletionTimer_.stop();
    historyConnected_ = false;
    if (historyStage_ != HistoryStage::Closing && historySocket_.state() != QAbstractSocket::UnconnectedState) {
        historySocket_.abort();
    }
    historyStage_ = HistoryStage::Idle;
    if (historyFinishedHandler_) {
        historyFinishedHandler_(false, message.trimmed().isEmpty() ? "Rithmic historical backfill failed." : message);
    }
}

void RithmicProtocolClient::publishStatus(const QString& message)
{
    if (statusHandler_) {
        statusHandler_(message);
    }
}

QString RithmicProtocolClient::normalizedSystem(QString system) const
{
    return system.remove(' ').trimmed().toLower();
}

QString RithmicProtocolClient::normalizedGatewayUrl(const QString& gateway) const
{
    const QString trimmed = gateway.trimmed();
    if (trimmed.compare("Rithmic Test", Qt::CaseInsensitive) == 0 || trimmed.compare("Test", Qt::CaseInsensitive) == 0) {
        return "wss://rituz00100.rithmic.com:443";
    }
    if (trimmed.contains('.') || trimmed.contains(':')) {
        return withProtocolPrefix(trimmed);
    }
    return {};
}

QString RithmicProtocolClient::responseCodesToString(const google::protobuf::MessageLite& message) const
{
    return allResponseCodes(message);
}

bool RithmicProtocolClient::responseHasErrorCode(const google::protobuf::MessageLite& message) const
{
    return hasErrorResponseCode(message);
}

QDateTime RithmicProtocolClient::timestampFromRithmic(int ssboe, int usecs) const
{
    if (ssboe <= 0) {
        return QDateTime::currentDateTimeUtc();
    }
    return QDateTime::fromSecsSinceEpoch(ssboe, QTimeZone::UTC).addMSecs(usecs / 1000);
}

} // namespace tc
