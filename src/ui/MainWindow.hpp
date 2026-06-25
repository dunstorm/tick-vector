#pragma once

#include "core/FeedConnection.hpp"
#include "core/FeedConnectionStore.hpp"
#include "core/MarketDataAdapter.hpp"

#include <QtCore/QPoint>
#include <QtCore/QVector>
#include <QtWidgets/QMainWindow>
#include <memory>

class QAction;
class QComboBox;
class QEvent;
class QFrame;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;

namespace tc {

class ChartWindow;
class DomWindow;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr, bool loadSavedConnections = true);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    QWidget* createToolbar();
    QPushButton* createWindowControl(const QString& objectName);
    QToolButton* createMenuButton(const QString& text, QMenu* menu = nullptr);
    QPushButton* createTextButton(const QString& text);
    void loadConnections();
    void rebuildConnectionSelector();
    void selectConnectionById(const QString& id);
    void beginConnection(const QString& id);
    void disconnectActiveConnection();
    void setConnectionState(const QString& state);
    bool selectedConnectionReady() const;
    void showFeedSettings();
    void openPriceChart();
    void openDom();
    void applyStyle();

    FeedConnectionStore connectionStore_;
    QVector<FeedConnection> connections_;
    std::unique_ptr<ITradingAdapter> connectionAdapter_;
    QString selectedConnectionId_;
    QString activeConnectionId_;
    QString activeConnectionFingerprint_;
    QString connectionState_{"idle"};
    QString connectionStatusMessage_;
    int connectionAttempt_{0};

    QAction* priceChartAction_{nullptr};
    QAction* domAction_{nullptr};
    QFrame* toolbar_{nullptr};
    QFrame* statusPill_{nullptr};
    QFrame* statusDot_{nullptr};
    QComboBox* connectionSelector_{nullptr};
    QLabel* statusLabel_{nullptr};
    QVector<ChartWindow*> chartWindows_;
    QVector<DomWindow*> domWindows_;
    QPoint dragOffset_;
    bool dragging_{false};
};

} // namespace tc
