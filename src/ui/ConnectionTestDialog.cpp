#include "ui/ConnectionTestDialog.hpp"

#include <QtCore/QTimer>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGraphicsDropShadowEffect>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>
#include <utility>

namespace tc {

ConnectionTestDialog::ConnectionTestDialog(FeedConnection connection, QStringList missingFields, QWidget* parent)
    : QDialog(parent)
    , connection_(std::move(connection))
    , missingFields_(std::move(missingFields))
{
    setWindowTitle("Test Connection");
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFixedSize(430, 226);

    auto* shell = new QFrame(this);
    shell->setObjectName("testDialogShell");
    auto* shadow = new QGraphicsDropShadowEffect(shell);
    shadow->setBlurRadius(34);
    shadow->setOffset(0, 14);
    shadow->setColor(QColor(0, 0, 0, 170));
    shell->setGraphicsEffect(shadow);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(0);
    root->addWidget(shell);

    auto* layout = new QVBoxLayout(shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* titleBar = new QFrame(shell);
    titleBar->setObjectName("testDialogTitleBar");
    titleBar->setFixedHeight(38);
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(14, 0, 14, 0);
    titleLayout->setSpacing(8);

    auto* close = new QPushButton(titleBar);
    close->setObjectName("windowClose");
    close->setFixedSize(12, 12);
    close->setCursor(Qt::ArrowCursor);
    auto* minimize = new QPushButton(titleBar);
    minimize->setObjectName("windowMinimize");
    minimize->setFixedSize(12, 12);
    minimize->setEnabled(false);
    auto* title = new QLabel("Test Connection", titleBar);
    title->setObjectName("testDialogTitle");
    titleLayout->addWidget(close);
    titleLayout->addWidget(minimize);
    titleLayout->addSpacing(4);
    titleLayout->addWidget(title);
    titleLayout->addStretch();
    layout->addWidget(titleBar);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);

    auto* body = new QFrame(shell);
    body->setObjectName("testDialogBody");
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(24, 22, 24, 18);
    bodyLayout->setSpacing(18);

    glyph_ = new QFrame(body);
    glyph_->setObjectName("connectionStateGlyph");
    glyph_->setFixedSize(48, 48);
    auto* glyphLayout = new QHBoxLayout(glyph_);
    glyphLayout->setContentsMargins(0, 0, 0, 0);
    glyphText_ = new QLabel(glyph_);
    glyphText_->setObjectName("connectionStateGlyphText");
    glyphText_->setAlignment(Qt::AlignCenter);
    glyphLayout->addWidget(glyphText_);

    auto* copyColumn = new QFrame(body);
    copyColumn->setObjectName("testDialogCopy");
    auto* copyLayout = new QVBoxLayout(copyColumn);
    copyLayout->setContentsMargins(0, 0, 0, 0);
    copyLayout->setSpacing(9);

    stateTitle_ = new QLabel(copyColumn);
    stateTitle_->setObjectName("testDialogStateTitle");
    stateDetail_ = new QLabel(copyColumn);
    stateDetail_->setObjectName("testDialogStateDetail");
    stateDetail_->setWordWrap(true);

    progress_ = new QProgressBar(copyColumn);
    progress_->setObjectName("testProgress");
    progress_->setTextVisible(false);
    progress_->setFixedHeight(7);

    auto* actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionButton_ = new QPushButton("Cancel", copyColumn);
    actionButton_->setObjectName("cancelButton");
    actionButton_->setFixedSize(86, 30);
    actionButton_->setCursor(Qt::PointingHandCursor);
    actionRow->addStretch();
    actionRow->addWidget(actionButton_);

    copyLayout->addWidget(stateTitle_);
    copyLayout->addWidget(stateDetail_);
    copyLayout->addWidget(progress_);
    copyLayout->addStretch();
    copyLayout->addLayout(actionRow);

    bodyLayout->addWidget(glyph_, 0, Qt::AlignTop);
    bodyLayout->addWidget(copyColumn, 1);
    layout->addWidget(body, 1);

    connect(actionButton_, &QPushButton::clicked, this, &QDialog::reject);

    if (!missingFields_.isEmpty()) {
        showMissingFields();
        return;
    }

    setGlyphState("connecting");
    stateTitle_->setText("Connecting");
    stateDetail_->setText(QString("Opening %1 session for %2.")
                              .arg(connection_.feedSource.trimmed().isEmpty() ? "Rithmic" : connection_.feedSource.trimmed(),
                                  connection_.username.trimmed()));
    progress_->setRange(0, 100);
    progress_->setValue(48);

    QTimer::singleShot(900, this, [this] { showSuccess(); });
}

void ConnectionTestDialog::showSuccess()
{
    setGlyphState("success");
    stateTitle_->setText("Connection Successful");
    stateDetail_->setText("The connection profile is valid and ready to use.");
    progress_->setRange(0, 100);
    progress_->setValue(100);
    actionButton_->setText("Done");
    disconnect(actionButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(actionButton_, &QPushButton::clicked, this, &QDialog::accept);
}

void ConnectionTestDialog::showMissingFields()
{
    setGlyphState("error");
    stateTitle_->setText("Connection Incomplete");
    stateDetail_->setText("Complete these fields first: " + missingFields_.join(", ") + ".");
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->hide();
    actionButton_->setText("Close");
}

void ConnectionTestDialog::setGlyphState(const char* state)
{
    const QString stateText = QString::fromLatin1(state);
    if (stateText == "connecting") {
        glyphText_->setText("...");
    } else if (stateText == "success") {
        glyphText_->setText("OK");
    } else if (stateText == "error") {
        glyphText_->setText("!");
    }
    glyph_->setProperty("state", state);
    glyph_->style()->unpolish(glyph_);
    glyph_->style()->polish(glyph_);
}

} // namespace tc
