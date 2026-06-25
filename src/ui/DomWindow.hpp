#pragma once

#include "core/FeedConnection.hpp"
#include "core/MarketDataAdapter.hpp"

#include <QtCore/QPoint>
#include <QtCore/QTimer>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>

class QEvent;
class QFrame;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QWheelEvent;

namespace tc {

class DomLadderWidget final : public QWidget {
public:
    explicit DomLadderWidget(QWidget* parent = nullptr);

    void setSnapshot(MarketSnapshot snapshot);
    void recenter();

private:
    QSize minimumSizeHint() const override;
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

    MarketSnapshot snapshot_;
    int manualCenterOffset_{0};
};

class DomWindow final : public QMainWindow {
public:
    explicit DomWindow(FeedConnection connection, ITradingAdapter* sharedAdapter, QString symbol = "MGC", QString exchange = "COMEX", QWidget* parent = nullptr);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    QWidget* createChrome();
    QWidget* createWorkspace();
    QPushButton* createWindowControl(const QString& objectName);
    QPushButton* createActionButton(const QString& text, const QString& objectName);
    void connectMarketData();
    void queueSnapshot(const MarketSnapshot& snapshot);
    void flushPendingSnapshot();
    void renderSnapshot(const MarketSnapshot& snapshot);
    void applyLocalStyle();

    FeedConnection connection_;
    QString symbol_;
    QString exchange_;
    ITradingAdapter* adapter_{nullptr};
    MarketSnapshot snapshot_;
    MarketSnapshot pendingSnapshot_;
    QTimer renderTimer_;
    QFrame* chrome_{nullptr};
    QLabel* title_{nullptr};
    QLabel* feedState_{nullptr};
    DomLadderWidget* ladder_{nullptr};
    QPoint dragOffset_;
    bool hasPendingSnapshot_{false};
    bool dragging_{false};
};

} // namespace tc
