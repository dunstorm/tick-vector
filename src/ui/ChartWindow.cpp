#include "ui/ChartWindow.hpp"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <cmath>
#include <utility>

namespace tc {

namespace {

class ChartCanvas final : public QWidget {
public:
    explicit ChartCanvas(QString symbol, QString exchange, QWidget* parent = nullptr)
        : QWidget(parent)
        , symbol_(std::move(symbol))
        , exchange_(std::move(exchange))
    {
        setMinimumSize(980, 560);
    }

private:
    QString symbol_;
    QString exchange_;

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor("#07090d"));

        const QRect plot = rect().adjusted(28, 24, -28, -28);
        painter.fillRect(plot, QColor("#090b10"));
        painter.setPen(QPen(QColor(255, 255, 255, 18), 1));
        for (int x = plot.left(); x <= plot.right(); x += 64) {
            painter.drawLine(x, plot.top(), x, plot.bottom());
        }
        for (int y = plot.top(); y <= plot.bottom(); y += 44) {
            painter.drawLine(plot.left(), y, plot.right(), y);
        }

        painter.setPen(QPen(QColor("#232832"), 1));
        painter.drawRect(plot.adjusted(0, 0, -1, -1));

        QPainterPath path;
        const int count = 72;
        for (int i = 0; i < count; ++i) {
            const double t = static_cast<double>(i) / (count - 1);
            const double wave = std::sin(t * 18.0) * 38.0 + std::cos(t * 7.0) * 26.0;
            const QPointF point(plot.left() + t * plot.width(), plot.center().y() - wave - t * 80.0);
            if (i == 0) {
                path.moveTo(point);
            } else {
                path.lineTo(point);
            }
        }

        painter.setPen(QPen(QColor("#69cf58"), 2));
        painter.drawPath(path);

        QFont title = painter.font();
        title.setPixelSize(15);
        title.setWeight(QFont::DemiBold);
        painter.setFont(title);
        painter.setPen(QColor("#c9ccd2"));
        painter.drawText(plot.adjusted(14, 12, -14, -12), Qt::AlignTop | Qt::AlignLeft, exchange_ + " / " + symbol_);

        QFont empty = painter.font();
        empty.setPixelSize(18);
        empty.setWeight(QFont::DemiBold);
        painter.setFont(empty);
        painter.setPen(QColor("#555b66"));
        painter.drawText(plot, Qt::AlignCenter, "Chart renderer will attach here");
    }
};

} // namespace

ChartWindow::ChartWindow(FeedConnection connection, QString symbol, QString exchange, QWidget* parent)
    : QMainWindow(parent)
    , connection_(std::move(connection))
    , symbol_(std::move(symbol))
    , exchange_(std::move(exchange))
{
    setWindowTitle("Price Chart");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    resize(1120, 680);

    auto* root = new QWidget(this);
    root->setObjectName("chartRoot");
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(0);
    layout->addWidget(createChrome());

    auto* body = new QFrame(root);
    body->setObjectName("chartBody");
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->addWidget(new ChartCanvas(symbol_, exchange_, body), 1);
    layout->addWidget(body, 1);

    setCentralWidget(root);
    applyLocalStyle();
}

QWidget* ChartWindow::createChrome()
{
    chrome_ = new QFrame(this);
    chrome_->setObjectName("chartChrome");
    chrome_->setFixedHeight(50);
    chrome_->installEventFilter(this);

    auto* layout = new QHBoxLayout(chrome_);
    layout->setContentsMargins(14, 0, 16, 0);
    layout->setSpacing(9);

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
    layout->addStretch();

    auto* symbol = new QComboBox(chrome_);
    symbol->setObjectName("chartCombo");
    symbol->addItem(exchange_ + " / " + symbol_);
    symbol->addItems({"CME / ES", "CME / NQ", "COMEX / GC", "NYMEX / CL"});
    symbol->setFixedWidth(128);
    layout->addWidget(symbol);

    auto* timeframe = new QComboBox(chrome_);
    timeframe->setObjectName("chartCombo");
    timeframe->addItems({"2m", "5m", "15m"});
    timeframe->setFixedWidth(72);
    layout->addWidget(timeframe);

    return chrome_;
}

QPushButton* ChartWindow::createWindowControl(const QString& objectName)
{
    auto* button = new QPushButton(this);
    button->setObjectName(objectName);
    button->setFixedSize(14, 14);
    button->setCursor(Qt::ArrowCursor);
    return button;
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
            background: #05070a;
            color: #e6e8ec;
            font-family: "Helvetica Neue";
            font-size: 13px;
        }
        QFrame#chartChrome {
            background: #1c1e23;
            border: 1px solid #30343d;
            border-bottom: 0;
            border-top-left-radius: 8px;
            border-top-right-radius: 8px;
        }
        QFrame#chartBody {
            background: #07090d;
            border: 1px solid #30343d;
            border-bottom-left-radius: 8px;
            border-bottom-right-radius: 8px;
        }
        QLabel#chartTitle {
            color: #f0f2f4;
            font-size: 14px;
            font-weight: 800;
            padding-left: 8px;
        }
        QPushButton#windowClose,
        QPushButton#windowMinimize {
            border: 0;
            border-radius: 7px;
        }
        QPushButton#windowClose {
            background: #ff5f57;
        }
        QPushButton#windowMinimize {
            background: #ffbd2e;
        }
        QPushButton#windowClose:hover {
            background: #ff746d;
        }
        QPushButton#windowMinimize:hover {
            background: #ffd04d;
        }
        QComboBox#chartCombo {
            background: #2c2f36;
            border: 1px solid #3a3f49;
            border-radius: 4px;
            color: #f0f2f4;
            min-height: 30px;
            padding: 0 8px;
            font-weight: 700;
        }
        QComboBox::drop-down {
            border: 0;
            width: 18px;
        }
    )");
}

} // namespace tc
