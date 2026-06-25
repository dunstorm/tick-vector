#include "ui/ChartWindow.hpp"

#include <QtCharts/QCandlestickSeries>
#include <QtCharts/QCandlestickSet>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCore/QMetaObject>
#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtCore/QTimeZone>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace tc {

namespace {

QToolButton* makeIconButton(const QString& text, const QString& tooltip, QWidget* parent, const QString& iconPath = {})
{
    auto* button = new QToolButton(parent);
    button->setObjectName("chartIconButton");
    button->setText(text);
    button->setToolTip(tooltip);
    button->setFixedSize(30, 30);
    button->setCursor(Qt::PointingHandCursor);
    if (!iconPath.isEmpty()) {
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    return button;
}

class InteractiveChartView final : public QChartView {
public:
    explicit InteractiveChartView(QChart* chart, QWidget* parent = nullptr)
        : QChartView(chart, parent)
    {
        setMouseTracking(true);
    }

    std::function<void(QWheelEvent*)> wheelHandler;
    std::function<void(QMouseEvent*)> pressHandler;
    std::function<void(QMouseEvent*)> moveHandler;
    std::function<void(QMouseEvent*)> releaseHandler;
    std::function<void(QMouseEvent*)> doubleClickHandler;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (wheelHandler) {
            wheelHandler(event);
            return;
        }
        QChartView::wheelEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (pressHandler) {
            pressHandler(event);
            return;
        }
        QChartView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (moveHandler) {
            moveHandler(event);
            return;
        }
        QChartView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (releaseHandler) {
            releaseHandler(event);
            return;
        }
        QChartView::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (doubleClickHandler) {
            doubleClickHandler(event);
            return;
        }
        QChartView::mouseDoubleClickEvent(event);
    }
};

class IndicatorDialog final : public QDialog {
public:
    explicit IndicatorDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Indicators");
        setModal(false);
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setFixedSize(360, 280);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* titleBar = new QFrame(this);
        titleBar->setObjectName("indicatorTitleBar");
        titleBar->setFixedHeight(40);
        auto* titleLayout = new QHBoxLayout(titleBar);
        titleLayout->setContentsMargins(14, 0, 12, 0);
        titleLayout->setSpacing(8);
        auto* title = new QLabel("Indicators", titleBar);
        title->setObjectName("indicatorTitle");
        auto* close = new QPushButton(titleBar);
        close->setObjectName("windowClose");
        close->setFixedSize(12, 12);
        connect(close, &QPushButton::clicked, this, &QDialog::close);
        titleLayout->addWidget(close);
        titleLayout->addSpacing(4);
        titleLayout->addWidget(title);
        titleLayout->addStretch();
        root->addWidget(titleBar);

        auto* body = new QFrame(this);
        body->setObjectName("indicatorBody");
        auto* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(14, 14, 14, 14);
        bodyLayout->setSpacing(10);
        auto* search = new QLineEdit(body);
        search->setObjectName("indicatorSearch");
        search->setPlaceholderText("Search indicators");
        search->setFixedHeight(32);
        bodyLayout->addWidget(search);

        for (const auto& name : {"Volume", "VWAP", "Moving Average", "Delta", "Volume Profile"}) {
            auto* row = new QPushButton(name, body);
            row->setObjectName("indicatorRow");
            row->setFixedHeight(32);
            row->setCursor(Qt::PointingHandCursor);
            bodyLayout->addWidget(row);
        }
        bodyLayout->addStretch();
        root->addWidget(body, 1);
    }
};

bool isRenderableCandle(const Candle& candle)
{
    return candle.time.isValid()
        && std::isfinite(candle.open)
        && std::isfinite(candle.high)
        && std::isfinite(candle.low)
        && std::isfinite(candle.close)
        && candle.open > 0.0
        && candle.high > 0.0
        && candle.low > 0.0
        && candle.close > 0.0
        && candle.high >= candle.low;
}

quint64 candleFingerprint(const QVector<Candle>& candles)
{
    quint64 hash = 1469598103934665603ULL;
    const auto mix = [&hash](qint64 value) {
        hash ^= static_cast<quint64>(value);
        hash *= 1099511628211ULL;
    };
    const auto scaled = [](double value) {
        return static_cast<qint64>(std::llround(value * 1000000.0));
    };

    mix(candles.size());
    for (const auto& candle : candles) {
        mix(candle.time.toMSecsSinceEpoch());
        mix(scaled(candle.open));
        mix(scaled(candle.high));
        mix(scaled(candle.low));
        mix(scaled(candle.close));
        mix(scaled(candle.volume));
    }
    return hash;
}

} // namespace

ChartWindow::ChartWindow(FeedConnection connection, ITradingAdapter* sharedAdapter, QString symbol, QString exchange, QWidget* parent)
    : QMainWindow(parent)
    , connection_(std::move(connection))
    , symbol_(std::move(symbol))
    , exchange_(std::move(exchange))
    , adapter_(sharedAdapter)
{
    setWindowTitle("Price Chart");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    resize(1120, 680);
    renderTimer_.setSingleShot(true);
    renderTimer_.setInterval(80);
    QObject::connect(&renderTimer_, &QTimer::timeout, this, [this] { flushPendingSnapshot(); });

    auto* root = new QWidget(this);
    root->setObjectName("chartRoot");
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createChrome());

    auto* workspace = new QFrame(root);
    workspace->setObjectName("chartWorkspace");
    auto* workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);
    workspaceLayout->addWidget(createToolRail());
    workspaceLayout->addWidget(createCandlestickChart(), 1);
    layout->addWidget(workspace, 1);

    setCentralWidget(root);
    applyLocalStyle();
    connectMarketData();
}

QWidget* ChartWindow::createChrome()
{
    chrome_ = new QFrame(this);
    chrome_->setObjectName("chartChrome");
    chrome_->setFixedHeight(44);
    chrome_->installEventFilter(this);

    auto* layout = new QHBoxLayout(chrome_);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    auto* close = createWindowControl("windowClose");
    auto* minimize = createWindowControl("windowMinimize");
    layout->addWidget(close);
    layout->addWidget(minimize);

    QObject::connect(close, &QPushButton::clicked, this, &QWidget::close);
    QObject::connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);

    title_ = new QLabel(connection_.name.isEmpty() ? exchange_ + " / " + symbol_ : connection_.name + " / " + exchange_ + " / " + symbol_, chrome_);
    title_->setObjectName("chartTitle");
    title_->installEventFilter(this);
    layout->addWidget(title_);

    auto* indicatorButton = makeIconButton({}, "Indicators", chrome_, ":/icons/indicator.svg");
    connect(indicatorButton, &QToolButton::clicked, this, [this] { showIndicators(); });
    layout->addWidget(indicatorButton);

    layout->addStretch();

    auto* symbol = new QComboBox(chrome_);
    symbol->setObjectName("chartCombo");
    symbol->addItem(exchange_ + " / " + symbol_);
    symbol->addItems({"CME / MNQ", "CME / NQ", "COMEX / MGC", "COMEX / GC", "NYMEX / CL"});
    symbol->setFixedWidth(128);
    layout->addWidget(symbol);

    auto* timeframe = new QComboBox(chrome_);
    timeframe->setObjectName("chartCombo");
    timeframe->addItems({"2m", "5m", "15m"});
    timeframe->setFixedWidth(72);
    layout->addWidget(timeframe);

    return chrome_;
}

QWidget* ChartWindow::createToolRail()
{
    auto* rail = new QFrame(this);
    rail->setObjectName("chartToolRail");
    rail->setFixedWidth(44);
    auto* layout = new QVBoxLayout(rail);
    layout->setContentsMargins(7, 8, 7, 8);
    layout->setSpacing(8);
    layout->addWidget(makeIconButton({}, "Arrow", rail, ":/icons/tool-arrow.svg"));
    layout->addWidget(makeIconButton({}, "Rectangle", rail, ":/icons/tool-rectangle.svg"));
    layout->addStretch();
    return rail;
}

QWidget* ChartWindow::createCandlestickChart()
{
    auto* panel = new QFrame(this);
    panel->setObjectName("chartPanel");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    feedState_ = new QLabel("Waiting for live Rithmic feed...", panel);
    feedState_->setObjectName("feedStateLabel");
    feedState_->setFixedHeight(26);

    buildProgress_ = new QProgressBar(panel);
    buildProgress_->setObjectName("buildProgress");
    buildProgress_->setFixedHeight(6);
    buildProgress_->setTextVisible(false);
    buildProgress_->setRange(0, 100);
    buildProgress_->hide();

    candleSeries_ = new QCandlestickSeries;
    candleSeries_->setName(symbol_);
    candleSeries_->setIncreasingColor(QColor("#65d36e"));
    candleSeries_->setDecreasingColor(QColor("#ff5d57"));
    candleSeries_->setBodyWidth(0.72);

    chart_ = new QChart;
    chart_->setBackgroundVisible(false);
    chart_->setPlotAreaBackgroundVisible(true);
    chart_->setPlotAreaBackgroundBrush(QColor("#080b10"));
    chart_->legend()->hide();
    chart_->addSeries(candleSeries_);
    chart_->setMargins(QMargins(8, 8, 8, 8));

    axisX_ = new QDateTimeAxis;
    axisX_->setFormat("HH:mm");
    axisX_->setTickCount(8);
    axisX_->setLabelsColor(QColor("#9aa4b3"));
    axisX_->setGridLineColor(QColor(255, 255, 255, 18));
    axisX_->setLinePenColor(QColor("#242b36"));
    chart_->addAxis(axisX_, Qt::AlignBottom);
    candleSeries_->attachAxis(axisX_);

    axisY_ = new QValueAxis;
    axisY_->setLabelFormat("%.2f");
    axisY_->setTickCount(8);
    axisY_->setRange(0.0, 1.0);
    axisY_->setLabelsColor(QColor("#9aa4b3"));
    axisY_->setGridLineColor(QColor(255, 255, 255, 18));
    axisY_->setLinePenColor(QColor("#242b36"));
    chart_->addAxis(axisY_, Qt::AlignRight);
    candleSeries_->attachAxis(axisY_);

    auto* view = new InteractiveChartView(chart_, panel);
    chartView_ = view;
    view->setObjectName("candlestickChartView");
    view->setRenderHint(QPainter::Antialiasing, true);
    view->setRubberBand(QChartView::NoRubberBand);
    view->setCursor(Qt::CrossCursor);
    view->wheelHandler = [this](QWheelEvent* event) { handleChartWheel(event); };
    view->pressHandler = [this](QMouseEvent* event) { handleChartMousePress(event); };
    view->moveHandler = [this](QMouseEvent* event) { handleChartMouseMove(event); };
    view->releaseHandler = [this](QMouseEvent* event) { handleChartMouseRelease(event); };
    view->doubleClickHandler = [this](QMouseEvent* event) { handleChartDoubleClick(event); };
    layout->addWidget(feedState_);
    layout->addWidget(buildProgress_);
    layout->addWidget(view, 1);
    return panel;
}

QPushButton* ChartWindow::createWindowControl(const QString& objectName)
{
    auto* button = new QPushButton(this);
    button->setObjectName(objectName);
    button->setFixedSize(12, 12);
    button->setCursor(Qt::ArrowCursor);
    return button;
}

void ChartWindow::showIndicators()
{
    auto* dialog = new IndicatorDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
    dialog->raise();
}

void ChartWindow::connectMarketData()
{
    if (!adapter_) {
        feedState_->setText("No active feed session.");
        feedState_->show();
        return;
    }

    adapter_->addSnapshotHandler(this, [this](const MarketSnapshot& snapshot) {
        if (QThread::currentThread() == thread()) {
            queueSnapshot(snapshot);
        } else {
            QMetaObject::invokeMethod(this, [this, snapshot] { queueSnapshot(snapshot); }, Qt::QueuedConnection);
        }
    });

    feedState_->setText("Using connected " + adapter_->name() + " session...");
    adapter_->subscribe(exchange_ + ":" + symbol_);
    queueSnapshot(adapter_->snapshot());
}

void ChartWindow::queueSnapshot(const MarketSnapshot& snapshot)
{
    pendingSnapshot_ = snapshot;
    hasPendingSnapshot_ = true;
    if (!renderTimer_.isActive()) {
        renderTimer_.start(candles_.isEmpty() ? 0 : renderTimer_.interval());
    }
}

void ChartWindow::flushPendingSnapshot()
{
    if (!hasPendingSnapshot_) {
        return;
    }

    const MarketSnapshot snapshot = pendingSnapshot_;
    hasPendingSnapshot_ = false;
    renderSnapshot(snapshot);
    if (hasPendingSnapshot_ && !renderTimer_.isActive()) {
        renderTimer_.start(renderTimer_.interval());
    }
}

void ChartWindow::renderSnapshot(const MarketSnapshot& snapshot)
{
    if (!candleSeries_ || !axisX_ || !axisY_ || !feedState_ || !buildProgress_) {
        return;
    }

    const bool hadCandles = !candles_.isEmpty();
    const qint64 previousEnd = hadCandles ? candles_.last().time.toMSecsSinceEpoch() : 0;
    candles_ = snapshot.candles;
    candles_.erase(std::remove_if(candles_.begin(), candles_.end(), [](const Candle& candle) {
        return !isRenderableCandle(candle);
    }), candles_.end());
    std::sort(candles_.begin(), candles_.end(), [](const Candle& left, const Candle& right) {
        return left.time < right.time;
    });
    const quint64 fingerprint = candleFingerprint(candles_);
    const bool candleDataChanged = fingerprint != renderedCandleFingerprint_;
    if (title_ && !snapshot.symbol.trimmed().isEmpty()) {
        title_->setText((connection_.name.isEmpty() ? QString{} : connection_.name + " / ") + snapshot.symbol);
    }

    if (snapshot.buildingChartData) {
        const int expected = std::max(snapshot.chartBarsExpected, 1);
        const int loaded = std::clamp(snapshot.chartBarsLoaded, 0, expected);
        feedState_->setText("Building chart data " + QString::number(loaded) + " / " + QString::number(expected) + " bars");
        feedState_->show();
        buildProgress_->setRange(0, expected);
        buildProgress_->setValue(loaded);
        buildProgress_->show();
    } else {
        buildProgress_->hide();
    }

    if (!snapshot.connected && candles_.isEmpty()) {
        feedState_->setText(snapshot.connectionLabel.trimmed().isEmpty() ? "Waiting for live feed..." : snapshot.connectionLabel);
        feedState_->show();
        if (candleDataChanged) {
            renderCandles();
            renderedCandleFingerprint_ = fingerprint;
        }
        return;
    }

    if (candles_.isEmpty()) {
        if (snapshot.connected && !snapshot.connectionLabel.trimmed().isEmpty() && snapshot.connectionLabel != "Live Rithmic feed" && snapshot.connectionLabel != "Rithmic Protocol connected.") {
            feedState_->setText(snapshot.connectionLabel);
        } else {
            feedState_->setText(snapshot.connected ? "Connected. Waiting for live candles for " + snapshot.symbol + "..." : snapshot.connectionLabel);
        }
        feedState_->show();
        if (candleDataChanged) {
            renderCandles();
            renderedCandleFingerprint_ = fingerprint;
        }
        return;
    }

    const bool appendedNewRightEdge = !hadCandles || candles_.last().time.toMSecsSinceEpoch() > previousEnd;
    wasPinnedRight_ = !userTimeRange_ || previousEnd <= 0 || std::llabs(visibleEndMs_ - previousEnd) < 180000;
    if (candleDataChanged) {
        renderCandles();
        renderedCandleFingerprint_ = fingerprint;
        updateVisibleRanges(userTimeRange_ && !(wasPinnedRight_ && appendedNewRightEdge));
    }

    if (!snapshot.buildingChartData) {
        feedState_->hide();
    }
}

void ChartWindow::renderCandles()
{
    if (!candleSeries_ || !axisX_ || !axisY_) {
        return;
    }

    candleSeries_->clear();
    for (const auto& candle : candles_) {
        if (!isRenderableCandle(candle)) {
            continue;
        }
        candleSeries_->append(new QCandlestickSet(candle.open, candle.high, candle.low, candle.close, static_cast<qreal>(candle.time.toMSecsSinceEpoch())));
    }
}

void ChartWindow::updateVisibleRanges(bool preserveUserView)
{
    if (candles_.isEmpty()) {
        setTimeRange(QDateTime::currentDateTimeUtc().addSecs(-3600).toMSecsSinceEpoch(), QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
        setPriceRange(0.0, 1.0);
        return;
    }

    const qint64 firstMs = candles_.first().time.toMSecsSinceEpoch();
    const qint64 lastMs = candles_.last().time.toMSecsSinceEpoch();
    if (!preserveUserView || visibleStartMs_ >= visibleEndMs_) {
        const int visibleBars = std::min(static_cast<int>(candles_.size()), 120);
        const qint64 startMs = candles_.at(candles_.size() - visibleBars).time.toMSecsSinceEpoch();
        setTimeRange(startMs, lastMs + 120000);
        userTimeRange_ = false;
    } else {
        const qint64 range = std::max<qint64>(visibleEndMs_ - visibleStartMs_, 120000);
        if (wasPinnedRight_) {
            setTimeRange(lastMs + 120000 - range, lastMs + 120000);
        } else {
            setTimeRange(std::max(firstMs, visibleStartMs_), std::min(lastMs + 120000, visibleEndMs_));
        }
    }

    if (!userPriceRange_) {
        autoscalePriceForVisibleRange();
    }
}

void ChartWindow::autoscalePriceForVisibleRange()
{
    if (candles_.isEmpty()) {
        setPriceRange(0.0, 1.0);
        return;
    }

    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    for (const auto& candle : candles_) {
        const qint64 t = candle.time.toMSecsSinceEpoch();
        if (t < visibleStartMs_ || t > visibleEndMs_) {
            continue;
        }
        low = std::min(low, candle.low);
        high = std::max(high, candle.high);
    }

    if (low == std::numeric_limits<double>::max() || high == std::numeric_limits<double>::lowest()) {
        for (const auto& candle : candles_) {
            low = std::min(low, candle.low);
            high = std::max(high, candle.high);
        }
    }

    const double padding = std::max((high - low) * 0.10, 0.01);
    setPriceRange(low - padding, high + padding);
}

void ChartWindow::setTimeRange(qint64 startMs, qint64 endMs)
{
    if (!axisX_) {
        return;
    }
    if (endMs <= startMs) {
        endMs = startMs + 120000;
    }
    visibleStartMs_ = startMs;
    visibleEndMs_ = endMs;
    axisX_->setRange(QDateTime::fromMSecsSinceEpoch(visibleStartMs_, QTimeZone::UTC), QDateTime::fromMSecsSinceEpoch(visibleEndMs_, QTimeZone::UTC));
}

void ChartWindow::setPriceRange(double minPrice, double maxPrice)
{
    if (!axisY_) {
        return;
    }
    if (maxPrice <= minPrice) {
        maxPrice = minPrice + 1.0;
    }
    visiblePriceMin_ = minPrice;
    visiblePriceMax_ = maxPrice;
    axisY_->setRange(minPrice, maxPrice);
}

void ChartWindow::handleChartWheel(QWheelEvent* event)
{
    if (!chartView_ || candles_.isEmpty()) {
        event->accept();
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->accept();
        return;
    }

    if (isOverPriceScale(event->position())) {
        const double center = (visiblePriceMin_ + visiblePriceMax_) * 0.5;
        const double currentRange = std::max(visiblePriceMax_ - visiblePriceMin_, 0.01);
        const double factor = delta > 0 ? 0.86 : 1.16;
        const double nextRange = std::max(currentRange * factor, 0.01);
        userPriceRange_ = true;
        setPriceRange(center - nextRange * 0.5, center + nextRange * 0.5);
        event->accept();
        return;
    }

    const qint64 currentRange = std::max<qint64>(visibleEndMs_ - visibleStartMs_, 120000);
    const double factor = delta > 0 ? 0.78 : 1.28;
    const qint64 nextRange = std::clamp<qint64>(static_cast<qint64>(currentRange * factor), 120000, 86400000);
    const QRectF plot = chart_->plotArea();
    const double cursorRatio = plot.width() > 0 ? std::clamp((event->position().x() - plot.left()) / plot.width(), 0.0, 1.0) : 0.5;
    const qint64 anchor = visibleStartMs_ + static_cast<qint64>(currentRange * cursorRatio);
    qint64 start = anchor - static_cast<qint64>(nextRange * cursorRatio);
    qint64 end = start + nextRange;

    const qint64 firstMs = candles_.first().time.toMSecsSinceEpoch();
    const qint64 lastMs = candles_.last().time.toMSecsSinceEpoch() + 120000;
    if (start < firstMs) {
        end += firstMs - start;
        start = firstMs;
    }
    if (end > lastMs) {
        start -= end - lastMs;
        end = lastMs;
    }
    userTimeRange_ = true;
    setTimeRange(start, end);
    if (!userPriceRange_) {
        autoscalePriceForVisibleRange();
    }
    event->accept();
}

void ChartWindow::handleChartMousePress(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    chartDragStart_ = event->pos();
    dragStartMinMs_ = visibleStartMs_;
    dragStartMaxMs_ = visibleEndMs_;
    dragStartPriceMin_ = visiblePriceMin_;
    dragStartPriceMax_ = visiblePriceMax_;
    draggingPriceScale_ = isOverPriceScale(event->position());
    draggingChart_ = !draggingPriceScale_;
    event->accept();
}

void ChartWindow::handleChartMouseMove(QMouseEvent* event)
{
    if (!chart_ || candles_.isEmpty() || (!draggingChart_ && !draggingPriceScale_)) {
        event->accept();
        return;
    }

    const QPoint delta = event->pos() - chartDragStart_;
    const QRectF plot = chart_->plotArea();
    if (draggingPriceScale_) {
        const double currentRange = std::max(dragStartPriceMax_ - dragStartPriceMin_, 0.01);
        const double factor = std::clamp(std::exp(delta.y() * 0.006), 0.15, 8.0);
        const double center = (dragStartPriceMin_ + dragStartPriceMax_) * 0.5;
        const double nextRange = std::max(currentRange * factor, 0.01);
        userPriceRange_ = true;
        setPriceRange(center - nextRange * 0.5, center + nextRange * 0.5);
        event->accept();
        return;
    }

    const qint64 timeRange = std::max<qint64>(dragStartMaxMs_ - dragStartMinMs_, 120000);
    const double priceRange = std::max(dragStartPriceMax_ - dragStartPriceMin_, 0.01);
    const qint64 shiftMs = plot.width() > 0 ? static_cast<qint64>(-delta.x() * timeRange / plot.width()) : 0;
    const double priceShift = plot.height() > 0 ? delta.y() * priceRange / plot.height() : 0.0;
    userTimeRange_ = true;
    userPriceRange_ = true;
    setTimeRange(dragStartMinMs_ + shiftMs, dragStartMaxMs_ + shiftMs);
    setPriceRange(dragStartPriceMin_ + priceShift, dragStartPriceMax_ + priceShift);
    event->accept();
}

void ChartWindow::handleChartMouseRelease(QMouseEvent* event)
{
    draggingChart_ = false;
    draggingPriceScale_ = false;
    event->accept();
}

void ChartWindow::handleChartDoubleClick(QMouseEvent* event)
{
    userTimeRange_ = false;
    userPriceRange_ = false;
    updateVisibleRanges(false);
    event->accept();
}

bool ChartWindow::isOverPriceScale(const QPointF& pos) const
{
    if (!chart_) {
        return false;
    }
    const QRectF plot = chart_->plotArea();
    return pos.x() >= plot.right() && pos.x() <= plot.right() + 80;
}

bool ChartWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == chrome_ || watched == title_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                dragging_ = true;
                dragOffset_ = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove && dragging_) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            move(mouse->globalPosition().toPoint() - dragOffset_);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            dragging_ = false;
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void ChartWindow::applyLocalStyle()
{
    setStyleSheet(R"(
        QWidget#chartRoot {
            background: #06080c;
            color: #e6e8ec;
            font-family: "Helvetica Neue";
            font-size: 13px;
        }
        QFrame#chartChrome {
            background: #11151d;
            border: 0;
            border-bottom: 1px solid #252d3a;
        }
        QFrame#chartWorkspace {
            background: #07090d;
            border: 0;
        }
        QFrame#chartToolRail {
            background: #0d1118;
            border: 0;
            border-right: 1px solid #242b36;
        }
        QFrame#chartPanel {
            background: #07090d;
            border: 0;
        }
        QLabel#feedStateLabel {
            background: #121821;
            border: 1px solid #263140;
            border-radius: 5px;
            color: #ffcf5a;
            padding: 0 10px;
            font-size: 12px;
            font-weight: 900;
        }
        QProgressBar#buildProgress {
            background: #10151d;
            border: 0;
            border-radius: 3px;
        }
        QProgressBar#buildProgress::chunk {
            background: #ffcf5a;
            border-radius: 3px;
        }
        QChartView#candlestickChartView {
            background: #07090d;
            border: 0;
        }
        QLabel#chartTitle {
            color: #f0f2f4;
            font-size: 13px;
            font-weight: 900;
            padding-left: 8px;
        }
        QPushButton#windowClose,
        QPushButton#windowMinimize {
            border: 0;
            border-radius: 6px;
        }
        QPushButton#windowClose {
            background: #ff5f57;
        }
        QPushButton#windowMinimize {
            background: #ffbd2e;
        }
        QToolButton#chartIconButton {
            background: #151b25;
            border: 1px solid #303b4d;
            border-radius: 5px;
            color: #dfe5ee;
            font-size: 12px;
            font-weight: 900;
        }
        QToolButton#chartIconButton:hover {
            background: #1c2430;
            border-color: #465366;
        }
        QToolButton#chartIconButton:pressed {
            background: #111720;
            border-color: #56657a;
        }
        QComboBox#chartCombo {
            background: #171c25;
            border: 1px solid #2e3746;
            border-radius: 5px;
            color: #f0f2f4;
            min-height: 28px;
            padding: 0 8px;
            font-weight: 800;
        }
        QComboBox::drop-down {
            border: 0;
            width: 18px;
        }
        QDialog {
            background: #0f1218;
            color: #e8ecf3;
            font-family: "Helvetica Neue";
            font-size: 12px;
        }
        QFrame#indicatorTitleBar {
            background: #121721;
            border: 0;
            border-bottom: 1px solid #252d3a;
        }
        QLabel#indicatorTitle {
            color: #eef2f7;
            font-size: 13px;
            font-weight: 900;
        }
        QFrame#indicatorBody {
            background: #0f1218;
            border: 0;
        }
        QLineEdit#indicatorSearch {
            background: #171c25;
            border: 1px solid #2e3746;
            border-radius: 6px;
            color: #eef2f7;
            padding: 0 10px;
            font-weight: 800;
        }
        QPushButton#indicatorRow {
            background: #121821;
            border: 1px solid #263140;
            border-radius: 5px;
            color: #dce4ef;
            text-align: left;
            padding-left: 10px;
            font-weight: 800;
        }
        QPushButton#indicatorRow:hover {
            background: #18202b;
            border-color: #334052;
        }
    )");
}

} // namespace tc
