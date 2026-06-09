#pragma once

#include <QtCore/QPoint>
#include <QtCore/QString>
#include <QtWidgets/QDialog>

class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace tc {

class SelectInstrumentDialog final : public QDialog {
public:
    explicit SelectInstrumentDialog(QWidget* parent = nullptr);

    QString selectedExchange() const;
    QString selectedSymbol() const;
    QString selectedName() const;

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void rebuildExchanges();
    void rebuildSymbols();
    void acceptSelectedSymbol();
    void setSelectedExchange(const QString& exchange);

    QFrame* titleBar_{nullptr};
    QLabel* title_{nullptr};
    QListWidget* exchangeList_{nullptr};
    QTableWidget* symbolTable_{nullptr};
    QLineEdit* search_{nullptr};
    QLabel* resultCount_{nullptr};
    QPushButton* openButton_{nullptr};

    QString selectedExchange_{"All"};
    QString selectedSymbol_;
    QString selectedName_;
    QPoint dragOffset_;
    bool dragging_{false};
};

} // namespace tc
