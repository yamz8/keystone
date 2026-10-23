#pragma once

#include <QObject>
#include <QUrl>

// Asks the user for a model to open. The portal does it for real; tests hand
// the backend a fake that answers at once.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openModel() = 0;

signals:
    void openSelected(const QUrl &url);
    void failed(const QString &message);
};
