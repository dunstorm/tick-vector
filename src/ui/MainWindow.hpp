#pragma once

#include "core/FeedConnection.hpp"
#include "core/FeedConnectionStore.hpp"

#include <QtCore/QPoint>
#include <QtCore/QVector>
#include <QtWidgets/QMainWindow>

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
    void setConnectionState(const QString& state);
    bool selectedConnectionReady() const;
    void showFeedSettings();
    void openPriceChart();
    void applyStyle();

    FeedConnectionStore connectionStore_;
    QVector<FeedConnection> connections_;
    QString selectedConnectionId_;
    QString connectionState_{"idle"};
    int connectionAttempt_{0};

    QAction* priceChartAction_{nullptr};
    QFrame* toolbar_{nullptr};
    QFrame* statusPill_{nullptr};
    QFrame* statusDot_{nullptr};
    QComboBox* connectionSelector_{nullptr};
    QLabel* statusLabel_{nullptr};
    QVector<ChartWindow*> chartWindows_;
    QPoint dragOffset_;
    bool dragging_{false};
};

} // namespace tc
