#include "eventwatcher.h"

#include <QFileInfo>
#include <QtGlobal>

QString EventWatcher::defaultSocketPath() {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (runtime.isEmpty() || signature.isEmpty())
        return {};
    return runtime + "/hypr/" + signature + "/.socket2.sock";
}

bool EventWatcher::isRelevant(const QByteArray &line) {
    for (const char *event : {"monitoradded>>", "monitoraddedv2>>", "monitorremoved>>",
                              "monitorremovedv2>>", "configreloaded>>", "focusedmon>>", "workspace>>",
                              "workspacev2>>", "createworkspace>>", "createworkspacev2>>",
                              "destroyworkspace>>", "destroyworkspacev2>>", "moveworkspace>>",
                              "moveworkspacev2>>"})
        if (line.startsWith(event))
            return true;
    return false;
}

EventWatcher::EventWatcher(const QString &socketPath, QObject *parent)
    : QObject(parent), m_path(socketPath.isEmpty() ? defaultSocketPath() : socketPath) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &EventWatcher::refreshRequested);

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &EventWatcher::connectNow);

    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_backoff = 500;
        m_buffer.clear();
        // The state may have changed while we were away.
        m_debounce.start();
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &EventWatcher::onReadyRead);
    connect(&m_socket, &QLocalSocket::disconnected, this, &EventWatcher::scheduleReconnect);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] {
        if (m_socket.state() == QLocalSocket::UnconnectedState)
            scheduleReconnect();
    });
    if (!m_path.isEmpty())
        connectNow();
}

// m_socket is declared before the timers, so it is destroyed after them; its
// destructor emits disconnected(), which would reach the reconnect timer
// after it was already gone. Cut the connections first.
EventWatcher::~EventWatcher() {
    m_socket.disconnect(this);
    m_socket.abort();
}

void EventWatcher::connectNow() {
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    m_socket.connectToServer(m_path, QIODevice::ReadOnly);
}

void EventWatcher::scheduleReconnect() {
    if (m_reconnect.isActive())
        return;
    m_reconnect.start(m_backoff);
    m_backoff = qMin(m_backoff * 2, 10000);
}

void EventWatcher::onReadyRead() {
    m_buffer += m_socket.readAll();
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (isRelevant(line))
            m_debounce.start();
    }
    if (m_buffer.size() > 65536)
        m_buffer.clear();
}
