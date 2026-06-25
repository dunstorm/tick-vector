#include "adapters/TradingAdapterFactory.hpp"
#include "app/AppConstants.hpp"
#include "core/FeedConnectionStore.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ChartWindow.hpp"
#include "ui/ConnectionTestDialog.hpp"
#include "ui/DomWindow.hpp"
#include "ui/FeedSettingsDialog.hpp"
#include "ui/SelectInstrumentDialog.hpp"

#include <QtGui/QFont>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <algorithm>

namespace {

QString valueAfterFlag(const QStringList& args, const QString& flag, const QString& fallback = {})
{
    const int index = args.indexOf(flag);
    if (index < 0 || index + 1 >= args.size()) {
        return fallback;
    }
    return args.at(index + 1);
}

void parseInstrument(const QString& input, QString* exchange, QString* symbol)
{
    QString cleaned = input.trimmed();
    if (cleaned.isEmpty()) {
        cleaned = "CME:NQ";
    }

    const QString separator = cleaned.contains(':') ? ":" : cleaned.contains('/') ? "/" : QString{};
    if (!separator.isEmpty()) {
        const QStringList parts = cleaned.split(separator, Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            *exchange = parts.at(0).trimmed().toUpper();
            *symbol = parts.at(1).trimmed().toUpper();
            return;
        }
    }

    *exchange = "CME";
    *symbol = cleaned.toUpper();
}

tc::FeedConnection devChartConnection(const QString& requestedConnection)
{
    QString error;
    const QVector<tc::FeedConnection> connections = tc::FeedConnectionStore().load(&error);
    const auto isRequested = [&requestedConnection](const tc::FeedConnection& connection) {
        return !requestedConnection.trimmed().isEmpty()
            && (connection.id.compare(requestedConnection, Qt::CaseInsensitive) == 0
                || connection.name.compare(requestedConnection, Qt::CaseInsensitive) == 0);
    };
    const auto complete = [](const tc::FeedConnection& connection) {
        return connection.isComplete();
    };

    auto it = std::find_if(connections.begin(), connections.end(), [&](const tc::FeedConnection& connection) {
        return isRequested(connection) && complete(connection);
    });
    if (it == connections.end()) {
        it = std::find_if(connections.begin(), connections.end(), [](const tc::FeedConnection& connection) {
            return connection.connectOnStartup && connection.isComplete();
        });
    }
    if (it == connections.end()) {
        it = std::find_if(connections.begin(), connections.end(), complete);
    }
    if (it != connections.end()) {
        return *it;
    }

    tc::FeedConnection simulator;
    simulator.id = "simulator";
    simulator.name = "Simulator";
    simulator.feedSource = "Simulator";
    simulator.account = "SIM-ACCOUNT";
    return simulator;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(tc::app::kDisplayName);
    QApplication::setOrganizationName(tc::app::kOrganizationName);
    QApplication::setFont(QFont("Helvetica Neue", 13));

    const QStringList args = QApplication::arguments();
    const int chartScreenshotIndex = args.indexOf("--screenshot-chart");
    if (chartScreenshotIndex >= 0 && chartScreenshotIndex + 1 < args.size()) {
        tc::MainWindow styleHost(nullptr, false);
        tc::FeedConnection connection;
        connection.name = "Rithmic";
        connection.feedSource = "Simulator";
        auto adapter = tc::createTradingAdapter(connection);
        adapter->connectAdapter(connection.toConnectionConfig());
        tc::ChartWindow chart(connection, adapter.get());
        chart.show();
        app.processEvents();
        const QPixmap capture = chart.grab();
        return capture.save(args.at(chartScreenshotIndex + 1)) ? 0 : 1;
    }

    const int domScreenshotIndex = args.indexOf("--screenshot-dom");
    if (domScreenshotIndex >= 0 && domScreenshotIndex + 1 < args.size()) {
        tc::MainWindow styleHost(nullptr, false);
        tc::FeedConnection connection;
        connection.name = "Rithmic";
        connection.feedSource = "Simulator";
        auto adapter = tc::createTradingAdapter(connection);
        adapter->connectAdapter(connection.toConnectionConfig());
        tc::DomWindow dom(connection, adapter.get(), "GC", "COMEX");
        dom.show();
        app.processEvents();
        const QPixmap capture = dom.grab();
        return capture.save(args.at(domScreenshotIndex + 1)) ? 0 : 1;
    }

    const int feedSettingsScreenshotIndex = args.indexOf("--screenshot-feed-settings");
    if (feedSettingsScreenshotIndex >= 0 && feedSettingsScreenshotIndex + 1 < args.size()) {
        tc::MainWindow styleHost(nullptr, false);
        QVector<tc::FeedConnection> connections;
        tc::FeedSettingsDialog dialog(connections);
        dialog.show();
        app.processEvents();
        const QPixmap capture = dialog.grab();
        return capture.save(args.at(feedSettingsScreenshotIndex + 1)) ? 0 : 1;
    }

    const int connectionTestScreenshotIndex = args.indexOf("--screenshot-connection-test");
    if (connectionTestScreenshotIndex >= 0 && connectionTestScreenshotIndex + 1 < args.size()) {
        tc::MainWindow styleHost(nullptr, false);
        tc::FeedConnection connection;
        connection.name = "Rithmic";
        connection.feedSource = "Rithmic";
        connection.username = "demo-user";
        connection.password = "demo-password";
        connection.gateway = "rituz00100.rithmic.com:443";
        connection.system = "Rithmic Test";
        tc::ConnectionTestDialog dialog(connection);
        dialog.show();
        app.processEvents();
        const QPixmap capture = dialog.grab();
        return capture.save(args.at(connectionTestScreenshotIndex + 1)) ? 0 : 1;
    }

    const int instrumentScreenshotIndex = args.indexOf("--screenshot-instrument");
    if (instrumentScreenshotIndex >= 0 && instrumentScreenshotIndex + 1 < args.size()) {
        tc::MainWindow styleHost(nullptr, false);
        tc::SelectInstrumentDialog dialog;
        dialog.show();
        app.processEvents();
        const QPixmap capture = dialog.grab();
        return capture.save(args.at(instrumentScreenshotIndex + 1)) ? 0 : 1;
    }

    const int screenshotIndex = args.indexOf("--screenshot");
    if (screenshotIndex >= 0 && screenshotIndex + 1 < args.size()) {
        tc::MainWindow window(nullptr, false);
        window.show();
        app.processEvents();
        const QPixmap capture = window.grab();
        return capture.save(args.at(screenshotIndex + 1)) ? 0 : 1;
    }

    const int devChartIndex = args.indexOf("--dev-chart");
    if (devChartIndex >= 0) {
        QString exchange;
        QString symbol;
        parseInstrument(devChartIndex + 1 < args.size() && !args.at(devChartIndex + 1).startsWith("--") ? args.at(devChartIndex + 1) : "CME:NQ", &exchange, &symbol);
        tc::FeedConnection connection = devChartConnection(valueAfterFlag(args, "--connection"));
        auto adapter = tc::createTradingAdapter(connection);
        adapter->connectAdapter(connection.toConnectionConfig());
        auto* chart = new tc::ChartWindow(connection, adapter.get(), symbol, exchange);
        chart->setAttribute(Qt::WA_DeleteOnClose, true);
        QObject::connect(chart, &QObject::destroyed, &app, [] { QApplication::quit(); });
        chart->show();
        chart->raise();
        chart->activateWindow();
        return QApplication::exec();
    }

    tc::MainWindow window;
    window.show();
    return QApplication::exec();
}
