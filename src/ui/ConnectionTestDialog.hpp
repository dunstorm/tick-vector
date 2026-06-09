#pragma once

#include "core/FeedConnection.hpp"

#include <QtCore/QStringList>
#include <QtWidgets/QDialog>

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
    void showMissingFields();
    void setGlyphState(const char* state);

    FeedConnection connection_;
    QStringList missingFields_;
    QFrame* glyph_{nullptr};
    QLabel* glyphText_{nullptr};
    QLabel* stateTitle_{nullptr};
    QLabel* stateDetail_{nullptr};
    QProgressBar* progress_{nullptr};
    QPushButton* actionButton_{nullptr};
};

} // namespace tc
