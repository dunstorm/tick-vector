#pragma once

#include "core/FeedConnection.hpp"
#include "core/MarketDataAdapter.hpp"

#include <QtCore/QPoint>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtWidgets/QMainWindow>

class QEvent;
class QFrame;
class QLabel;
class QMouseEvent;
class QProgressBar;
class QPushButton;
class QWheelEvent;
class QDateTimeAxis;
class QCandlestickSeries;
class QValueAxis;
class QChart;
class QChartView;

namespace tc {

class ChartWindow final : public QMainWindow {
public:
    explicit ChartWindow(FeedConnection connection, ITradingAdapter* sharedAdapter, QString symbol = "NQ", QString exchange = "CME", QWidget* parent = nullptr);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    QWidget* createChrome();
    QWidget* createToolRail();
    QWidget* createCandlestickChart();
    QPushButton* createWindowControl(const QString& objectName);
    void showIndicators();
    void connectMarketData();
    void queueSnapshot(const MarketSnapshot& snapshot);
    void flushPendingSnapshot();
    void renderSnapshot(const MarketSnapshot& snapshot);
    void renderCandles();
    void updateVisibleRanges(bool preserveUserView);
    void autoscalePriceForVisibleRange();
    void setTimeRange(qint64 startMs, qint64 endMs);
    void setPriceRange(double minPrice, double maxPrice);
    void handleChartWheel(QWheelEvent* event);
    void handleChartMousePress(QMouseEvent* event);
    void handleChartMouseMove(QMouseEvent* event);
    void handleChartMouseRelease(QMouseEvent* event);
    void handleChartDoubleClick(QMouseEvent* event);
    bool isOverPriceScale(const QPointF& pos) const;
    void applyLocalStyle();

    FeedConnection connection_;
    QString symbol_;
    QString exchange_;
    ITradingAdapter* adapter_{nullptr};
    QVector<Candle> candles_;
    MarketSnapshot pendingSnapshot_;
    QTimer renderTimer_;
    QChart* chart_{nullptr};
    QChartView* chartView_{nullptr};
    QCandlestickSeries* candleSeries_{nullptr};
    QDateTimeAxis* axisX_{nullptr};
    QValueAxis* axisY_{nullptr};
    QFrame* chrome_{nullptr};
    QLabel* title_{nullptr};
    QLabel* feedState_{nullptr};
    QProgressBar* buildProgress_{nullptr};
    QPoint dragOffset_;
    QPoint chartDragStart_;
    qint64 dragStartMinMs_{0};
    qint64 dragStartMaxMs_{0};
    double dragStartPriceMin_{0.0};
    double dragStartPriceMax_{1.0};
    qint64 visibleStartMs_{0};
    qint64 visibleEndMs_{0};
    double visiblePriceMin_{0.0};
    double visiblePriceMax_{1.0};
    quint64 renderedCandleFingerprint_{0};
    bool hasPendingSnapshot_{false};
    bool dragging_{false};
    bool draggingChart_{false};
    bool draggingPriceScale_{false};
    bool userTimeRange_{false};
    bool userPriceRange_{false};
    bool wasPinnedRight_{true};
};

} // namespace tc
