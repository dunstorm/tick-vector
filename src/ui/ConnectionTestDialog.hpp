#pragma once

#include "core/FeedConnection.hpp"
#include "core/MarketDataAdapter.hpp"

#include <QtCore/QStringList>
#include <QtWidgets/QDialog>
#include <memory>

class QLabel;
class QFrame;
class QProgressBar;
class QPushButton;

namespace tc {

class ConnectionTestDialog final : public QDialog {
public:
    explicit ConnectionTestDialog(FeedConnection connection, QStringList missingFields = {}, QWidget* parent = nullptr);

    void showSuccess();

private:
    void startTest();
    void showMissingFields();
    void showFailure(const QString& message);
    void setGlyphState(const char* state);

    FeedConnection connection_;
    QStringList missingFields_;
    std::unique_ptr<ITradingAdapter> adapter_;
    QFrame* glyph_{nullptr};
    QLabel* glyphText_{nullptr};
    QLabel* stateTitle_{nullptr};
    QLabel* stateDetail_{nullptr};
    QProgressBar* progress_{nullptr};
    QPushButton* actionButton_{nullptr};
};

} // namespace tc
