#include "ui/SelectInstrumentDialog.hpp"

#include <QtCore/QSize>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>

namespace tc {

namespace {

struct InstrumentDefinition {
    QString exchange;
    QString symbol;
    QString name;
};

const QVector<InstrumentDefinition>& instruments()
{
    static const QVector<InstrumentDefinition> items{
        {"CME", "MNQ", "Micro E-mini Nasdaq 100 - auto best contract"},
        {"CME", "NQ", "E-mini Nasdaq 100 - auto best contract"},
        {"CME", "MES", "Micro E-mini S&P 500 - auto best contract"},
        {"CME", "ES", "E-mini S&P 500 - auto best contract"},
        {"CME", "M2K", "Micro E-mini Russell 2000 - auto best contract"},
        {"CME", "RTY", "E-mini Russell 2000 - auto best contract"},
        {"CME", "6E", "Euro FX - auto best contract"},
        {"CME", "6J", "Japanese Yen - auto best contract"},
        {"COMEX", "MGC", "Micro Gold - auto best contract"},
        {"COMEX", "GC", "Gold - auto best contract"},
        {"COMEX", "SI", "Silver - auto best contract"},
        {"COMEX", "HG", "Copper - auto best contract"},
        {"NYMEX", "CL", "Crude Oil - auto best contract"},
        {"NYMEX", "NG", "Natural Gas - auto best contract"},
        {"CBOT", "ZN", "10-Year T-Note - auto best contract"},
        {"CBOT", "ZB", "30-Year T-Bond - auto best contract"},
        {"CBOT", "ZF", "5-Year T-Note - auto best contract"},
        {"CBOT", "ZC", "Corn - auto best contract"},
        {"CBOT", "ZS", "Soybeans - auto best contract"},
        {"CBOT", "ZW", "Wheat - auto best contract"},
        {"EUREX", "FDAX", "DAX - auto best contract"},
        {"EUREX", "FESX", "Euro Stoxx 50 - auto best contract"},
        {"EUREX", "FGBL", "Euro-Bund - auto best contract"},
    };
    return items;
}

QPushButton* makeWindowControl(const QString& objectName, QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setObjectName(objectName);
    button->setFixedSize(12, 12);
    button->setCursor(Qt::ArrowCursor);
    return button;
}

QStringList exchangeNames()
{
    QStringList names{"All"};
    for (const auto& instrument : instruments()) {
        if (!names.contains(instrument.exchange)) {
            names << instrument.exchange;
        }
    }
    return names;
}

} // namespace

SelectInstrumentDialog::SelectInstrumentDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Select Instrument");
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedSize(760, 500);

    auto* shell = new QFrame(this);
    shell->setObjectName("instrumentDialogShell");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(shell);

    auto* shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    titleBar_ = new QFrame(shell);
    titleBar_->setObjectName("instrumentTitleBar");
    titleBar_->setFixedHeight(42);
    titleBar_->installEventFilter(this);
    auto* titleLayout = new QHBoxLayout(titleBar_);
    titleLayout->setContentsMargins(14, 0, 14, 0);
    titleLayout->setSpacing(8);

    auto* close = makeWindowControl("windowClose", titleBar_);
    auto* minimize = makeWindowControl("windowMinimize", titleBar_);
    minimize->setEnabled(false);
    QObject::connect(close, &QPushButton::clicked, this, &QDialog::reject);
    title_ = new QLabel("Select Instrument", titleBar_);
    title_->setObjectName("instrumentDialogTitle");
    title_->installEventFilter(this);
    titleLayout->addWidget(close);
    titleLayout->addWidget(minimize);
    titleLayout->addSpacing(6);
    titleLayout->addWidget(title_);
    titleLayout->addStretch();
    shellLayout->addWidget(titleBar_);

    auto* content = new QFrame(shell);
    content->setObjectName("instrumentContent");
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(16, 16, 16, 0);
    contentLayout->setSpacing(16);

    auto* sidebar = new QFrame(content);
    sidebar->setObjectName("instrumentSidebar");
    sidebar->setFixedWidth(176);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 12, 0);
    sidebarLayout->setSpacing(8);
    auto* sidebarTitle = new QLabel("Exchanges", sidebar);
    sidebarTitle->setObjectName("sidePanelTitle");
    exchangeList_ = new QListWidget(sidebar);
    exchangeList_->setObjectName("exchangeList");
    sidebarLayout->addWidget(sidebarTitle);
    sidebarLayout->addWidget(exchangeList_, 1);

    auto* body = new QFrame(content);
    body->setObjectName("instrumentBody");
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(10);

    auto* searchRow = new QFrame(body);
    searchRow->setObjectName("instrumentSearchRow");
    auto* searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(10);
    search_ = new QLineEdit(searchRow);
    search_->setObjectName("instrumentSearch");
    search_->setPlaceholderText("Search symbol or name");
    search_->setFixedHeight(32);
    resultCount_ = new QLabel(searchRow);
    resultCount_->setObjectName("instrumentResultCount");
    resultCount_->setFixedWidth(108);
    resultCount_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    searchLayout->addWidget(search_, 1);
    searchLayout->addWidget(resultCount_);

    symbolTable_ = new QTableWidget(body);
    symbolTable_->setObjectName("symbolTable");
    symbolTable_->setColumnCount(3);
    symbolTable_->setHorizontalHeaderLabels({"Symbol", "Description", "Exchange"});
    symbolTable_->verticalHeader()->hide();
    symbolTable_->setShowGrid(false);
    symbolTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    symbolTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    symbolTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    symbolTable_->setAlternatingRowColors(false);
    symbolTable_->horizontalHeader()->setStretchLastSection(false);
    symbolTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    symbolTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    symbolTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    symbolTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    symbolTable_->horizontalHeader()->setHighlightSections(false);
    symbolTable_->horizontalHeader()->setSectionsClickable(false);
    symbolTable_->setColumnWidth(0, 108);
    symbolTable_->setColumnWidth(2, 112);

    bodyLayout->addWidget(searchRow);
    bodyLayout->addWidget(symbolTable_, 1);

    contentLayout->addWidget(sidebar);
    contentLayout->addWidget(body, 1);
    shellLayout->addWidget(content, 1);

    auto* footer = new QFrame(shell);
    footer->setObjectName("instrumentFooter");
    footer->setFixedHeight(58);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(20, 0, 20, 0);
    footerLayout->setSpacing(8);
    auto* cancel = new QPushButton("Cancel", footer);
    cancel->setObjectName("cancelButton");
    cancel->setFixedSize(84, 30);
    openButton_ = new QPushButton("Open Chart", footer);
    openButton_->setObjectName("saveConnectionButton");
    openButton_->setFixedSize(112, 30);
    openButton_->setEnabled(false);
    for (auto* button : {cancel, openButton_}) {
        button->setCursor(Qt::PointingHandCursor);
    }
    footerLayout->addStretch();
    footerLayout->addWidget(cancel);
    footerLayout->addWidget(openButton_);
    shellLayout->addWidget(footer);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(openButton_, &QPushButton::clicked, this, [this] { acceptSelectedSymbol(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { rebuildSymbols(); });
    connect(exchangeList_, &QListWidget::currentTextChanged, this, [this](const QString& exchange) { setSelectedExchange(exchange); });
    connect(symbolTable_, &QTableWidget::currentCellChanged, this, [this](int row) { openButton_->setEnabled(row >= 0); });
    connect(symbolTable_, &QTableWidget::cellDoubleClicked, this, [this] { acceptSelectedSymbol(); });

    rebuildExchanges();
    setSelectedExchange("COMEX");
    search_->setFocus();
}

QString SelectInstrumentDialog::selectedExchange() const
{
    return selectedExchange_;
}

QString SelectInstrumentDialog::selectedSymbol() const
{
    return selectedSymbol_;
}

QString SelectInstrumentDialog::selectedName() const
{
    return selectedName_;
}

bool SelectInstrumentDialog::eventFilter(QObject* watched, QEvent* event)
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

void SelectInstrumentDialog::rebuildExchanges()
{
    exchangeList_->clear();
    for (const auto& exchange : exchangeNames()) {
        exchangeList_->addItem(exchange);
    }
}

void SelectInstrumentDialog::setSelectedExchange(const QString& exchange)
{
    selectedExchange_ = exchange.trimmed().isEmpty() ? "All" : exchange;
    const auto items = exchangeList_->findItems(selectedExchange_, Qt::MatchExactly);
    if (!items.isEmpty()) {
        exchangeList_->setCurrentItem(items.first());
    }
    rebuildSymbols();
}

void SelectInstrumentDialog::rebuildSymbols()
{
    symbolTable_->setRowCount(0);
    const QString query = search_->text().trimmed();
    int matches = 0;
    int preferredRow = -1;

    for (const auto& instrument : instruments()) {
        const bool exchangeMatch = selectedExchange_ == "All" || instrument.exchange == selectedExchange_;
        const bool queryMatch = query.isEmpty()
            || instrument.symbol.contains(query, Qt::CaseInsensitive)
            || instrument.name.contains(query, Qt::CaseInsensitive)
            || instrument.exchange.contains(query, Qt::CaseInsensitive);
        if (!exchangeMatch || !queryMatch) {
            continue;
        }

        const int row = symbolTable_->rowCount();
        symbolTable_->insertRow(row);

        auto* symbol = new QTableWidgetItem(instrument.symbol);
        symbol->setData(Qt::UserRole, instrument.exchange);
        symbol->setData(Qt::UserRole + 1, instrument.symbol);
        symbol->setData(Qt::UserRole + 2, instrument.name);
        auto* description = new QTableWidgetItem(instrument.name);
        auto* exchange = new QTableWidgetItem(instrument.exchange);
        for (auto* cell : {symbol, description, exchange}) {
            cell->setFlags(cell->flags() & ~Qt::ItemIsEditable);
            cell->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
        symbolTable_->setItem(row, 0, symbol);
        symbolTable_->setItem(row, 1, description);
        symbolTable_->setItem(row, 2, exchange);
        symbolTable_->setRowHeight(row, 34);
        if (preferredRow < 0 && query.isEmpty() && instrument.exchange == "COMEX" && instrument.symbol == "GC") {
            preferredRow = row;
        }
        ++matches;
    }

    resultCount_->setText(QString::number(matches) + (matches == 1 ? " symbol" : " symbols"));
    if (matches > 0) {
        symbolTable_->setCurrentCell(preferredRow >= 0 ? preferredRow : 0, 0);
    } else {
        symbolTable_->clearSelection();
        symbolTable_->setCurrentCell(-1, -1);
    }
    openButton_->setEnabled(matches > 0);
}

void SelectInstrumentDialog::acceptSelectedSymbol()
{
    auto* item = symbolTable_->item(symbolTable_->currentRow(), 0);
    if (!item) {
        return;
    }
    selectedExchange_ = item->data(Qt::UserRole).toString();
    selectedSymbol_ = item->data(Qt::UserRole + 1).toString();
    selectedName_ = item->data(Qt::UserRole + 2).toString();
    accept();
}

} // namespace tc
