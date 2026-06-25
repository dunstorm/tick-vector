#include "ui/FeedSettingsDialog.hpp"

#include "app/AppConstants.hpp"
#include "ui/ConnectionTestDialog.hpp"

#include <QtCore/QUuid>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGraphicsDropShadowEffect>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <utility>

namespace tc {

namespace {

FeedConnection makeBlankConnection()
{
    FeedConnection connection;
    connection.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    connection.name = "Rithmic";
    connection.workspaceName = "Lucid Trading";
    connection.workspaceLocation = "Local";
    connection.feedSource = "Rithmic";
    connection.gateway = "rituz00100.rithmic.com:443";
    connection.system = "Rithmic Test";
    connection.marketData = "Non Aggregated";
    connection.appName = app::kRithmicAppName;
    return connection;
}

QLabel* makeLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName("formLabel");
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setMinimumWidth(112);
    return label;
}

QLabel* makeSectionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName("sectionLabel");
    return label;
}

QLineEdit* makeLineEdit(QWidget* parent)
{
    auto* line = new QLineEdit(parent);
    line->setFixedHeight(30);
    return line;
}

QComboBox* makeComboBox(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->setFixedHeight(30);
    return combo;
}

QFrame* makeDivider(QWidget* parent)
{
    auto* divider = new QFrame(parent);
    divider->setObjectName("formDivider");
    divider->setFixedHeight(1);
    return divider;
}

void setComboValue(QComboBox* combo, const QString& value, const QString& fallback)
{
    const QString selected = value.trimmed().isEmpty() ? fallback : value.trimmed();
    if (combo->findText(selected) < 0) {
        combo->addItem(selected);
    }
    combo->setCurrentText(selected);
}

QPushButton* makeWindowControl(const QString& objectName, QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setObjectName(objectName);
    button->setFixedSize(12, 12);
    button->setCursor(Qt::ArrowCursor);
    return button;
}

} // namespace

FeedSettingsDialog::FeedSettingsDialog(QVector<FeedConnection> connections, QWidget* parent)
    : QDialog(parent)
    , connections_(std::move(connections))
{
    setWindowTitle("Edit Workspace");
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedSize(960, 580);

    if (connections_.isEmpty()) {
        connections_.push_back(makeBlankConnection());
    }

    auto* shell = new QFrame(this);
    shell->setObjectName("dialogShell");
    auto* shadow = new QGraphicsDropShadowEffect(shell);
    shadow->setBlurRadius(30);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 160));
    shell->setGraphicsEffect(shadow);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(0);
    root->addWidget(shell);

    auto* shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    titleBar_ = new QFrame(shell);
    titleBar_->setObjectName("dialogTitleBar");
    titleBar_->setFixedHeight(44);
    titleBar_->installEventFilter(this);
    auto* titleLayout = new QHBoxLayout(titleBar_);
    titleLayout->setContentsMargins(14, 0, 14, 0);
    titleLayout->setSpacing(8);

    auto* close = makeWindowControl("windowClose", titleBar_);
    auto* minimize = makeWindowControl("windowMinimize", titleBar_);
    QObject::connect(close, &QPushButton::clicked, this, &QWidget::close);
    QObject::connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);
    titleLayout->addWidget(close);
    titleLayout->addWidget(minimize);

    auto* logo = new QLabel("T", titleBar_);
    logo->setObjectName("dialogLogo");
    logo->setFixedSize(22, 22);
    title_ = new QLabel("Edit Workspace", titleBar_);
    title_->setObjectName("dialogTitle");
    title_->installEventFilter(this);
    titleLayout->addWidget(logo);
    titleLayout->addWidget(title_);
    titleLayout->addStretch();
    shellLayout->addWidget(titleBar_);

    auto* content = new QFrame(shell);
    content->setObjectName("dialogContent");
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(16, 16, 16, 0);
    contentLayout->setSpacing(16);

    auto* leftPanel = new QFrame(content);
    leftPanel->setObjectName("connectionListPanel");
    leftPanel->setFixedWidth(172);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 12, 0);
    leftLayout->setSpacing(8);

    auto* listTitle = new QLabel("Connections", leftPanel);
    listTitle->setObjectName("sidePanelTitle");
    leftLayout->addWidget(listTitle);
    connectionList_ = new QListWidget(leftPanel);
    connectionList_->setObjectName("connectionList");
    leftLayout->addWidget(connectionList_, 1);

    auto* formScroll = new QScrollArea(content);
    formScroll->setObjectName("settingsScrollArea");
    formScroll->setWidgetResizable(true);
    formScroll->setFrameShape(QFrame::NoFrame);
    formScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    formScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* formPanel = new QFrame(formScroll);
    formPanel->setObjectName("connectionFormPanel");
    auto* formPanelLayout = new QHBoxLayout(formPanel);
    formPanelLayout->setContentsMargins(0, 0, 0, 0);
    formPanelLayout->setSpacing(18);

    auto* formColumn = new QFrame(formPanel);
    formColumn->setObjectName("formColumn");
    auto* formLayout = new QGridLayout(formColumn);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setHorizontalSpacing(14);
    formLayout->setVerticalSpacing(8);
    formLayout->setColumnStretch(0, 0);
    formLayout->setColumnStretch(1, 1);

    workspaceName_ = makeLineEdit(formColumn);
    workspaceLocation_ = makeComboBox(formColumn);
    workspaceLocation_->addItem("Local");
    name_ = makeLineEdit(formColumn);
    feedSource_ = makeComboBox(formColumn);
    feedSource_->addItem("Rithmic");
    gatewaySelector_ = makeComboBox(formColumn);
    gatewaySelector_->setEditable(true);
    gatewaySelector_->addItems({"rituz00100.rithmic.com:443", "Rithmic Test", "Chicago", "Aurora", "Europe", "Asia"});
    systemSelector_ = makeComboBox(formColumn);
    systemSelector_->setEditable(true);
    systemSelector_->addItems({"Lucid Trading", "Rithmic Paper Trading", "Rithmic Test", "Apex", "Topstep"});
    marketData_ = makeComboBox(formColumn);
    marketData_->addItems({"Non Aggregated", "Aggregated"});
    username_ = makeLineEdit(formColumn);
    password_ = makeLineEdit(formColumn);
    password_->setEchoMode(QLineEdit::Password);
    connectOnStartup_ = new QCheckBox(formColumn);
    connectOnStartup_->setObjectName("switchCheckBox");

    int row = 0;
    formLayout->addWidget(makeSectionLabel("General Information", formColumn), row++, 0, 1, 2);
    formLayout->addWidget(makeDivider(formColumn), row++, 0, 1, 2);
    formLayout->addWidget(makeLabel("Workspace Name", formColumn), row, 0);
    formLayout->addWidget(workspaceName_, row++, 1);
    formLayout->addWidget(makeLabel("Location", formColumn), row, 0);
    formLayout->addWidget(workspaceLocation_, row++, 1);

    formLayout->setRowMinimumHeight(row++, 8);
    formLayout->addWidget(makeSectionLabel("Connection Information", formColumn), row++, 0, 1, 2);
    formLayout->addWidget(makeDivider(formColumn), row++, 0, 1, 2);
    formLayout->addWidget(makeLabel("Connection Name", formColumn), row, 0);
    formLayout->addWidget(name_, row++, 1);
    formLayout->addWidget(makeLabel("Service", formColumn), row, 0);
    formLayout->addWidget(feedSource_, row++, 1);
    formLayout->addWidget(makeLabel("Username", formColumn), row, 0);
    formLayout->addWidget(username_, row++, 1);
    formLayout->addWidget(makeLabel("Password", formColumn), row, 0);
    formLayout->addWidget(password_, row++, 1);
    formLayout->addWidget(makeLabel("Gateway URL", formColumn), row, 0);
    formLayout->addWidget(gatewaySelector_, row++, 1);
    formLayout->addWidget(makeLabel("System", formColumn), row, 0);
    formLayout->addWidget(systemSelector_, row++, 1);
    formLayout->addWidget(makeLabel("Market Data", formColumn), row, 0);
    formLayout->addWidget(marketData_, row++, 1);
    formLayout->addWidget(makeLabel("Connect on Startup", formColumn), row, 0);
    formLayout->addWidget(connectOnStartup_, row++, 1, Qt::AlignLeft);

    auto* actionRow = new QFrame(formColumn);
    actionRow->setObjectName("inlineActions");
    auto* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    auto* test = new QPushButton("Test Connection", actionRow);
    test->setObjectName("secondaryActionButton");
    auto* dataService = new QPushButton("Data Service", actionRow);
    dataService->setObjectName("secondaryActionButton");
    test->setFixedSize(124, 30);
    dataService->setFixedSize(108, 30);
    test->setCursor(Qt::PointingHandCursor);
    dataService->setCursor(Qt::PointingHandCursor);
    actionLayout->addWidget(test);
    actionLayout->addWidget(dataService);
    actionLayout->addStretch();
    formLayout->addWidget(actionRow, row++, 1);
    formLayout->setRowStretch(row, 1);

    auto* infoPanel = new QFrame(formPanel);
    infoPanel->setObjectName("rithmicInfoPanel");
    infoPanel->setFixedWidth(248);
    auto* infoLayout = new QVBoxLayout(infoPanel);
    infoLayout->setContentsMargins(18, 18, 18, 18);
    infoLayout->setSpacing(10);

    auto* infoKicker = new QLabel("FEED SOURCE", infoPanel);
    infoKicker->setObjectName("infoKicker");
    auto* infoTitle = new QLabel("Rithmic", infoPanel);
    infoTitle->setObjectName("infoTitle");
    auto* infoText = new QLabel("Direct market-data and order-routing connector for futures broker and evaluation accounts.", infoPanel);
    infoText->setObjectName("infoText");
    infoText->setWordWrap(true);
    auto* storageText = new QLabel("Development credentials are saved in a local plaintext JSON profile. Gateway, system and market-data mode are stored with each connection.", infoPanel);
    storageText->setObjectName("infoText");
    storageText->setWordWrap(true);
    auto* routeTitle = new QLabel("Routing", infoPanel);
    routeTitle->setObjectName("infoSubTitle");
    auto* routeText = new QLabel("Gateway should be the Rithmic Protocol WebSocket host. System should match the broker or funding-provider environment assigned to the login.", infoPanel);
    routeText->setObjectName("infoText");
    routeText->setWordWrap(true);
    infoLayout->addWidget(infoKicker);
    infoLayout->addWidget(infoTitle);
    infoLayout->addWidget(infoText);
    infoLayout->addWidget(makeDivider(infoPanel));
    infoLayout->addWidget(storageText);
    infoLayout->addSpacing(4);
    infoLayout->addWidget(routeTitle);
    infoLayout->addWidget(routeText);
    infoLayout->addStretch();

    formPanelLayout->addWidget(formColumn, 1);
    formPanelLayout->addWidget(infoPanel);

    formScroll->setWidget(formPanel);

    contentLayout->addWidget(leftPanel);
    contentLayout->addWidget(formScroll, 1);
    shellLayout->addWidget(content, 1);

    auto* footer = new QFrame(shell);
    footer->setObjectName("dialogFooter");
    footer->setFixedHeight(58);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 0, 20, 0);
    footerLayout->setSpacing(8);

    auto* addButton = new QPushButton("Add Connection", footer);
    addButton->setObjectName("addConnectionButton");
    addButton->setFixedSize(132, 30);
    removeButton_ = new QPushButton("Remove", footer);
    removeButton_->setObjectName("removeConnectionButton");
    removeButton_->setFixedSize(88, 30);
    saveButton_ = new QPushButton("Update Workspace", footer);
    saveButton_->setObjectName("saveConnectionButton");
    saveButton_->setFixedSize(142, 30);
    auto* cancelButton = new QPushButton("Cancel", footer);
    cancelButton->setObjectName("cancelButton");
    cancelButton->setFixedSize(84, 30);
    for (auto* button : {addButton, removeButton_, saveButton_, cancelButton}) {
        button->setCursor(Qt::PointingHandCursor);
    }
    footerLayout->addWidget(addButton);
    footerLayout->addStretch();
    footerLayout->addWidget(removeButton_);
    footerLayout->addWidget(saveButton_);
    footerLayout->addWidget(cancelButton);
    shellLayout->addWidget(footer);

    QObject::connect(connectionList_, &QListWidget::currentRowChanged, this, [this](int row) { selectConnection(row); });
    QObject::connect(addButton, &QPushButton::clicked, this, [this] { addConnection(); });
    QObject::connect(removeButton_, &QPushButton::clicked, this, [this] { removeSelectedConnection(); });
    QObject::connect(test, &QPushButton::clicked, this, [this] { testCurrentConnection(); });
    QObject::connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    QObject::connect(saveButton_, &QPushButton::clicked, this, [this] {
        saveCurrentForm();
        accept();
    });

    rebuildConnectionList();
    connectionList_->setCurrentRow(0);
    name_->setFocus();
    name_->selectAll();
}

bool FeedSettingsDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == titleBar_ || watched == title_) {
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
    return QDialog::eventFilter(watched, event);
}

QVector<FeedConnection> FeedSettingsDialog::connections() const
{
    return connections_;
}

void FeedSettingsDialog::rebuildConnectionList()
{
    connectionList_->clear();
    for (const auto& connection : connections_) {
        connectionList_->addItem(connection.name.trimmed().isEmpty() ? "Unnamed Rithmic" : connection.name);
    }
    removeButton_->setEnabled(selectedIndex_ >= 0 && selectedIndex_ < connections_.size());
}

void FeedSettingsDialog::selectConnection(int index)
{
    if (index == selectedIndex_) {
        return;
    }

    saveCurrentForm();
    selectedIndex_ = index;
    loadSelectedConnection();
}

void FeedSettingsDialog::addConnection()
{
    saveCurrentForm();
    connections_.push_back(makeBlankConnection());
    selectedIndex_ = connections_.size() - 1;
    rebuildConnectionList();
    connectionList_->setCurrentRow(selectedIndex_);
    loadSelectedConnection();
    name_->setFocus();
    name_->selectAll();
}

void FeedSettingsDialog::removeSelectedConnection()
{
    if (selectedIndex_ < 0 || selectedIndex_ >= connections_.size()) {
        return;
    }

    connections_.removeAt(selectedIndex_);
    selectedIndex_ = std::min(selectedIndex_, static_cast<int>(connections_.size()) - 1);
    rebuildConnectionList();
    if (selectedIndex_ >= 0) {
        connectionList_->setCurrentRow(selectedIndex_);
        loadSelectedConnection();
    } else {
        setFormEnabled(false);
    }
}

void FeedSettingsDialog::testCurrentConnection()
{
    saveCurrentForm();
    if (selectedIndex_ < 0 || selectedIndex_ >= connections_.size()) {
        return;
    }

    const auto& connection = connections_[selectedIndex_];
    QStringList missing;
    if (connection.username.trimmed().isEmpty()) {
        missing << "Username";
    }
    if (connection.password.isEmpty()) {
        missing << "Password";
    }
    if (connection.gateway.trimmed().isEmpty()) {
        missing << "Gateway URL";
    }
    if (connection.system.trimmed().isEmpty()) {
        missing << "System";
    }

    ConnectionTestDialog dialog(connection, missing, this);
    dialog.exec();
}

void FeedSettingsDialog::saveCurrentForm()
{
    if (selectedIndex_ < 0 || selectedIndex_ >= connections_.size()) {
        return;
    }

    auto& connection = connections_[selectedIndex_];
    connection.workspaceName = workspaceName_->text().trimmed().isEmpty() ? "Lucid Trading" : workspaceName_->text().trimmed();
    connection.workspaceLocation = workspaceLocation_->currentText();
    connection.name = name_->text().trimmed();
    connection.feedSource = feedSource_->currentText();
    connection.gateway = gatewaySelector_->currentText().trimmed();
    connection.server = connection.gateway;
    connection.system = systemSelector_->currentText().trimmed();
    connection.marketData = marketData_->currentText();
    connection.username = username_->text().trimmed();
    connection.password = password_->text();
    connection.connectOnStartup = connectOnStartup_->isChecked();

    if (connection.name.isEmpty()) {
        connection.name = "Rithmic";
    }

    auto* item = connectionList_->item(selectedIndex_);
    if (item) {
        item->setText(connection.name);
    }
}

void FeedSettingsDialog::loadSelectedConnection()
{
    const bool valid = selectedIndex_ >= 0 && selectedIndex_ < connections_.size();
    setFormEnabled(valid);
    if (!valid) {
        return;
    }

    const auto& connection = connections_[selectedIndex_];
    workspaceName_->setText(connection.workspaceName.trimmed().isEmpty() ? "Lucid Trading" : connection.workspaceName);
    setComboValue(workspaceLocation_, connection.workspaceLocation, "Local");
    name_->setText(connection.name);
    setComboValue(feedSource_, connection.feedSource, "Rithmic");
    setComboValue(gatewaySelector_, connection.gateway, "rituz00100.rithmic.com:443");
    setComboValue(systemSelector_, connection.system, "Rithmic Test");
    setComboValue(marketData_, connection.marketData, "Non Aggregated");
    username_->setText(connection.username);
    password_->setText(connection.password);
    connectOnStartup_->setChecked(connection.connectOnStartup);
}

void FeedSettingsDialog::setFormEnabled(bool enabled)
{
    for (auto* widget : {workspaceName_, name_, username_, password_}) {
        widget->setEnabled(enabled);
        if (!enabled) {
            widget->clear();
        }
    }
    workspaceLocation_->setEnabled(enabled);
    feedSource_->setEnabled(enabled);
    gatewaySelector_->setEnabled(enabled);
    systemSelector_->setEnabled(enabled);
    marketData_->setEnabled(enabled);
    connectOnStartup_->setEnabled(enabled);
    if (!enabled) {
        connectOnStartup_->setChecked(false);
    }
    removeButton_->setEnabled(enabled);
}

} // namespace tc
