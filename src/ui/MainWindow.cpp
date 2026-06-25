#include "ui/MainWindow.hpp"

#include "adapters/TradingAdapterFactory.hpp"
#include "app/AppConstants.hpp"
#include "ui/ChartWindow.hpp"
#include "ui/DomWindow.hpp"
#include "ui/FeedSettingsDialog.hpp"
#include "ui/SelectInstrumentDialog.hpp"

#include <QtCore/QCryptographicHash>
#include <QtCore/QMetaObject>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>

namespace tc {

namespace {

constexpr auto kNoConnection = "__none__";
constexpr auto kFeedSettings = "__feed_settings__";

QString connectionFingerprint(const FeedConnection& connection)
{
    QByteArray payload;
    const auto append = [&payload](const QString& value) {
        const QByteArray utf8 = value.toUtf8();
        payload.append(QByteArray::number(utf8.size()));
        payload.append(':');
        payload.append(utf8);
        payload.append('\n');
    };

    append(connection.id);
    append(connection.feedSource.trimmed());
    append(connection.gateway.trimmed());
    append(connection.server.trimmed());
    append(connection.system.trimmed());
    append(connection.marketData.trimmed());
    append(connection.account.trimmed());
    append(connection.username.trimmed());
    append(connection.password);
    append(connection.appName.trimmed());
    append(connection.useDemoCredentials ? QStringLiteral("1") : QStringLiteral("0"));

    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

} // namespace

MainWindow::MainWindow(QWidget* parent, bool loadSavedConnections)
    : QMainWindow(parent)
{
    setWindowTitle(app::kDisplayName);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedSize(980, 40);
    if (loadSavedConnections) {
        loadConnections();
    }

    auto* root = new QWidget(this);
    root->setObjectName("root");
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createToolbar());

    setCentralWidget(root);
    applyStyle();
    rebuildConnectionSelector();
    if (!selectedConnectionId_.isEmpty()) {
        beginConnection(selectedConnectionId_);
    }
}

QWidget* MainWindow::createToolbar()
{
    toolbar_ = new QFrame(this);
    toolbar_->setObjectName("topToolbar");
    toolbar_->setFixedHeight(40);
    toolbar_->installEventFilter(this);

    auto* layout = new QHBoxLayout(toolbar_);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    auto* close = createWindowControl("windowClose");
    auto* minimize = createWindowControl("windowMinimize");
    layout->addWidget(close);
    layout->addWidget(minimize);

    QObject::connect(close, &QPushButton::clicked, this, &QWidget::close);
    QObject::connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);

    auto* brand = new QFrame(toolbar_);
    brand->setObjectName("brandGroup");
    brand->setFixedWidth(164);
    brand->setFixedHeight(30);
    brand->installEventFilter(this);
    auto* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(8);
    auto* brandMark = new QLabel("T", brand);
    brandMark->setObjectName("brandMark");
    brandMark->setFixedSize(24, 24);
    auto* logo = new QLabel("TICK VECTOR", brand);
    logo->setObjectName("brandLogo");
    logo->installEventFilter(this);
    brandLayout->addWidget(brandMark);
    brandLayout->addWidget(logo, 1);
    layout->addWidget(brand);

    auto* newMenu = new QMenu(toolbar_);
    priceChartAction_ = newMenu->addAction("Price Chart");
    priceChartAction_->setEnabled(false);
    domAction_ = newMenu->addAction("DOM");
    domAction_->setEnabled(false);
    QObject::connect(priceChartAction_, &QAction::triggered, this, [this] { openPriceChart(); });
    QObject::connect(domAction_, &QAction::triggered, this, [this] { openDom(); });
    layout->addWidget(createMenuButton("New", newMenu));

    layout->addStretch();

    auto* workspace = new QComboBox(toolbar_);
    workspace->setObjectName("workspaceSelector");
    workspace->addItem("Workspace");
    workspace->addItem("Default");
    workspace->setFixedWidth(152);
    layout->addWidget(workspace);

    auto* feed = new QLabel("Feed", toolbar_);
    feed->setObjectName("toolbarLabel");
    layout->addWidget(feed);

    connectionSelector_ = new QComboBox(toolbar_);
    connectionSelector_->setObjectName("connectionSelector");
    connectionSelector_->setFixedWidth(244);
    layout->addWidget(connectionSelector_);

    statusPill_ = new QFrame(toolbar_);
    statusPill_->setObjectName("connectionStatusPill");
    statusPill_->setProperty("state", "idle");
    statusPill_->setFixedHeight(28);
    auto* statusLayout = new QHBoxLayout(statusPill_);
    statusLayout->setContentsMargins(9, 0, 10, 0);
    statusLayout->setSpacing(6);
    statusDot_ = new QFrame(statusPill_);
    statusDot_->setObjectName("connectionStatusDot");
    statusDot_->setFixedSize(8, 8);
    statusLabel_ = new QLabel("Disconnected", statusPill_);
    statusLabel_->setObjectName("connectionStatus");
    statusLayout->addWidget(statusDot_);
    statusLayout->addWidget(statusLabel_);
    statusPill_->hide();
    layout->addWidget(statusPill_);

    QObject::connect(connectionSelector_, &QComboBox::activated, this, [this](int index) {
        const QString id = connectionSelector_->itemData(index).toString();
        if (id == kFeedSettings) {
            showFeedSettings();
            return;
        }
        if (id == kNoConnection) {
            selectConnectionById({});
            return;
        }
        selectConnectionById(id);
    });

    return toolbar_;
}

QPushButton* MainWindow::createWindowControl(const QString& objectName)
{
    auto* button = new QPushButton(this);
    button->setObjectName(objectName);
    button->setFixedSize(14, 14);
    button->setCursor(Qt::ArrowCursor);
    return button;
}

QToolButton* MainWindow::createMenuButton(const QString& text, QMenu* menu)
{
    auto* button = new QToolButton(this);
    button->setObjectName("toolbarMenuButton");
    button->setText(text);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMinimumWidth(66);
    button->setFixedHeight(28);
    if (menu) {
        button->setMenu(menu);
    }
    return button;
}

QPushButton* MainWindow::createTextButton(const QString& text)
{
    auto* button = new QPushButton(text, this);
    button->setObjectName("toolbarTextButton");
    button->setFixedHeight(28);
    return button;
}

void MainWindow::loadConnections()
{
    QString error;
    connections_ = connectionStore_.load(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Connection storage", error);
    }

    selectedConnectionId_.clear();
    for (const auto& connection : connections_) {
        if (connection.connectOnStartup) {
            selectedConnectionId_ = connection.id;
            break;
        }
    }
}

void MainWindow::rebuildConnectionSelector()
{
    connectionSelector_->blockSignals(true);
    connectionSelector_->clear();
    connectionSelector_->addItem("Select a connection", kNoConnection);
    for (const auto& connection : connections_) {
        connectionSelector_->addItem(connection.name.trimmed().isEmpty() ? "Unnamed Rithmic" : connection.name, connection.id);
    }
    connectionSelector_->insertSeparator(connectionSelector_->count());
    connectionSelector_->addItem("Feed Settings...", kFeedSettings);

    int selectedIndex = 0;
    const FeedConnection* selectedConnection = nullptr;
    if (!selectedConnectionId_.isEmpty()) {
        for (int i = 0; i < connectionSelector_->count(); ++i) {
            if (connectionSelector_->itemData(i).toString() == selectedConnectionId_) {
                selectedIndex = i;
                break;
            }
        }
        for (const auto& connection : connections_) {
            if (connection.id == selectedConnectionId_) {
                selectedConnection = &connection;
                break;
            }
        }
        if (!selectedConnection) {
            selectedConnectionId_.clear();
        }
    }
    connectionSelector_->setCurrentIndex(selectedIndex);
    connectionSelector_->blockSignals(false);

    const bool ready = selectedConnectionReady();
    priceChartAction_->setEnabled(ready);
    domAction_->setEnabled(ready);
    if (!selectedConnection) {
        setConnectionState("idle");
    } else {
        setConnectionState(connectionState_);
    }
}

void MainWindow::selectConnectionById(const QString& id)
{
    if (id.isEmpty()) {
        if (selectedConnectionId_.isEmpty() && !connectionAdapter_) {
            rebuildConnectionSelector();
            return;
        }

        selectedConnectionId_.clear();
        disconnectActiveConnection();
        connectionStatusMessage_.clear();
        setConnectionState("idle");
        rebuildConnectionSelector();
        return;
    }

    const auto it = std::find_if(connections_.begin(), connections_.end(), [&id](const FeedConnection& connection) {
        return connection.id == id;
    });

    if (it != connections_.end() && it->isComplete()) {
        const QString fingerprint = connectionFingerprint(*it);
        if (id == selectedConnectionId_ && connectionAdapter_ && activeConnectionId_ == id && activeConnectionFingerprint_ == fingerprint) {
            rebuildConnectionSelector();
            return;
        }
    }

    selectedConnectionId_ = id;
    if (it == connections_.end() || !it->isComplete()) {
        disconnectActiveConnection();
        connectionStatusMessage_.clear();
        setConnectionState("incomplete");
        rebuildConnectionSelector();
        return;
    }

    beginConnection(selectedConnectionId_);
}

void MainWindow::beginConnection(const QString& id)
{
    selectedConnectionId_ = id;
    const auto it = std::find_if(connections_.begin(), connections_.end(), [&id](const FeedConnection& connection) {
        return connection.id == id;
    });
    if (it == connections_.end() || !it->isComplete()) {
        disconnectActiveConnection();
        connectionStatusMessage_.clear();
        setConnectionState("incomplete");
        rebuildConnectionSelector();
        return;
    }

    const QString fingerprint = connectionFingerprint(*it);
    if (connectionAdapter_ && activeConnectionId_ == id && activeConnectionFingerprint_ == fingerprint) {
        rebuildConnectionSelector();
        return;
    }

    disconnectActiveConnection();
    const int attempt = ++connectionAttempt_;
    activeConnectionId_ = id;
    activeConnectionFingerprint_ = fingerprint;
    connectionStatusMessage_.clear();
    setConnectionState("connecting");
    rebuildConnectionSelector();

    connectionAdapter_ = createTradingAdapter(*it, this);
    connectionAdapter_->setSnapshotHandler([this, id, attempt](const MarketSnapshot& snapshot) {
        QMetaObject::invokeMethod(this, [this, id, attempt, snapshot] {
            if (attempt != connectionAttempt_ || id != selectedConnectionId_) {
                return;
            }
            const QString previousState = connectionState_;
            const QString previousMessage = connectionStatusMessage_;
            if (snapshot.connected) {
                connectionStatusMessage_.clear();
                if (connectionState_ != "connected") {
                    setConnectionState("connected");
                }
            } else if (snapshot.connectionFailed) {
                connectionStatusMessage_ = snapshot.connectionLabel.trimmed();
                if (connectionState_ != "failed" || connectionStatusMessage_ != previousMessage) {
                    setConnectionState("failed");
                }
            } else if (!snapshot.connectionLabel.trimmed().isEmpty()) {
                connectionStatusMessage_ = snapshot.connectionLabel.trimmed();
                if (connectionState_ != "connecting" || connectionStatusMessage_ != previousMessage) {
                    setConnectionState("connecting");
                }
            }
            if (connectionState_ != previousState || connectionStatusMessage_ != previousMessage) {
                rebuildConnectionSelector();
            }
        }, Qt::QueuedConnection);
    });

    const bool started = connectionAdapter_->connectAdapter(it->toConnectionConfig());
    if (attempt != connectionAttempt_ || id != selectedConnectionId_) {
        return;
    }

    if (!started) {
        connectionStatusMessage_ = connectionAdapter_->snapshot().connectionLabel.trimmed();
        setConnectionState("failed");
    } else if (connectionAdapter_->isConnected()) {
        connectionStatusMessage_.clear();
        setConnectionState("connected");
    } else {
        setConnectionState("connecting");
    }
    rebuildConnectionSelector();
}

void MainWindow::disconnectActiveConnection()
{
    if (!connectionAdapter_ && activeConnectionId_.isEmpty()) {
        return;
    }

    ++connectionAttempt_;
    if (connectionAdapter_) {
        connectionAdapter_->disconnectAdapter();
        connectionAdapter_.reset();
    }
    activeConnectionId_.clear();
    activeConnectionFingerprint_.clear();
}

void MainWindow::setConnectionState(const QString& state)
{
    connectionState_ = state;
    if (!statusPill_ || !statusLabel_) {
        return;
    }

    if (state == "idle" || selectedConnectionId_.isEmpty()) {
        statusPill_->hide();
        priceChartAction_->setEnabled(false);
        domAction_->setEnabled(false);
        return;
    }

    statusPill_->show();
    statusPill_->setProperty("state", state);
    statusDot_->setProperty("state", state);
    if (state == "connecting") {
        statusLabel_->setText("Connecting");
    } else if (state == "connected") {
        statusLabel_->setText("Connected");
    } else if (state == "incomplete") {
        statusLabel_->setText("Incomplete");
    } else if (state == "failed") {
        statusLabel_->setText(connectionStatusMessage_.contains("linked", Qt::CaseInsensitive) ? "Not linked" : "Failed");
    } else {
        statusLabel_->setText("Disconnected");
    }
    statusPill_->setToolTip(connectionStatusMessage_);
    statusPill_->style()->unpolish(statusPill_);
    statusPill_->style()->polish(statusPill_);
    statusDot_->style()->unpolish(statusDot_);
    statusDot_->style()->polish(statusDot_);
    const bool ready = selectedConnectionReady();
    priceChartAction_->setEnabled(ready);
    domAction_->setEnabled(ready);
}

bool MainWindow::selectedConnectionReady() const
{
    if (connectionState_ != "connected") {
        return false;
    }
    if (!connectionAdapter_ || !connectionAdapter_->isConnected()) {
        return false;
    }
    const auto it = std::find_if(connections_.begin(), connections_.end(), [this](const FeedConnection& connection) {
        return connection.id == selectedConnectionId_;
    });
    return it != connections_.end() && it->isComplete();
}

void MainWindow::showFeedSettings()
{
    FeedSettingsDialog dialog(connections_, this);
    if (dialog.exec() != QDialog::Accepted) {
        rebuildConnectionSelector();
        return;
    }

    connections_ = dialog.connections();
    QString error;
    if (!connectionStore_.save(connections_, &error)) {
        QMessageBox::warning(this, "Connection storage", error);
    }

    bool selectedStillExists = false;
    for (const auto& connection : connections_) {
        if (connection.id == selectedConnectionId_) {
            selectedStillExists = true;
            break;
        }
    }
    if (!selectedStillExists) {
        selectConnectionById({});
        return;
    }

    selectConnectionById(selectedConnectionId_);
}

void MainWindow::openPriceChart()
{
    const auto it = std::find_if(connections_.begin(), connections_.end(), [this](const FeedConnection& connection) {
        return connection.id == selectedConnectionId_;
    });
    if (it == connections_.end() || !it->isComplete() || !selectedConnectionReady()) {
        setConnectionState(it != connections_.end() && it->isComplete() ? "connecting" : "incomplete");
        return;
    }

    SelectInstrumentDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto* chart = new ChartWindow(*it, connectionAdapter_.get(), dialog.selectedSymbol(), dialog.selectedExchange());
    chart->setAttribute(Qt::WA_DeleteOnClose, true);
    chartWindows_.push_back(chart);
    QObject::connect(chart, &QObject::destroyed, this, [this, chart] {
        chartWindows_.removeOne(chart);
    });
    chart->show();
    chart->raise();
    chart->activateWindow();
}

void MainWindow::openDom()
{
    const auto it = std::find_if(connections_.begin(), connections_.end(), [this](const FeedConnection& connection) {
        return connection.id == selectedConnectionId_;
    });
    if (it == connections_.end() || !it->isComplete() || !selectedConnectionReady()) {
        setConnectionState(it != connections_.end() && it->isComplete() ? "connecting" : "incomplete");
        return;
    }

    SelectInstrumentDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto* dom = new DomWindow(*it, connectionAdapter_.get(), dialog.selectedSymbol(), dialog.selectedExchange());
    dom->setAttribute(Qt::WA_DeleteOnClose, true);
    domWindows_.push_back(dom);
    QObject::connect(dom, &QObject::destroyed, this, [this, dom] {
        domWindows_.removeOne(dom);
    });
    dom->show();
    dom->raise();
    dom->activateWindow();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == toolbar_ || watched->objectName() == "brandLogo" || watched->objectName() == "brandGroup") {
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

void MainWindow::applyStyle()
{
    qApp->setStyleSheet(R"(
        QWidget#root {
            background: transparent;
            color: #eceff4;
            font-family: "Helvetica Neue";
            font-size: 12px;
        }
        QFrame#topToolbar {
            background: #10131a;
            border: 0;
            border-radius: 9px;
        }
        QFrame#brandGroup {
            background: transparent;
            border: 0;
        }
        QLabel#brandMark {
            background: #171d27;
            border: 1px solid #2c3542;
            border-radius: 6px;
            color: #7ce56f;
            font-size: 13px;
            font-weight: 900;
            qproperty-alignment: AlignCenter;
        }
        QLabel#brandLogo {
            color: #f4f6f9;
            font-size: 13px;
            font-weight: 900;
            letter-spacing: 0;
            padding-left: 0;
        }
        QPushButton#windowClose,
        QPushButton#windowMinimize {
            border: 0;
            border-radius: 5px;
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
        QToolButton#toolbarMenuButton,
        QPushButton#toolbarTextButton {
            background: transparent;
            border: 1px solid transparent;
            border-radius: 5px;
            color: #d6dbe5;
            padding: 0 20px 0 10px;
            font-weight: 800;
        }
        QToolButton#toolbarMenuButton:hover,
        QPushButton#toolbarTextButton:hover {
            background: #1a202b;
            border-color: #2f3948;
        }
        QToolButton#toolbarMenuButton::menu-indicator {
            image: url(:/icons/chevron-down.svg);
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 10px;
            height: 10px;
            right: 7px;
        }
        QLabel#toolbarLabel,
        QLabel#connectionStatus {
            color: #9ca6b6;
            font-weight: 800;
        }
        QFrame#connectionStatusPill {
            background: #171c25;
            border: 1px solid #2c3542;
            border-radius: 6px;
        }
        QFrame#connectionStatusPill[state="connecting"] {
            border-color: #866d2c;
            background: #201c12;
        }
        QFrame#connectionStatusPill[state="connected"] {
            border-color: #3d7f45;
            background: #132017;
        }
        QFrame#connectionStatusPill[state="incomplete"] {
            border-color: #7f3d45;
            background: #201316;
        }
        QFrame#connectionStatusPill[state="failed"] {
            border-color: #7f3d45;
            background: #201316;
        }
        QFrame#connectionStatusDot {
            border: 0;
            border-radius: 4px;
            background: #6d7786;
        }
        QFrame#connectionStatusDot[state="connecting"] {
            background: #f2c94c;
        }
        QFrame#connectionStatusDot[state="connected"] {
            background: #6fdc64;
        }
        QFrame#connectionStatusDot[state="incomplete"] {
            background: #ff7474;
        }
        QFrame#connectionStatusDot[state="failed"] {
            background: #ff7474;
        }
        QComboBox#workspaceSelector,
        QComboBox#connectionSelector {
            background: #181d27;
            border: 1px solid #2c3542;
            border-radius: 6px;
            color: #eef2f7;
            min-height: 28px;
            padding: 0 28px 0 10px;
            font-weight: 800;
        }
        QComboBox#workspaceSelector:hover,
        QComboBox#connectionSelector:hover {
            background: #1d2430;
            border-color: #3a4658;
        }
        QComboBox#workspaceSelector:focus,
        QComboBox#connectionSelector:focus {
            border-color: #6fdc64;
        }
        QComboBox::drop-down {
            border: 0;
            width: 24px;
        }
        QComboBox::down-arrow {
            image: url(:/icons/chevron-down.svg);
            width: 10px;
            height: 10px;
            right: 8px;
        }
        QMenu {
            background: #11151d;
            border: 1px solid #303847;
            border-radius: 8px;
            color: #edf1f7;
            padding: 6px;
        }
        QMenu::item {
            border-radius: 5px;
            padding: 8px 28px 8px 12px;
        }
        QMenu::item:selected {
            background: #202937;
        }
        QMenu::item:disabled {
            color: #646d7b;
        }
        QAbstractItemView {
            background: #11151d;
            border: 1px solid #303847;
            border-radius: 8px;
            color: #edf1f7;
            selection-background-color: #202937;
            selection-color: #ffffff;
            outline: 0;
        }
        QDialog {
            background: transparent;
            color: #e8ecf3;
            font-family: "Helvetica Neue";
            font-size: 12px;
        }
        QFrame#dialogShell {
            background: #0f1218;
            border: 1px solid #2a3240;
            border-radius: 9px;
        }
        QFrame#dialogTitleBar {
            background: #121721;
            border-bottom: 1px solid #252d3a;
            border-top-left-radius: 9px;
            border-top-right-radius: 9px;
        }
        QLabel#dialogLogo {
            background: #171d27;
            border: 1px solid #2c3542;
            color: #7ce56f;
            border-radius: 6px;
            font-size: 12px;
            font-weight: 900;
            qproperty-alignment: AlignCenter;
        }
        QLabel#dialogTitle {
            color: #eef2f7;
            font-size: 13px;
            font-weight: 900;
            letter-spacing: 0;
        }
        QFrame#dialogContent {
            background: #0f1218;
        }
        QFrame#connectionListPanel {
            border-right: 1px solid #242b36;
        }
        QLabel#sidePanelTitle {
            color: #7f8b9c;
            font-size: 10px;
            font-weight: 900;
            letter-spacing: 0;
            padding-left: 3px;
        }
        QListWidget#connectionList {
            background: #090d13;
            border: 1px solid #202733;
            border-radius: 7px;
            color: #dce2ec;
            outline: 0;
            padding: 4px;
            font-size: 12px;
            font-weight: 700;
        }
        QListWidget#connectionList::item {
            border-radius: 5px;
            min-height: 28px;
            padding: 5px 8px;
        }
        QListWidget#connectionList::item:selected {
            background: #1d2634;
            color: #ffffff;
        }
        QFrame#connectionFormPanel {
            background: #0f1218;
        }
        QScrollArea#settingsScrollArea {
            background: #0f1218;
            border: 0;
        }
        QScrollArea#settingsScrollArea QWidget#qt_scrollarea_viewport {
            background: #0f1218;
        }
        QScrollBar:vertical {
            background: transparent;
            border: 0;
            width: 8px;
            margin: 2px 0 2px 0;
        }
        QScrollBar::handle:vertical {
            background: #2f3948;
            border-radius: 4px;
            min-height: 28px;
        }
        QScrollBar::handle:vertical:hover {
            background: #435066;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical,
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: transparent;
            border: 0;
            height: 0;
        }
        QFrame#formColumn {
            background: transparent;
        }
        QLabel#sectionLabel {
            color: #f2f5f9;
            font-size: 14px;
            font-weight: 900;
            padding-top: 2px;
        }
        QLabel#formLabel {
            color: #9ea8b8;
            font-size: 12px;
            font-weight: 800;
        }
        QLineEdit,
        QComboBox {
            background: #171c25;
            border: 1px solid #2e3746;
            border-radius: 6px;
            color: #eef2f7;
            padding: 0 9px;
            font-size: 12px;
            font-weight: 700;
            selection-background-color: #3e994a;
        }
        QLineEdit:focus,
        QComboBox:focus {
            border: 1px solid #6fdc64;
            background: #1b222d;
        }
        QLineEdit:disabled,
        QComboBox:disabled {
            background: #12161d;
            color: #687385;
        }
        QFrame#formDivider {
            background: #222a36;
        }
        QFrame#rithmicInfoPanel {
            background: #111720;
            border: 1px solid #263140;
            border-radius: 8px;
        }
        QLabel#infoKicker {
            color: #6fdc64;
            font-size: 10px;
            font-weight: 900;
            letter-spacing: 0;
        }
        QLabel#infoTitle {
            color: #ffffff;
            font-size: 21px;
            font-weight: 900;
        }
        QLabel#infoSubTitle {
            color: #e9edf4;
            font-size: 12px;
            font-weight: 900;
        }
        QLabel#infoText {
            color: #a9b3c2;
            font-size: 12px;
            font-weight: 600;
            line-height: 145%;
        }
        QCheckBox#switchCheckBox::indicator {
            width: 34px;
            height: 18px;
            border-radius: 9px;
            background: #3c4451;
            border: 1px solid #515c6d;
        }
        QCheckBox#switchCheckBox::indicator:checked {
            background: #6fdc64;
            border: 1px solid #6fdc64;
        }
        QFrame#dialogFooter {
            background: #121721;
            border-top: 1px solid #252d3a;
            border-bottom-left-radius: 9px;
            border-bottom-right-radius: 9px;
        }
        QPushButton#addConnectionButton {
            background: #151b25;
            color: #e9edf4;
            border: 1px solid #334052;
            border-radius: 6px;
            font-size: 12px;
            font-weight: 900;
        }
        QPushButton#addConnectionButton:hover {
            background: #1c2430;
            border-color: #465366;
        }
        QPushButton#addConnectionButton:pressed {
            background: #121820;
            border-color: #56657a;
        }
        QPushButton#removeConnectionButton {
            background: #17161a;
            color: #ff8a8a;
            border: 1px solid #49313a;
            border-radius: 6px;
            font-size: 12px;
            font-weight: 900;
        }
        QPushButton#removeConnectionButton:hover {
            background: #251a20;
            border-color: #7d4551;
        }
        QPushButton#removeConnectionButton:pressed {
            background: #1e151a;
            border-color: #a44d5d;
        }
        QPushButton#removeConnectionButton:disabled {
            color: #684c52;
            border-color: #3a2930;
            background: #17171a;
        }
        QPushButton#saveConnectionButton {
            background: #7fd875;
            color: #071107;
            border: 1px solid #7fd875;
            border-radius: 6px;
            font-size: 12px;
            font-weight: 900;
        }
        QPushButton#saveConnectionButton:hover {
            background: #8fec83;
            border-color: #8fec83;
        }
        QPushButton#saveConnectionButton:pressed {
            background: #6fc965;
            border-color: #6fc965;
        }
        QPushButton#secondaryActionButton,
        QPushButton#cancelButton {
            background: #141a23;
            color: #e9edf4;
            border: 1px solid #303b4d;
            border-radius: 6px;
            font-size: 12px;
            font-weight: 900;
        }
        QPushButton#secondaryActionButton:hover,
        QPushButton#cancelButton:hover {
            background: #1c2430;
            border-color: #465366;
        }
        QPushButton#secondaryActionButton:pressed,
        QPushButton#cancelButton:pressed {
            background: #111720;
            border-color: #56657a;
        }
        QFrame#testDialogShell {
            background: #10141b;
            border: 1px solid #2c3543;
            border-radius: 10px;
        }
        QFrame#testDialogTitleBar {
            background: #141922;
            border-bottom: 1px solid #262f3c;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
        }
        QLabel#testDialogTitle {
            color: #e9edf4;
            font-size: 12px;
            font-weight: 900;
        }
        QFrame#testDialogBody,
        QFrame#testDialogCopy {
            background: transparent;
        }
        QFrame#connectionStateGlyph {
            border-radius: 24px;
            border: 1px solid #3d4655;
            background: #171d27;
        }
        QFrame#connectionStateGlyph[state="connecting"] {
            border: 1px solid #4d86ff;
            background: #152135;
        }
        QFrame#connectionStateGlyph[state="success"] {
            border: 1px solid #6fdc64;
            background: #132415;
        }
        QFrame#connectionStateGlyph[state="error"] {
            border: 1px solid #f06c6c;
            background: #2a171a;
        }
        QLabel#connectionStateGlyphText {
            color: #f4f8ff;
            font-size: 12px;
            font-weight: 900;
        }
        QLabel#testDialogStateTitle {
            color: #ffffff;
            font-size: 20px;
            font-weight: 900;
        }
        QLabel#testDialogStateDetail {
            color: #aeb8c7;
            font-size: 12px;
            font-weight: 700;
        }
        QProgressBar#testProgress {
            background: #161c26;
            border: 1px solid #2e3746;
            border-radius: 3px;
        }
        QProgressBar#testProgress::chunk {
            background: #6fdc64;
            border-radius: 3px;
        }
        QFrame#instrumentDialogShell {
            background: #0f1218;
            border: 0;
            border-radius: 10px;
        }
        QFrame#instrumentTitleBar {
            background: #121721;
            border-bottom: 1px solid #252d3a;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
        }
        QLabel#instrumentDialogTitle {
            color: #eef2f7;
            font-size: 13px;
            font-weight: 900;
        }
        QFrame#instrumentContent {
            background: #0f1218;
        }
        QFrame#instrumentSidebar {
            border-right: 1px solid #242b36;
        }
        QFrame#instrumentBody,
        QFrame#instrumentSearchRow {
            background: transparent;
        }
        QListWidget#exchangeList {
            background: #090d13;
            border: 1px solid #202733;
            border-radius: 7px;
            color: #dce2ec;
            outline: 0;
            padding: 4px;
            font-size: 12px;
            font-weight: 800;
        }
        QListWidget#exchangeList::item {
            border-radius: 5px;
            min-height: 30px;
            padding: 5px 8px;
        }
        QListWidget#exchangeList::item:selected {
            background: #1d2634;
            color: #ffffff;
        }
        QLineEdit#instrumentSearch {
            background: #171c25;
            border: 1px solid #2e3746;
            border-radius: 6px;
            color: #eef2f7;
            padding: 0 10px;
            font-size: 12px;
            font-weight: 800;
            selection-background-color: #3e994a;
        }
        QLineEdit#instrumentSearch:focus {
            border-color: #6fdc64;
            background: #1b222d;
        }
        QLabel#instrumentResultCount {
            color: #8f9bab;
            font-size: 11px;
            font-weight: 900;
        }
        QTableWidget#symbolTable {
            background: #090d13;
            border: 1px solid #202733;
            border-radius: 7px;
            color: #dce2ec;
            outline: 0;
            font-size: 12px;
            font-weight: 800;
            selection-background-color: #1f2a39;
            selection-color: #ffffff;
        }
        QTableWidget#symbolTable::item {
            border: 0;
            padding-left: 10px;
        }
        QTableWidget#symbolTable::item:selected {
            background: #1f2a39;
            color: #ffffff;
        }
        QHeaderView::section {
            background: #111720;
            border: 0;
            border-bottom: 1px solid #253040;
            color: #aab4c3;
            font-size: 11px;
            font-weight: 900;
            padding: 7px 10px;
        }
        QFrame#instrumentFooter {
            background: #121721;
            border-top: 1px solid #252d3a;
            border-bottom-left-radius: 10px;
            border-bottom-right-radius: 10px;
        }
    )");
}

} // namespace tc
