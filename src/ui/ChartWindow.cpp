#include "ui/ChartWindow.hpp"

#include <QtCharts/QCandlestickSeries>
#include <QtCharts/QCandlestickSet>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCore/QMetaObject>
#include <QtCore/QSettings>
#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtCore/QTimeZone>
#include <QtCore/QVariant>
#include <QtGui/QPen>
#include <QtGui/QMouseEvent>
#include <QtGui/QShortcut>
#include <QtGui/QWheelEvent>
#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtWidgets/QGraphicsLayout>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace tc {

namespace {

constexpr int kHistoryLookbackDays = 10;
constexpr int kHistoryBarMinutes = 2;
constexpr int kMaxRenderedCandles = 1400;
constexpr qint64 kDayMs = 24LL * 60 * 60 * 1000;

struct TimeframeOption {
    const char* label;
    int minutes;
    const char* axisFormat;
};

constexpr TimeframeOption kTimeframes[] = {
    {"1m", 1, "HH:mm"},
    {"2m", 2, "HH:mm"},
    {"3m", 3, "HH:mm"},
    {"1h", 60, "MMM d HH:mm"},
    {"4h", 240, "MMM d HH:mm"},
    {"1D", 1440, "MMM d"},
};

struct ChartPalette {
    QString id;
    QString name;
    QColor increasing;
    QColor decreasing;
};

struct ChartVisualSettings {
    QString paletteId{"profile-neon"};
    QColor increasingColor{"#00ff3b"};
    QColor decreasingColor{"#8b25ff"};
};

const QVector<ChartPalette>& chartPalettes()
{
    static const QVector<ChartPalette> palettes{
        {"profile-neon", "Profile Neon", QColor("#00ff3b"), QColor("#8b25ff")},
        {"classic", "Classic", QColor("#6ecf9d"), QColor("#ee7f75")},
        {"blue-gold", "Blue Gold", QColor("#5ea4ff"), QColor("#f0c567")},
    };
    return palettes;
}

ChartPalette chartPaletteById(const QString& id)
{
    for (const auto& palette : chartPalettes()) {
        if (palette.id == id) {
            return palette;
        }
    }
    return chartPalettes().first();
}

QString timeframeLabel(int minutes)
{
    for (const auto& option : kTimeframes) {
        if (option.minutes == minutes) {
            return QString::fromLatin1(option.label);
        }
    }
    if (minutes % 1440 == 0) {
        return QString::number(minutes / 1440) + "D";
    }
    if (minutes % 60 == 0) {
        return QString::number(minutes / 60) + "h";
    }
    return QString::number(minutes) + "m";
}

QString axisFormatForTimeframe(int minutes)
{
    for (const auto& option : kTimeframes) {
        if (option.minutes == minutes) {
            return QString::fromLatin1(option.axisFormat);
        }
    }
    return minutes >= 1440 ? "MMM d" : minutes >= 60 ? "MMM d HH:mm" : "HH:mm";
}

ChartVisualSettings loadChartVisualSettings()
{
    QSettings settings;
    ChartVisualSettings visual;
    visual.paletteId = settings.value("priceChart/palette", visual.paletteId).toString();
    const ChartPalette palette = chartPaletteById(visual.paletteId);
    visual.increasingColor = QColor(settings.value("priceChart/increasingColor", palette.increasing.name(QColor::HexRgb)).toString());
    visual.decreasingColor = QColor(settings.value("priceChart/decreasingColor", palette.decreasing.name(QColor::HexRgb)).toString());
    if (!visual.increasingColor.isValid()) {
        visual.increasingColor = palette.increasing;
    }
    if (!visual.decreasingColor.isValid()) {
        visual.decreasingColor = palette.decreasing;
    }
    return visual;
}

void saveChartVisualSettings(const ChartVisualSettings& visual)
{
    QSettings settings;
    settings.setValue("priceChart/palette", visual.paletteId);
    settings.setValue("priceChart/increasingColor", visual.increasingColor.name(QColor::HexRgb));
    settings.setValue("priceChart/decreasingColor", visual.decreasingColor.name(QColor::HexRgb));
}

QString swatchStyle(const QColor& color)
{
    return "background: " + color.name(QColor::HexRgb) + "; border: 1px solid #414b5b; border-radius: 4px;";
}

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

class ChartSettingsDialog final : public QDialog {
public:
    using ChangeHandler = std::function<void(const ChartVisualSettings&)>;

    ChartSettingsDialog(ChartVisualSettings visual, ChangeHandler changeHandler, QWidget* parent = nullptr)
        : QDialog(parent)
        , visual_(std::move(visual))
        , changeHandler_(std::move(changeHandler))
    {
        setWindowTitle("Price Chart Settings");
        setModal(false);
        resize(520, 360);
        setObjectName("chartSettingsDialog");

        auto* root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* sidebar = new QListWidget(this);
        sidebar->setObjectName("chartSettingsSidebar");
        sidebar->setFixedWidth(150);
        sidebar->addItems({"Appearance", "Candles", "Scale"});
        sidebar->setCurrentRow(0);
        root->addWidget(sidebar);

        auto* stack = new QStackedWidget(this);
        stack->setObjectName("chartSettingsStack");
        root->addWidget(stack, 1);

        auto* appearance = new QFrame(stack);
        appearance->setObjectName("chartSettingsPage");
        auto* appearanceLayout = new QFormLayout(appearance);
        appearanceLayout->setContentsMargins(18, 18, 18, 18);
        appearanceLayout->setSpacing(12);
        auto* paletteCombo = new QComboBox(appearance);
        paletteCombo->setObjectName("chartSettingsCombo");
        for (const auto& palette : chartPalettes()) {
            paletteCombo->addItem(palette.name, palette.id);
        }
        paletteCombo->addItem("Custom", "custom");
        const int paletteIndex = paletteCombo->findData(visual_.paletteId);
        paletteCombo->setCurrentIndex(paletteIndex >= 0 ? paletteIndex : 0);
        appearanceLayout->addRow("Palette", paletteCombo);
        stack->addWidget(appearance);

        auto* candles = new QFrame(stack);
        candles->setObjectName("chartSettingsPage");
        auto* candleLayout = new QFormLayout(candles);
        candleLayout->setContentsMargins(18, 18, 18, 18);
        candleLayout->setSpacing(12);
        increasingSwatch_ = new QPushButton(candles);
        increasingSwatch_->setObjectName("chartColorSwatch");
        increasingSwatch_->setFixedSize(92, 28);
        decreasingSwatch_ = new QPushButton(candles);
        decreasingSwatch_->setObjectName("chartColorSwatch");
        decreasingSwatch_->setFixedSize(92, 28);
        candleLayout->addRow("Up candle", increasingSwatch_);
        candleLayout->addRow("Down candle", decreasingSwatch_);
        stack->addWidget(candles);

        auto* scale = new QFrame(stack);
        scale->setObjectName("chartSettingsPage");
        auto* scaleLayout = new QVBoxLayout(scale);
        scaleLayout->setContentsMargins(18, 18, 18, 18);
        scaleLayout->setSpacing(10);
        auto* priceAxis = new QLabel("Price axis: right", scale);
        priceAxis->setObjectName("chartSettingsLabel");
        auto* timeAxis = new QLabel("Time axis: bottom", scale);
        timeAxis->setObjectName("chartSettingsLabel");
        scaleLayout->addWidget(priceAxis);
        scaleLayout->addWidget(timeAxis);
        scaleLayout->addStretch();
        stack->addWidget(scale);

        connect(sidebar, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
        connect(paletteCombo, &QComboBox::currentIndexChanged, this, [this, paletteCombo](int index) {
            const QString id = paletteCombo->itemData(index).toString();
            visual_.paletteId = id;
            if (id != "custom") {
                const ChartPalette palette = chartPaletteById(id);
                visual_.increasingColor = palette.increasing;
                visual_.decreasingColor = palette.decreasing;
            }
            updateSwatches();
            notify();
        });
        connect(increasingSwatch_, &QPushButton::clicked, this, [this, paletteCombo] {
            chooseColor(&visual_.increasingColor, paletteCombo);
        });
        connect(decreasingSwatch_, &QPushButton::clicked, this, [this, paletteCombo] {
            chooseColor(&visual_.decreasingColor, paletteCombo);
        });

        updateSwatches();
        setStyleSheet(R"(
            QDialog#chartSettingsDialog {
                background: #0b0f15;
                color: #e6e8ec;
                font-family: "Helvetica Neue";
                font-size: 13px;
            }
            QListWidget#chartSettingsSidebar {
                background: #11151d;
                border: 0;
                border-right: 1px solid #252d3a;
                color: #aeb7c6;
                outline: 0;
            }
            QListWidget#chartSettingsSidebar::item {
                height: 34px;
                padding-left: 12px;
            }
            QListWidget#chartSettingsSidebar::item:selected {
                background: #1a2330;
                color: #f0f2f4;
                border-left: 2px solid #f0c567;
            }
            QFrame#chartSettingsPage {
                background: #0b0f15;
                border: 0;
            }
            QLabel,
            QLabel#chartSettingsLabel {
                color: #d6dbe5;
                font-weight: 800;
            }
            QComboBox#chartSettingsCombo {
                background: #141a24;
                border: 1px solid #303b4d;
                border-radius: 5px;
                color: #f0f2f4;
                min-height: 30px;
                padding: 0 8px;
            }
            QPushButton#chartColorSwatch {
                border-radius: 4px;
            }
        )");
    }

private:
    void updateSwatches()
    {
        if (increasingSwatch_) {
            increasingSwatch_->setStyleSheet(swatchStyle(visual_.increasingColor));
            increasingSwatch_->setText(visual_.increasingColor.name(QColor::HexRgb).toUpper());
        }
        if (decreasingSwatch_) {
            decreasingSwatch_->setStyleSheet(swatchStyle(visual_.decreasingColor));
            decreasingSwatch_->setText(visual_.decreasingColor.name(QColor::HexRgb).toUpper());
        }
    }

    void chooseColor(QColor* target, QComboBox* paletteCombo)
    {
        const QColor next = QColorDialog::getColor(*target, this, "Select candle color");
        if (!next.isValid()) {
            return;
        }
        *target = next;
        visual_.paletteId = "custom";
        const int customIndex = paletteCombo->findData("custom");
        if (customIndex >= 0) {
            paletteCombo->setCurrentIndex(customIndex);
        }
        updateSwatches();
        notify();
    }

    void notify()
    {
        if (changeHandler_) {
            changeHandler_(visual_);
        }
    }

    ChartVisualSettings visual_;
    ChangeHandler changeHandler_;
    QPushButton* increasingSwatch_{nullptr};
    QPushButton* decreasingSwatch_{nullptr};
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

QString compactCount(qint64 value)
{
    if (value >= 1000000) {
        return QString::number(value / 1000000.0, 'f', value >= 10000000 ? 1 : 2) + "M";
    }
    if (value >= 1000) {
        return QString::number(value / 1000.0, 'f', value >= 10000 ? 1 : 2) + "K";
    }
    return QString::number(value);
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
    loadingSpinnerTimer_.setInterval(120);
    QObject::connect(&loadingSpinnerTimer_, &QTimer::timeout, this, [this] { updateLoadingSpinner(); });

    auto* fullScreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    QObject::connect(fullScreenShortcut, &QShortcut::activated, this, [this] { toggleFullScreen(); });

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
    applyChartVisualSettings();
    updateAxisTitles();
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
    auto* maximize = createWindowControl("windowMaximize");
    layout->addWidget(close);
    layout->addWidget(minimize);
    layout->addWidget(maximize);

    QObject::connect(close, &QPushButton::clicked, this, &QWidget::close);
    QObject::connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);
    QObject::connect(maximize, &QPushButton::clicked, this, [this] { toggleMaximized(); });

    title_ = new QLabel(connection_.name.isEmpty() ? exchange_ + " / " + symbol_ : connection_.name + " / " + exchange_ + " / " + symbol_, chrome_);
    title_->setObjectName("chartTitle");
    title_->setMinimumWidth(170);
    title_->installEventFilter(this);
    layout->addWidget(title_);

    auto* indicatorButton = makeIconButton({}, "Indicators", chrome_, ":/icons/indicator.svg");
    connect(indicatorButton, &QToolButton::clicked, this, [this] { showIndicators(); });
    layout->addWidget(indicatorButton);

    layout->addStretch();

    rightTitle_ = new QLabel(chrome_);
    rightTitle_->setObjectName("chartRightTitle");
    rightTitle_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rightTitle_->setMinimumWidth(132);
    layout->addWidget(rightTitle_);

    auto* symbol = new QComboBox(chrome_);
    symbol->setObjectName("chartCombo");
    symbol->addItem(exchange_ + " / " + symbol_);
    symbol->addItems({"CME / MNQ", "CME / NQ", "COMEX / MGC", "COMEX / GC", "NYMEX / CL"});
    symbol->setFixedWidth(128);
    layout->addWidget(symbol);

    timeframe_ = new QComboBox(chrome_);
    timeframe_->setObjectName("chartCombo");
    for (const auto& option : kTimeframes) {
        timeframe_->addItem(QString::fromLatin1(option.label), option.minutes);
    }
    const int defaultTimeframeIndex = timeframe_->findData(kHistoryBarMinutes);
    timeframe_->setCurrentIndex(defaultTimeframeIndex >= 0 ? defaultTimeframeIndex : 1);
    timeframe_->setFixedWidth(72);
    connect(timeframe_, &QComboBox::currentIndexChanged, this, [this](int index) { handleTimeframeChanged(index); });
    layout->addWidget(timeframe_);

    auto* settingsButton = makeIconButton({}, "Price chart settings", chrome_, ":/icons/settings.svg");
    connect(settingsButton, &QToolButton::clicked, this, [this] { showChartSettings(); });
    layout->addWidget(settingsButton);

    updateHeaderTitles(symbol_);

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
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    loadingPanel_ = new QFrame(panel);
    loadingPanel_->setObjectName("chartLoadingPanel");
    auto* loadingLayout = new QVBoxLayout(loadingPanel_);
    loadingLayout->setContentsMargins(10, 8, 10, 8);
    loadingLayout->setSpacing(7);

    auto* loadingCopy = new QHBoxLayout;
    loadingCopy->setContentsMargins(0, 0, 0, 0);
    loadingCopy->setSpacing(8);
    loadingSpinner_ = new QLabel("|", loadingPanel_);
    loadingSpinner_->setObjectName("loadingSpinner");
    loadingSpinner_->setFixedWidth(18);
    loadingSpinner_->setAlignment(Qt::AlignCenter);
    feedState_ = new QLabel("Download data, this may take a while", loadingPanel_);
    feedState_->setObjectName("feedStateLabel");
    loadingDetail_ = new QLabel("Preparing 10D tick data", loadingPanel_);
    loadingDetail_->setObjectName("loadingDetailLabel");
    loadingDetail_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    loadingCopy->addWidget(loadingSpinner_);
    loadingCopy->addWidget(feedState_, 1);
    loadingCopy->addWidget(loadingDetail_);
    loadingLayout->addLayout(loadingCopy);

    buildProgress_ = new QProgressBar(panel);
    buildProgress_->setObjectName("buildProgress");
    buildProgress_->setFixedHeight(6);
    buildProgress_->setTextVisible(false);
    buildProgress_->setRange(0, 100);
    loadingLayout->addWidget(buildProgress_);
    loadingPanel_->hide();

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
    chart_->setBackgroundRoundness(0);
    chart_->setMargins(QMargins(0, 0, 0, 0));
    chart_->layout()->setContentsMargins(0, 0, 0, 0);

    axisX_ = new QDateTimeAxis;
    axisX_->setFormat(axisFormatForTimeframe(selectedBarMinutes_));
    axisX_->setTitleText({});
    axisX_->setTitleVisible(false);
    axisX_->setTickCount(8);
    axisX_->setLabelsVisible(true);
    axisX_->setLabelsColor(QColor("#9aa4b3"));
    axisX_->setGridLineColor(QColor(255, 255, 255, 18));
    axisX_->setLinePenColor(QColor("#242b36"));
    chart_->addAxis(axisX_, Qt::AlignBottom);
    candleSeries_->attachAxis(axisX_);

    axisY_ = new QValueAxis;
    axisY_->setLabelFormat("%.2f");
    axisY_->setTitleText({});
    axisY_->setTitleVisible(false);
    axisY_->setTickCount(8);
    axisY_->setRange(0.0, 1.0);
    axisY_->setLabelsVisible(true);
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
    auto* timeAxis = new QFrame(panel);
    timeAxis->setObjectName("timeAxisStrip");
    timeAxis->setFixedHeight(20);
    auto* timeAxisLayout = new QHBoxLayout(timeAxis);
    timeAxisLayout->setContentsMargins(0, 0, 0, 0);
    timeAxisLayout->setSpacing(8);
    timeAxisStart_ = new QLabel(timeAxis);
    timeAxisStart_->setObjectName("timeAxisLabel");
    timeAxisStart_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    timeAxisMid_ = new QLabel(timeAxis);
    timeAxisMid_->setObjectName("timeAxisLabel");
    timeAxisMid_->setAlignment(Qt::AlignCenter);
    timeAxisEnd_ = new QLabel(timeAxis);
    timeAxisEnd_->setObjectName("timeAxisLabel");
    timeAxisEnd_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeAxisLayout->addWidget(timeAxisStart_, 1);
    timeAxisLayout->addWidget(timeAxisMid_, 1);
    timeAxisLayout->addWidget(timeAxisEnd_, 1);
    layout->addWidget(loadingPanel_);
    layout->addWidget(view, 1);
    layout->addWidget(timeAxis);
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

void ChartWindow::showChartSettings()
{
    auto* dialog = new ChartSettingsDialog(loadChartVisualSettings(), [this](const ChartVisualSettings& visual) {
        saveChartVisualSettings(visual);
        applyChartVisualSettings();
    }, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
    dialog->raise();
}

void ChartWindow::connectMarketData()
{
    if (!adapter_) {
        showStatusMessage("No active feed session.");
        return;
    }

    adapter_->addSnapshotHandler(this, [this](const MarketSnapshot& snapshot) {
        if (QThread::currentThread() == thread()) {
            queueSnapshot(snapshot);
        } else {
            QMetaObject::invokeMethod(this, [this, snapshot] { queueSnapshot(snapshot); }, Qt::QueuedConnection);
        }
    });

    showStatusMessage("Using connected " + adapter_->name() + " session...");
    adapter_->subscribe(exchange_ + ":" + symbol_);
    requestChartData(true);
    queueSnapshot(adapter_->snapshot());
}

void ChartWindow::requestChartData(bool allowCached)
{
    if (!adapter_) {
        return;
    }
    adapter_->requestChartData({symbol_, exchange_, kHistoryLookbackDays, selectedBarMinutes_, allowCached});
}

void ChartWindow::handleTimeframeChanged(int index)
{
    if (!timeframe_) {
        return;
    }

    const int minutes = std::max(timeframe_->itemData(index).toInt(), 1);
    if (minutes == selectedBarMinutes_) {
        return;
    }

    selectedBarMinutes_ = minutes;
    userTimeRange_ = false;
    userPriceRange_ = false;
    wasPinnedRight_ = true;
    candles_.clear();
    renderedCandleFingerprint_ = 0;
    if (candleSeries_) {
        candleSeries_->clear();
    }
    updateAxisTitles();
    updateHeaderTitles(symbol_);
    showStatusMessage("Loading " + timeframeLabel(selectedBarMinutes_) + " chart data...");
    requestChartData(true);
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
    if (!snapshot.symbol.trimmed().isEmpty()) {
        updateHeaderTitles(snapshot.symbol);
    }

    if (snapshot.buildingChartData) {
        showLoadingState(snapshot);
    }

    if (!snapshot.connected && candles_.isEmpty()) {
        if (!snapshot.buildingChartData) {
            showStatusMessage(snapshot.connectionLabel.trimmed().isEmpty() ? "Waiting for live feed..." : snapshot.connectionLabel);
        }
        if (candleDataChanged) {
            renderCandles();
            renderedCandleFingerprint_ = fingerprint;
        }
        return;
    }

    if (candles_.isEmpty()) {
        if (!snapshot.buildingChartData) {
            if (snapshot.connected && !snapshot.connectionLabel.trimmed().isEmpty() && snapshot.connectionLabel != "Live Rithmic feed" && snapshot.connectionLabel != "Rithmic Protocol connected.") {
                showStatusMessage(snapshot.connectionLabel);
            } else {
                showStatusMessage(snapshot.connected ? "Connected. Waiting for live candles for " + snapshot.symbol + "..." : snapshot.connectionLabel);
            }
        }
        if (candleDataChanged) {
            renderCandles();
            renderedCandleFingerprint_ = fingerprint;
        }
        return;
    }

    wasPinnedRight_ = isFollowingRightEdge(previousEnd);
    if (candleDataChanged) {
        updateVisibleRanges(userTimeRange_ && !wasPinnedRight_);
        renderedCandleFingerprint_ = fingerprint;
    }

    if (!snapshot.buildingChartData) {
        hideLoadingState();
    }
}

void ChartWindow::showLoadingState(const MarketSnapshot& snapshot)
{
    if (!loadingPanel_ || !feedState_ || !loadingDetail_ || !buildProgress_) {
        return;
    }

    const int expectedBars = std::max(snapshot.chartBarsExpected, 1);
    const int loadedBars = std::clamp(snapshot.chartBarsLoaded, 0, expectedBars);
    const int expectedDays = std::max(snapshot.chartDaysExpected, 1);
    const int loadedDays = std::clamp(snapshot.chartDaysLoaded, 0, expectedDays);
    const QString title = snapshot.chartDataLabel.trimmed().isEmpty() ? "Download data, this may take a while" : snapshot.chartDataLabel.trimmed();

    feedState_->setText(title);
    loadingDetail_->setText(
        compactCount(snapshot.chartTicksLoaded) + " ticks"
        + "  |  " + QString::number(loadedDays) + "/" + QString::number(expectedDays) + " days"
        + "  |  " + QString::number(loadedBars) + "/" + QString::number(expectedBars) + " bars");
    buildProgress_->setRange(0, expectedBars);
    buildProgress_->setValue(loadedBars);
    buildProgress_->show();
    if (loadingSpinner_) {
        loadingSpinner_->show();
    }
    loadingPanel_->show();
    if (!loadingSpinnerTimer_.isActive()) {
        loadingSpinnerTimer_.start();
    }
}

void ChartWindow::showStatusMessage(const QString& message)
{
    if (!loadingPanel_ || !feedState_ || !loadingDetail_ || !buildProgress_) {
        return;
    }

    feedState_->setText(message.trimmed().isEmpty() ? "Waiting for live feed..." : message.trimmed());
    loadingDetail_->clear();
    buildProgress_->hide();
    if (loadingSpinner_) {
        loadingSpinner_->hide();
    }
    loadingSpinnerTimer_.stop();
    loadingPanel_->show();
}

void ChartWindow::hideLoadingState()
{
    if (!loadingPanel_) {
        return;
    }

    loadingSpinnerTimer_.stop();
    loadingPanel_->hide();
}

void ChartWindow::updateLoadingSpinner()
{
    if (!loadingSpinner_) {
        return;
    }

    static const QString frames[] = {"|", "/", "-", "\\"};
    loadingSpinner_->setText(frames[loadingSpinnerFrame_ % 4]);
    ++loadingSpinnerFrame_;
}

void ChartWindow::renderCandles()
{
    if (!candleSeries_ || !axisX_ || !axisY_) {
        return;
    }

    candleSeries_->clear();
    if (candles_.isEmpty()) {
        return;
    }

    int startIndex = 0;
    int endIndex = candles_.size();
    if (visibleStartMs_ < visibleEndMs_) {
        const auto startIt = std::lower_bound(candles_.begin(), candles_.end(), visibleStartMs_, [](const Candle& candle, qint64 time) {
            return candle.time.toMSecsSinceEpoch() < time;
        });
        const auto endIt = std::upper_bound(candles_.begin(), candles_.end(), visibleEndMs_, [](qint64 time, const Candle& candle) {
            return time < candle.time.toMSecsSinceEpoch();
        });
        startIndex = std::max(0, static_cast<int>(std::distance(candles_.begin(), startIt)) - 50);
        endIndex = std::min(static_cast<int>(candles_.size()), static_cast<int>(std::distance(candles_.begin(), endIt)) + 50);
    } else {
        startIndex = std::max(0, static_cast<int>(candles_.size()) - kMaxRenderedCandles);
    }
    if (endIndex - startIndex > kMaxRenderedCandles) {
        startIndex = std::max(startIndex, endIndex - kMaxRenderedCandles);
    }

    QList<QCandlestickSet*> sets;
    sets.reserve(endIndex - startIndex);
    for (int i = startIndex; i < endIndex; ++i) {
        const auto& candle = candles_.at(i);
        if (!isRenderableCandle(candle)) {
            continue;
        }
        sets.append(new QCandlestickSet(candle.open, candle.high, candle.low, candle.close, static_cast<qreal>(candle.time.toMSecsSinceEpoch())));
    }
    if (!sets.isEmpty()) {
        candleSeries_->append(sets);
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
    const qint64 rightEdgeMs = rightEdgeForLastCandle(lastMs);
    if (!preserveUserView || visibleStartMs_ >= visibleEndMs_) {
        const int visibleBars = std::min(static_cast<int>(candles_.size()), 120);
        const qint64 startMs = candles_.at(candles_.size() - visibleBars).time.toMSecsSinceEpoch();
        setTimeRange(startMs, rightEdgeMs);
        userTimeRange_ = false;
    } else {
        const qint64 range = std::max<qint64>(visibleEndMs_ - visibleStartMs_, minVisibleRangeMs());
        if (wasPinnedRight_) {
            setTimeRange(rightEdgeMs - range, rightEdgeMs);
        } else {
            setTimeRange(std::max(firstMs, visibleStartMs_), std::min(rightEdgeMs, visibleEndMs_));
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
    const qint64 minRange = minVisibleRangeMs();
    if (endMs <= startMs) {
        endMs = startMs + minRange;
    }
    if (endMs - startMs < minRange) {
        const qint64 center = startMs + (endMs - startMs) / 2;
        startMs = center - minRange / 2;
        endMs = startMs + minRange;
    }
    const qint64 maxRange = maxVisibleRangeMs();
    if (endMs - startMs > maxRange) {
        const qint64 center = startMs + (endMs - startMs) / 2;
        startMs = center - maxRange / 2;
        endMs = startMs + maxRange;
    }

    if (!candles_.isEmpty()) {
        const qint64 firstMs = candles_.first().time.toMSecsSinceEpoch();
        const qint64 rightEdgeMs = rightEdgeForLastCandle(candles_.last().time.toMSecsSinceEpoch());
        const qint64 dataRange = std::max<qint64>(rightEdgeMs - firstMs, minRange);
        qint64 requestedRange = endMs - startMs;
        if (requestedRange >= dataRange) {
            startMs = firstMs;
            endMs = rightEdgeMs;
        } else {
            if (startMs < firstMs) {
                endMs += firstMs - startMs;
                startMs = firstMs;
            }
            if (endMs > rightEdgeMs) {
                startMs -= endMs - rightEdgeMs;
                endMs = rightEdgeMs;
            }
            if (startMs < firstMs) {
                startMs = firstMs;
            }
        }
    }
    visibleStartMs_ = startMs;
    visibleEndMs_ = endMs;
    axisX_->setRange(QDateTime::fromMSecsSinceEpoch(visibleStartMs_, QTimeZone::UTC), QDateTime::fromMSecsSinceEpoch(visibleEndMs_, QTimeZone::UTC));
    updateTimeAxisLabels();
    renderCandles();
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

qint64 ChartWindow::barDurationMs() const
{
    return std::max<qint64>(selectedBarMinutes_, 1) * 60 * 1000;
}

qint64 ChartWindow::minVisibleRangeMs() const
{
    return std::max<qint64>(barDurationMs() * 5, 5 * 60 * 1000);
}

qint64 ChartWindow::maxVisibleRangeMs() const
{
    return std::max<qint64>(kHistoryLookbackDays * kDayMs + barDurationMs(), barDurationMs() * 12);
}

qint64 ChartWindow::rightEdgeForLastCandle(qint64 lastMs) const
{
    return lastMs + barDurationMs();
}

bool ChartWindow::isFollowingRightEdge(qint64 previousLastMs) const
{
    if (!userTimeRange_ || previousLastMs <= 0 || visibleEndMs_ <= visibleStartMs_) {
        return true;
    }

    const qint64 expectedRightEdge = rightEdgeForLastCandle(previousLastMs);
    const qint64 tolerance = std::max<qint64>(barDurationMs() / 2, 30 * 1000);
    return std::llabs(visibleEndMs_ - expectedRightEdge) <= tolerance;
}

void ChartWindow::updateAxisTitles()
{
    if (axisX_) {
        axisX_->setFormat(axisFormatForTimeframe(selectedBarMinutes_));
        axisX_->setTitleText({});
        axisX_->setTitleVisible(false);
        axisX_->setLabelsVisible(true);
    }
    if (axisY_) {
        axisY_->setTitleText({});
        axisY_->setTitleVisible(false);
        axisY_->setLabelsVisible(true);
    }
    updateTimeAxisLabels();
}

void ChartWindow::updateHeaderTitles(const QString& symbol)
{
    const QString displaySymbol = symbol.trimmed().isEmpty() ? exchange_ + ":" + symbol_ : symbol.trimmed();
    if (title_) {
        title_->setText((connection_.name.isEmpty() ? QString{} : connection_.name + " / ") + displaySymbol);
    }
    if (rightTitle_) {
        rightTitle_->setText(displaySymbol + "  |  " + timeframeLabel(selectedBarMinutes_));
    }
}

void ChartWindow::updateTimeAxisLabels()
{
    if (!timeAxisStart_ || !timeAxisMid_ || !timeAxisEnd_) {
        return;
    }

    if (visibleEndMs_ <= visibleStartMs_) {
        timeAxisStart_->clear();
        timeAxisMid_->clear();
        timeAxisEnd_->clear();
        return;
    }

    const QString format = axisFormatForTimeframe(selectedBarMinutes_);
    const qint64 midMs = visibleStartMs_ + (visibleEndMs_ - visibleStartMs_) / 2;
    timeAxisStart_->setText(QDateTime::fromMSecsSinceEpoch(visibleStartMs_, QTimeZone::UTC).toString(format));
    timeAxisMid_->setText(QDateTime::fromMSecsSinceEpoch(midMs, QTimeZone::UTC).toString(format));
    timeAxisEnd_->setText(QDateTime::fromMSecsSinceEpoch(visibleEndMs_, QTimeZone::UTC).toString(format));
}

void ChartWindow::applyChartVisualSettings()
{
    const ChartVisualSettings visual = loadChartVisualSettings();
    chartPaletteId_ = visual.paletteId;
    if (candleSeries_) {
        candleSeries_->setIncreasingColor(visual.increasingColor);
        candleSeries_->setDecreasingColor(visual.decreasingColor);
        candleSeries_->setPen(QPen(QColor("#5d6775"), 1.0));
    }
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

    const qint64 currentRange = std::max<qint64>(visibleEndMs_ - visibleStartMs_, minVisibleRangeMs());
    const double factor = delta > 0 ? 0.78 : 1.28;
    const qint64 nextRange = std::clamp<qint64>(static_cast<qint64>(currentRange * factor), minVisibleRangeMs(), maxVisibleRangeMs());
    const QRectF plot = chart_->plotArea();
    const double cursorRatio = plot.width() > 0 ? std::clamp((event->position().x() - plot.left()) / plot.width(), 0.0, 1.0) : 0.5;
    const qint64 anchor = visibleStartMs_ + static_cast<qint64>(currentRange * cursorRatio);
    qint64 start = anchor - static_cast<qint64>(nextRange * cursorRatio);
    qint64 end = start + nextRange;

    const qint64 firstMs = candles_.first().time.toMSecsSinceEpoch();
    const qint64 lastMs = rightEdgeForLastCandle(candles_.last().time.toMSecsSinceEpoch());
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

    const qint64 timeRange = std::max<qint64>(dragStartMaxMs_ - dragStartMinMs_, minVisibleRangeMs());
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

void ChartWindow::toggleMaximized()
{
    if (isFullScreen()) {
        showNormal();
        return;
    }
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void ChartWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
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
        QFrame#chartLoadingPanel {
            background: #121821;
            border: 1px solid #263140;
            border-radius: 5px;
        }
        QLabel#feedStateLabel {
            background: transparent;
            border: 0;
            color: #ffcf5a;
            font-size: 12px;
            font-weight: 900;
        }
        QLabel#loadingSpinner {
            color: #ffcf5a;
            font-size: 14px;
            font-weight: 900;
        }
        QLabel#loadingDetailLabel {
            color: #aab4c3;
            font-size: 11px;
            font-weight: 800;
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
        QFrame#timeAxisStrip {
            background: #07090d;
            border: 0;
            border-top: 1px solid #1b222d;
        }
        QLabel#timeAxisLabel {
            color: #9aa4b3;
            font-size: 11px;
            font-weight: 800;
        }
        QLabel#chartTitle {
            color: #f0f2f4;
            font-size: 13px;
            font-weight: 900;
            padding-left: 8px;
        }
        QLabel#chartRightTitle {
            color: #aeb7c6;
            font-size: 12px;
            font-weight: 900;
            padding-right: 6px;
        }
        QPushButton#windowClose,
        QPushButton#windowMinimize,
        QPushButton#windowMaximize {
            border: 0;
            border-radius: 6px;
        }
        QPushButton#windowClose {
            background: #ff5f57;
        }
        QPushButton#windowMinimize {
            background: #ffbd2e;
        }
        QPushButton#windowMaximize {
            background: #28c840;
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
