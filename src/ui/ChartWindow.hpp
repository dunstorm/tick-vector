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
class QToolButton;
class QWheelEvent;
class QDateTimeAxis;
class QCandlestickSeries;
class QScatterSeries;
class QLineSeries;
class QValueAxis;
class QChart;
class QChartView;
class QComboBox;
class QGraphicsItemGroup;

namespace tc {

class ChartWindow final : public QMainWindow {
public:
    explicit ChartWindow(FeedConnection connection, ITradingAdapter* sharedAdapter, QString symbol = "NQ", QString exchange = "CME", QWidget* parent = nullptr);

private:
    enum class ChartTool {
        Pointer,
        Rectangle,
        VolumeProfile
    };

    bool eventFilter(QObject* watched, QEvent* event) override;
    QWidget* createChrome();
    QWidget* createToolRail();
    QWidget* createCandlestickChart();
    QPushButton* createWindowControl(const QString& objectName);
    void showIndicators();
    void showChartSettings();
    void connectMarketData();
    void requestChartData(bool allowCached);
    void handleTimeframeChanged(int index);
    void queueSnapshot(const MarketSnapshot& snapshot);
    void flushPendingSnapshot();
    void renderSnapshot(const MarketSnapshot& snapshot);
    void showLoadingState(const MarketSnapshot& snapshot);
    void showStatusMessage(const QString& message);
    void hideLoadingState();
    void updateLoadingSpinner();
    void renderCandles();
    void renderIndicators();
    void renderVwap();
    void renderBigTrades();
    void renderAnchoredVolumeProfile();
    void clearVolumeProfileOverlay();
    void updateVisibleRanges(bool preserveUserView);
    void autoscalePriceForVisibleRange();
    void setTimeRange(qint64 startMs, qint64 endMs);
    void setPriceRange(double minPrice, double maxPrice);
    void panTimeRange(qint64 shiftMs);
    void zoomTimeRangeAt(double cursorRatio, double factor);
    void zoomPriceRangeAt(double cursorRatio, double factor);
    void updateCurrentPriceLine();
    void updateCurrentPriceLabel();
    qint64 barDurationMs() const;
    qint64 rightOffsetMs() const;
    qint64 minVisibleRangeMs() const;
    qint64 maxVisibleRangeMs() const;
    qint64 rightEdgeForLastCandle(qint64 lastMs) const;
    void updateAxisTitles();
    void updateHeaderTitles(const QString& symbol);
    void applyChartVisualSettings();
    void setActiveTool(ChartTool tool);
    void toggleIndicator(const QString& name);
    void handleChartWheel(QWheelEvent* event);
    void handleChartMousePress(QMouseEvent* event);
    void handleChartMouseMove(QMouseEvent* event);
    void handleChartMouseRelease(QMouseEvent* event);
    void handleChartDoubleClick(QMouseEvent* event);
    bool isOverPriceScale(const QPointF& pos) const;
    bool isInsidePlot(const QPointF& pos) const;
    qint64 timeAtPosition(const QPointF& pos) const;
    double priceAtPosition(const QPointF& pos) const;
    double timeToPlotX(qint64 timeMs) const;
    double priceToPlotY(double price) const;
    int nearestCandleIndexAt(const QPointF& pos) const;
    double magneticPriceAt(int candleIndex, const QPointF& pos) const;
    void beginVolumeProfileAnchor(const QPointF& pos);
    void updateVolumeProfileAnchor(const QPointF& pos);
    void finishVolumeProfileAnchor();
    void toggleMaximized();
    void toggleFullScreen();
    void applyLocalStyle();

    FeedConnection connection_;
    QString symbol_;
    QString exchange_;
    ITradingAdapter* adapter_{nullptr};
    QVector<Candle> candles_;
    QVector<BigTrade> bigTrades_;
    MarketSnapshot pendingSnapshot_;
    QTimer renderTimer_;
    QChart* chart_{nullptr};
    QChartView* chartView_{nullptr};
    QCandlestickSeries* candleSeries_{nullptr};
    QLineSeries* vwapSeries_{nullptr};
    QScatterSeries* bigTradeBuySeries_{nullptr};
    QScatterSeries* bigTradeSellSeries_{nullptr};
    QLineSeries* currentPriceLine_{nullptr};
    QDateTimeAxis* axisX_{nullptr};
    QValueAxis* axisY_{nullptr};
    QFrame* chrome_{nullptr};
    QFrame* loadingPanel_{nullptr};
    QLabel* title_{nullptr};
    QLabel* rightTitle_{nullptr};
    QComboBox* timeframe_{nullptr};
    QLabel* feedState_{nullptr};
    QLabel* loadingSpinner_{nullptr};
    QLabel* loadingDetail_{nullptr};
    QLabel* currentPriceLabel_{nullptr};
    QProgressBar* buildProgress_{nullptr};
    QToolButton* pointerToolButton_{nullptr};
    QToolButton* rectangleToolButton_{nullptr};
    QToolButton* volumeProfileToolButton_{nullptr};
    QGraphicsItemGroup* volumeProfileOverlay_{nullptr};
    QTimer loadingSpinnerTimer_;
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
    double currentPrice_{0.0};
    double profileAnchorStartPrice_{0.0};
    double profileAnchorEndPrice_{0.0};
    quint64 renderedCandleFingerprint_{0};
    int loadingSpinnerFrame_{0};
    int selectedBarMinutes_{1};
    int profileAnchorStartIndex_{-1};
    int profileAnchorEndIndex_{-1};
    QString chartPaletteId_;
    ChartTool activeTool_{ChartTool::Pointer};
    bool hasPendingSnapshot_{false};
    bool dragging_{false};
    bool draggingChart_{false};
    bool draggingPriceScale_{false};
    bool userTimeRange_{false};
    bool userPriceRange_{false};
    bool followRightEdge_{true};
    bool showVwap_{true};
    bool showBigTrades_{true};
    bool drawingVolumeProfile_{false};
};

} // namespace tc
