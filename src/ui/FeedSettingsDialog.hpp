#pragma once

#include "core/FeedConnection.hpp"

#include <QtCore/QPoint>
#include <QtCore/QVector>
#include <QtWidgets/QDialog>

class QCheckBox;
class QComboBox;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace tc {

class FeedSettingsDialog final : public QDialog {
public:
    explicit FeedSettingsDialog(QVector<FeedConnection> connections, QWidget* parent = nullptr);

    QVector<FeedConnection> connections() const;

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void rebuildConnectionList();
    void selectConnection(int index);
    void addConnection();
    void removeSelectedConnection();
    void testCurrentConnection();
    void saveCurrentForm();
    void loadSelectedConnection();
    void setFormEnabled(bool enabled);

    QVector<FeedConnection> connections_;
    int selectedIndex_{-1};

    QFrame* titleBar_{nullptr};
    QLabel* title_{nullptr};
    QListWidget* connectionList_{nullptr};
    QLineEdit* workspaceName_{nullptr};
    QComboBox* workspaceLocation_{nullptr};
    QLineEdit* name_{nullptr};
    QComboBox* feedSource_{nullptr};
    QComboBox* gatewaySelector_{nullptr};
    QComboBox* systemSelector_{nullptr};
    QComboBox* marketData_{nullptr};
    QLineEdit* username_{nullptr};
    QLineEdit* password_{nullptr};
    QCheckBox* connectOnStartup_{nullptr};
    QPushButton* removeButton_{nullptr};
    QPushButton* saveButton_{nullptr};
    QPoint dragOffset_;
    bool dragging_{false};
};

} // namespace tc
