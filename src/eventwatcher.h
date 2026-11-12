#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QTimer>

// Listens to Hyprland's event socket (.socket2.sock) and asks for a refresh,
// debounced, when monitors come and go or the config reloads. It only reads.
class EventWatcher : public QObject {
    Q_OBJECT
public:
    explicit EventWatcher(const QString &socketPath = {}, QObject *parent = nullptr);
    ~EventWatcher() override;

    static QString defaultSocketPath();
    static bool isRelevant(const QByteArray &line);
    bool connected() const { return m_socket.state() == QLocalSocket::ConnectedState; }

signals:
    void refreshRequested();

private:
    void connectNow();
    void scheduleReconnect();
    void onReadyRead();

    QString m_path;
    QLocalSocket m_socket;
    QTimer m_debounce, m_reconnect;
    QByteArray m_buffer;
    int m_backoff = 500;
};
