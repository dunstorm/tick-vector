#include "ui/DomWindow.hpp"

#include <QtCore/QHash>
#include <QtCore/QMetaObject>
#include <QtCore/QThread>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace tc {

namespace {

constexpr int kDomRowHeight = 22;
constexpr int kDomHeaderHeight = 28;

struct LadderRow {
    double price{};
    int bid{};
    int ask{};
    int delta{};
    int sessionDelta{};
    double profileVolume{};
    double profileDelta{};
    bool valueAreaHigh{};
    bool valueAreaLow{};
    bool pointOfControl{};
    bool lowVolumeNode{};
};

double tickSizeForDisplay(const QString& symbol)
{
    const QString upper = symbol.trimmed().toUpper();
    if (upper.contains(":GC") || upper.contains(":MGC")) {
        return 0.1;
    }
    if (upper.contains(":SI") || upper.contains(":SIL")) {
        return 0.005;
    }
    if (upper.contains(":HG")) {
        return 0.0005;
    }
    if (upper.contains(":ES") || upper.contains(":MES") || upper.contains(":NQ") || upper.contains(":MNQ")) {
        return 0.25;
    }
    if (upper.contains(":YM") || upper.contains(":MYM")) {
        return 1.0;
    }
    if (upper.contains(":CL") || upper.contains(":MCL")) {
        return 0.01;
    }
    return 0.25;
}

int decimalsForTick(double tick)
{
    if (tick >= 1.0) {
        return 0;
    }
    if (tick >= 0.1) {
        return 1;
    }
    if (tick >= 0.01) {
        return 2;
    }
    if (tick >= 0.001) {
        return 3;
    }
    return 4;
}

QString priceText(double price, double tick)
{
    return QString::number(price, 'f', decimalsForTick(tick));
}

QString compactNumber(double value)
{
    const double absValue = std::abs(value);
    if (absValue >= 1000000.0) {
        return QString::number(value / 1000000.0, 'f', 1) + "M";
    }
    if (absValue >= 1000.0) {
        return QString::number(value / 1000.0, 'f', 1) + "k";
    }
    return QString::number(std::llround(value));
}

QColor blended(const QColor& color, int alpha)
{
    QColor copy = color;
    copy.setAlpha(alpha);
    return copy;
}

double bestCenterPrice(const MarketSnapshot& snapshot, double tick)
{
    if (std::isfinite(snapshot.last) && snapshot.last > 0.0) {
        return std::round(snapshot.last / tick) * tick;
    }
    if (std::isfinite(snapshot.close) && snapshot.close > 0.0) {
        return std::round(snapshot.close / tick) * tick;
    }
    if (std::isfinite(snapshot.open) && snapshot.open > 0.0) {
        return std::round(snapshot.open / tick) * tick;
    }
    if (!snapshot.candles.isEmpty() && std::isfinite(snapshot.candles.last().close) && snapshot.candles.last().close > 0.0) {
        return std::round(snapshot.candles.last().close / tick) * tick;
    }
    if (!snapshot.dom.isEmpty()) {
        double high = std::numeric_limits<double>::lowest();
        double low = std::numeric_limits<double>::max();
        for (const auto& level : snapshot.dom) {
            if (level.price > 0.0) {
                high = std::max(high, level.price);
                low = std::min(low, level.price);
            }
        }
        if (high > low && low != std::numeric_limits<double>::max()) {
            return std::round(((high + low) * 0.5) / tick) * tick;
        }
    }
    if (!snapshot.volumeProfile.isEmpty()) {
        const auto poc = std::max_element(snapshot.volumeProfile.begin(), snapshot.volumeProfile.end(), [](const ProfileBin& left, const ProfileBin& right) {
            return left.volume < right.volume;
        });
        if (poc != snapshot.volumeProfile.end() && poc->price > 0.0) {
            return std::round(poc->price / tick) * tick;
        }
    }
    return 0.0;
}

QVector<LadderRow> buildRows(const MarketSnapshot& snapshot, int visibleRows, int manualCenterOffset)
{
    const double tick = tickSizeForDisplay(snapshot.symbol);
    const double center = bestCenterPrice(snapshot, tick);
    QVector<LadderRow> rows;
    if (center <= 0.0 || visibleRows <= 0) {
        return rows;
    }

    QHash<qint64, DomLevel> levelsByPrice;
    for (const auto& level : snapshot.dom) {
        if (level.price <= 0.0) {
            continue;
        }
        levelsByPrice.insert(std::llround(level.price / tick), level);
    }

    QHash<qint64, ProfileBin> profileByPrice;
    for (const auto& bin : snapshot.volumeProfile) {
        if (bin.price <= 0.0) {
            continue;
        }
        const qint64 key = std::llround(bin.price / tick);
        auto existing = profileByPrice.value(key);
        existing.price = key * tick;
        existing.volume += bin.volume;
        existing.delta += bin.delta;
        existing.valueAreaHigh = existing.valueAreaHigh || bin.valueAreaHigh;
        existing.valueAreaLow = existing.valueAreaLow || bin.valueAreaLow;
        existing.pointOfControl = existing.pointOfControl || bin.pointOfControl;
        existing.lowVolumeNode = existing.lowVolumeNode || bin.lowVolumeNode;
        profileByPrice.insert(key, existing);
    }

    const qint64 centerKey = std::llround(center / tick) + manualCenterOffset;
    const qint64 firstKey = centerKey + visibleRows / 2;
    rows.reserve(visibleRows);
    for (int i = 0; i < visibleRows; ++i) {
        const qint64 key = firstKey - i;
        const auto level = levelsByPrice.value(key);
        const auto profile = profileByPrice.value(key);
        rows.push_back({
            key * tick,
            level.bidSize,
            level.askSize,
            level.delta,
            level.sessionDelta,
            profile.volume,
            profile.delta,
            profile.valueAreaHigh,
            profile.valueAreaLow,
            profile.pointOfControl,
            profile.lowVolumeNode
        });
    }
    return rows;
}

void drawCellText(QPainter& painter, const QRect& rect, const QString& text, const QColor& color, int alignment)
{
    painter.setPen(color);
    painter.drawText(rect.adjusted(6, 0, -6, 0), alignment | Qt::AlignVCenter, text);
}

} // namespace

DomLadderWidget::DomLadderWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("domLadder");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void DomLadderWidget::setSnapshot(MarketSnapshot snapshot)
{
    snapshot_ = std::move(snapshot);
    update();
}

void DomLadderWidget::recenter()
{
    manualCenterOffset_ = 0;
    update();
}

QSize DomLadderWidget::minimumSizeHint() const
{
    return {620, 560};
}

void DomLadderWidget::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() > 0 ? 3 : -3;
    manualCenterOffset_ += steps;
    update();
    event->accept();
}

void DomLadderWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    recenter();
    event->accept();
}

void DomLadderWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor("#05070a"));

    const int priceWidth = 86;
    const int sizeWidth = 58;
    const int deltaWidth = 66;
    const int sessionWidth = std::clamp(width() / 5, 126, 210);
    const int remainingWidth = std::max(220, width() - priceWidth - (sizeWidth * 2) - (deltaWidth * 2) - sessionWidth);
    const int bidProfileWidth = remainingWidth / 2;
    const int askProfileWidth = remainingWidth - bidProfileWidth;
    const QVector<int> widths{
        bidProfileWidth,
        deltaWidth,
        sizeWidth,
        priceWidth,
        sizeWidth,
        deltaWidth,
        askProfileWidth,
        sessionWidth
    };
    QVector<int> starts;
    starts.reserve(widths.size());
    int cursor = 0;
    for (const int columnWidth : widths) {
        starts.push_back(cursor);
        cursor += columnWidth;
    }

    painter.fillRect(QRect(starts.at(0), kDomHeaderHeight, widths.at(0), height() - kDomHeaderHeight), QColor(12, 53, 35, 92));
    painter.fillRect(QRect(starts.at(1), kDomHeaderHeight, widths.at(1), height() - kDomHeaderHeight), QColor(58, 13, 18, 72));
    painter.fillRect(QRect(starts.at(2), kDomHeaderHeight, widths.at(2), height() - kDomHeaderHeight), QColor(14, 60, 48, 60));
    painter.fillRect(QRect(starts.at(3), kDomHeaderHeight, widths.at(3), height() - kDomHeaderHeight), QColor(8, 10, 14, 220));
    painter.fillRect(QRect(starts.at(4), kDomHeaderHeight, widths.at(4), height() - kDomHeaderHeight), QColor(58, 22, 24, 60));
    painter.fillRect(QRect(starts.at(5), kDomHeaderHeight, widths.at(5), height() - kDomHeaderHeight), QColor(12, 50, 34, 66));
    painter.fillRect(QRect(starts.at(6), kDomHeaderHeight, widths.at(6), height() - kDomHeaderHeight), QColor(42, 8, 62, 112));
    painter.fillRect(QRect(starts.at(7), kDomHeaderHeight, widths.at(7), height() - kDomHeaderHeight), QColor(11, 49, 31, 82));

    const QRect headerRect(0, 0, width(), kDomHeaderHeight);
    painter.fillRect(headerRect, QColor("#090d12"));
    painter.setPen(QColor("#202733"));
    painter.drawLine(headerRect.bottomLeft(), headerRect.bottomRight());
    const QStringList headers{"BID VOL", "BID Δ", "BID", "PRICE", "ASK", "ASK Δ", "ASK VOL", "PROFILE"};
    for (int i = 0; i < headers.size(); ++i) {
        drawCellText(painter, QRect(starts.at(i), 0, widths.at(i), kDomHeaderHeight), headers.at(i), QColor("#687384"), Qt::AlignCenter);
    }

    const int visibleRows = std::max(12, (height() - kDomHeaderHeight) / kDomRowHeight);
    const auto rows = buildRows(snapshot_, visibleRows, manualCenterOffset_);
    if (rows.isEmpty()) {
        painter.setPen(QColor("#8792a3"));
        const QString label = !snapshot_.chartDataLabel.trimmed().isEmpty()
            ? snapshot_.chartDataLabel
            : !snapshot_.connectionLabel.trimmed().isEmpty()
                ? snapshot_.connectionLabel
                : snapshot_.connected ? "Waiting for price data..." : "Connect a feed to build the DOM.";
        painter.drawText(rect().adjusted(0, kDomHeaderHeight, 0, 0), Qt::AlignCenter, label);
        return;
    }

    const double tick = tickSizeForDisplay(snapshot_.symbol);
    const double last = snapshot_.last > 0.0 ? std::round(snapshot_.last / tick) * tick : 0.0;
    double maxProfile = 1.0;
    double maxSplitProfile = 1.0;
    int maxBid = 1;
    int maxAsk = 1;
    int maxDelta = 1;
    for (const auto& row : rows) {
        maxProfile = std::max(maxProfile, row.profileVolume);
        const double bidProfile = row.profileVolume * (row.profileDelta >= 0.0 ? 0.64 : 0.34);
        const double askProfile = row.profileVolume * (row.profileDelta <= 0.0 ? 0.64 : 0.34);
        maxSplitProfile = std::max({maxSplitProfile, bidProfile, askProfile});
        maxBid = std::max(maxBid, row.bid);
        maxAsk = std::max(maxAsk, row.ask);
        maxDelta = std::max(maxDelta, std::abs(row.delta));
        maxDelta = std::max(maxDelta, std::abs(row.sessionDelta));
    }

    for (int i = 0; i < rows.size(); ++i) {
        const auto& row = rows.at(i);
        const int y = kDomHeaderHeight + i * kDomRowHeight;
        const QRect rowRect(0, y, width(), kDomRowHeight);
        const bool atLast = last > 0.0 && std::abs(row.price - last) < tick * 0.5;

        painter.fillRect(rowRect, QColor(i % 2 == 0 ? QColor(255, 255, 255, 8) : QColor(0, 0, 0, 0)));
        if (atLast) {
            painter.fillRect(rowRect, QColor(72, 63, 29, 90));
        }

        if (row.profileVolume > 0.0) {
            const double bidProfile = row.profileVolume * (row.profileDelta >= 0.0 ? 0.64 : 0.34);
            const double askProfile = row.profileVolume * (row.profileDelta <= 0.0 ? 0.64 : 0.34);
            const int bidProfileBar = std::clamp(static_cast<int>((widths.at(0) - 10) * (bidProfile / maxSplitProfile)), 2, widths.at(0) - 10);
            const int askProfileBar = std::clamp(static_cast<int>((widths.at(6) - 10) * (askProfile / maxSplitProfile)), 2, widths.at(6) - 10);
            const int sessionBar = std::clamp(static_cast<int>((widths.at(7) - 10) * (row.profileVolume / maxProfile)), 2, widths.at(7) - 10);
            painter.fillRect(QRect(starts.at(0) + widths.at(0) - bidProfileBar, y + 3, bidProfileBar, kDomRowHeight - 6), blended(QColor("#235d96"), atLast ? 190 : 132));
            painter.fillRect(QRect(starts.at(6), y + 3, askProfileBar, kDomRowHeight - 6), blended(QColor("#70308b"), atLast ? 190 : 130));
            QColor sessionColor = QColor("#a25a27");
            if (row.pointOfControl) {
                sessionColor = QColor("#a821b8");
            } else if (row.valueAreaHigh || row.valueAreaLow) {
                sessionColor = QColor("#b96a31");
            } else if (row.lowVolumeNode) {
                sessionColor = QColor("#5b3320");
            }
            painter.fillRect(QRect(starts.at(7) + widths.at(7) - sessionBar, y + 2, sessionBar, kDomRowHeight - 4), blended(sessionColor, row.pointOfControl ? 230 : 185));
            drawCellText(painter, QRect(starts.at(0), y, widths.at(0) - 4, kDomRowHeight), compactNumber(bidProfile), QColor("#9fb5c8"), Qt::AlignRight);
            drawCellText(painter, QRect(starts.at(6) + 4, y, widths.at(6) - 8, kDomRowHeight), compactNumber(askProfile), QColor("#9d8eb2"), Qt::AlignLeft);
            drawCellText(painter, QRect(starts.at(7), y, widths.at(7) - 6, kDomRowHeight), compactNumber(row.profileVolume), row.pointOfControl ? QColor("#e3a9ef") : QColor("#b99074"), Qt::AlignRight);
        }

        if (row.bid > 0) {
            const int barWidth = std::clamp(static_cast<int>((widths.at(2) - 6) * (static_cast<double>(row.bid) / maxBid)), 2, widths.at(2) - 6);
            painter.fillRect(QRect(starts.at(2) + widths.at(2) - barWidth, y + 2, barWidth, kDomRowHeight - 4), blended(QColor("#34a856"), atLast ? 170 : 100));
            drawCellText(painter, QRect(starts.at(2), y, widths.at(2), kDomRowHeight), QString::number(row.bid), QColor("#aee8bd"), Qt::AlignRight);
        }

        const QRect priceRect(starts.at(3), y, widths.at(3), kDomRowHeight);
        if (atLast) {
            painter.fillRect(priceRect.adjusted(1, 1, -1, -1), QColor("#8a5729"));
        }
        drawCellText(painter, priceRect, priceText(row.price, tick), atLast ? QColor("#f4f7fb") : QColor("#b6bfcd"), Qt::AlignCenter);

        if (row.ask > 0) {
            const int barWidth = std::clamp(static_cast<int>((widths.at(4) - 6) * (static_cast<double>(row.ask) / maxAsk)), 2, widths.at(4) - 6);
            painter.fillRect(QRect(starts.at(4), y + 2, barWidth, kDomRowHeight - 4), blended(QColor("#b84a4a"), atLast ? 178 : 104));
            drawCellText(painter, QRect(starts.at(4), y, widths.at(4), kDomRowHeight), QString::number(row.ask), QColor("#efb7b7"), Qt::AlignLeft);
        }

        const QColor deltaColor = row.delta >= 0 ? QColor("#73df80") : QColor("#ff7474");
        drawCellText(painter, QRect(starts.at(5), y, widths.at(5), kDomRowHeight), row.delta == 0 ? "" : QString::number(row.delta), deltaColor, Qt::AlignCenter);
        const QColor sessionColor = row.sessionDelta >= 0 ? QColor("#8ee89a") : QColor("#ff8a8a");
        drawCellText(painter, QRect(starts.at(1), y, widths.at(1), kDomRowHeight), row.sessionDelta == 0 ? "" : compactNumber(row.sessionDelta), sessionColor, Qt::AlignCenter);

        painter.setPen(QColor(255, 255, 255, 13));
        painter.drawLine(rowRect.bottomLeft(), rowRect.bottomRight());
    }

    painter.setPen(QColor("#1e2631"));
    for (int i = 1; i < starts.size(); ++i) {
        painter.drawLine(starts.at(i), 0, starts.at(i), height());
    }
}

DomWindow::DomWindow(FeedConnection connection, ITradingAdapter* sharedAdapter, QString symbol, QString exchange, QWidget* parent)
    : QMainWindow(parent)
    , connection_(std::move(connection))
    , symbol_(std::move(symbol))
    , exchange_(std::move(exchange))
    , adapter_(sharedAdapter)
{
    setWindowTitle("DOM");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMinimumSize(760, 560);
    resize(860, 720);
    renderTimer_.setSingleShot(true);
    renderTimer_.setInterval(40);
    QObject::connect(&renderTimer_, &QTimer::timeout, this, [this] { flushPendingSnapshot(); });

    auto* root = new QWidget(this);
    root->setObjectName("domRoot");
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createChrome());
    layout->addWidget(createWorkspace(), 1);
    setCentralWidget(root);

    applyLocalStyle();
    connectMarketData();
}

QWidget* DomWindow::createChrome()
{
    chrome_ = new QFrame(this);
    chrome_->setObjectName("domChrome");
    chrome_->setFixedHeight(44);
    chrome_->installEventFilter(this);

    auto* layout = new QHBoxLayout(chrome_);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    auto* close = createWindowControl("windowClose");
    auto* minimize = createWindowControl("windowMinimize");
    layout->addWidget(close);
    layout->addWidget(minimize);
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);

    title_ = new QLabel(connection_.name.isEmpty() ? exchange_ + ":" + symbol_ + " DOM" : connection_.name + " / " + exchange_ + ":" + symbol_ + " DOM", chrome_);
    title_->setObjectName("domTitle");
    title_->installEventFilter(this);
    layout->addWidget(title_);
    layout->addStretch();

    feedState_ = new QLabel("Waiting for feed", chrome_);
    feedState_->setObjectName("domFeedState");
    layout->addWidget(feedState_);

    auto* center = createActionButton("Center", "domSecondaryButton");
    connect(center, &QPushButton::clicked, this, [this] {
        if (ladder_) {
            ladder_->recenter();
        }
    });
    layout->addWidget(center);

    return chrome_;
}

QWidget* DomWindow::createWorkspace()
{
    auto* workspace = new QFrame(this);
    workspace->setObjectName("domWorkspace");
    auto* layout = new QVBoxLayout(workspace);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(0);

    ladder_ = new DomLadderWidget(workspace);
    layout->addWidget(ladder_, 1);
    return workspace;
}

QPushButton* DomWindow::createWindowControl(const QString& objectName)
{
    auto* button = new QPushButton(this);
    button->setObjectName(objectName);
    button->setFixedSize(12, 12);
    button->setCursor(Qt::ArrowCursor);
    return button;
}

QPushButton* DomWindow::createActionButton(const QString& text, const QString& objectName)
{
    auto* button = new QPushButton(text, this);
    button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(30);
    return button;
}

void DomWindow::connectMarketData()
{
    if (!adapter_) {
        if (feedState_) {
            feedState_->setText("No feed session");
        }
        return;
    }

    adapter_->addSnapshotHandler(this, [this](const MarketSnapshot& snapshot) {
        if (QThread::currentThread() == thread()) {
            queueSnapshot(snapshot);
        } else {
            QMetaObject::invokeMethod(this, [this, snapshot] { queueSnapshot(snapshot); }, Qt::QueuedConnection);
        }
    });

    feedState_->setText("Subscribing");
    adapter_->subscribe(exchange_ + ":" + symbol_);
    queueSnapshot(adapter_->snapshot());
}

void DomWindow::queueSnapshot(const MarketSnapshot& snapshot)
{
    pendingSnapshot_ = snapshot;
    hasPendingSnapshot_ = true;
    if (!renderTimer_.isActive()) {
        renderTimer_.start(snapshot_.dom.isEmpty() ? 0 : renderTimer_.interval());
    }
}

void DomWindow::flushPendingSnapshot()
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

void DomWindow::renderSnapshot(const MarketSnapshot& snapshot)
{
    snapshot_ = snapshot;
    if (title_ && !snapshot.symbol.trimmed().isEmpty()) {
        title_->setText((connection_.name.isEmpty() ? QString{} : connection_.name + " / ") + snapshot.symbol + " DOM");
    }
    if (feedState_) {
        if (snapshot.buildingChartData) {
            const int expected = std::max(snapshot.chartBarsExpected, 1);
            const int loaded = std::clamp(snapshot.chartBarsLoaded, 0, expected);
            feedState_->setText("Building " + QString::number(loaded) + "/" + QString::number(expected));
        } else if (snapshot.connected) {
            feedState_->setText("Live");
        } else if (!snapshot.connectionLabel.trimmed().isEmpty()) {
            feedState_->setText(snapshot.connectionLabel);
        } else {
            feedState_->setText("Waiting");
        }
    }

    if (ladder_) {
        ladder_->setSnapshot(snapshot);
    }
}

bool DomWindow::eventFilter(QObject* watched, QEvent* event)
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

void DomWindow::applyLocalStyle()
{
    setStyleSheet(R"(
        QWidget#domRoot {
            background: #06080c;
            color: #e6e8ec;
            font-family: "Helvetica Neue";
            font-size: 12px;
        }
        QFrame#domChrome {
            background: #11151d;
            border: 0;
            border-bottom: 1px solid #252d3a;
        }
        QLabel#domTitle {
            color: #f0f2f4;
            font-size: 13px;
            font-weight: 900;
            padding-left: 8px;
        }
        QLabel#domFeedState {
            background: #132017;
            border: 1px solid #3d7f45;
            border-radius: 5px;
            color: #8de386;
            min-width: 76px;
            padding: 5px 9px;
            font-weight: 900;
            qproperty-alignment: AlignCenter;
        }
        QFrame#domWorkspace {
            background: #07090d;
            border: 0;
        }
        QWidget#domLadder {
            background: #07090d;
            border: 1px solid #242d3a;
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
        QPushButton#domSecondaryButton,
        QPushButton#domRailButton,
        QPushButton#domNeutralButton {
            background: #0f141c;
            border: 1px solid #263140;
            border-radius: 4px;
            color: #cfd6e1;
            font-weight: 900;
        }
        QPushButton#domSecondaryButton:hover,
        QPushButton#domRailButton:hover,
        QPushButton#domNeutralButton:hover {
            background: #1c2430;
            border-color: #465366;
        }
    )");
}

} // namespace tc
