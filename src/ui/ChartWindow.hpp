#pragma once

#include "core/FeedConnection.hpp"

#include <QtCore/QPoint>
#include <QtCore/QString>
#include <QtWidgets/QMainWindow>

class QEvent;
class QFrame;
class QLabel;
class QPushButton;

namespace tc {

class ChartWindow final : public QMainWindow {
public:
    explicit ChartWindow(FeedConnection connection, QString symbol = "NQ", QString exchange = "CME", QWidget* parent = nullptr);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    QWidget* createChrome();
    QPushButton* createWindowControl(const QString& objectName);
    void applyLocalStyle();

    FeedConnection connection_;
    QString symbol_;
    QString exchange_;
    QFrame* chrome_{nullptr};
    QLabel* title_{nullptr};
    QPoint dragOffset_;
    bool dragging_{false};
};

} // namespace tc
