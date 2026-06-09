#include "ui/MainWindow.hpp"
#include "ui/ChartWindow.hpp"
#include "ui/ConnectionTestDialog.hpp"
#include "ui/FeedSettingsDialog.hpp"
#include "ui/SelectInstrumentDialog.hpp"

#include <QtGui/QFont>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Trading Client");
    QApplication::setOrganizationName("Trading Client");
    QApplication::setFont(QFont("Helvetica Neue", 13));

    const QStringList args = QApplication::arguments();
    const int chartScreenshotIndex = args.indexOf("--screenshot-chart");
    if (chartScreenshotIndex >= 0 && chartScreenshotIndex + 1 < args.size()) {
        tc::MainWindow styleHost(nullptr, false);
        tc::FeedConnection connection;
        connection.name = "Rithmic";
        tc::ChartWindow chart(connection);
        chart.show();
        app.processEvents();
        const QPixmap capture = chart.grab();
        return capture.save(args.at(chartScreenshotIndex + 1)) ? 0 : 1;
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
        connection.username = "LT-4VLX9C08";
        connection.password = "password";
        connection.gateway = "Chicago";
        connection.system = "Lucid Trading";
        tc::ConnectionTestDialog dialog(connection);
        dialog.showSuccess();
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

    tc::MainWindow window;
    window.show();
    return QApplication::exec();
}
