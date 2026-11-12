#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include "monitorparser.h"

class QQuickView;
class QScreen;

// What Qt knows about a screen; a plain struct so the matching can be tested
// with simulated screens.
struct ScreenDesc {
    QString name, manufacturer, model, serial;
};

// Which Hyprland monitor a screen is. monitorIndex is -1 when nothing matched.
struct ScreenMatch {
    int monitorIndex = -1;
    bool byName = false;  // false: matched through make/model/serial
};

// Shows a big connector name (DP-3, eDP-1...) on top of every real screen, so
// the user can tell which is which. Uses a layer-shell overlay window per
// screen; when that is unavailable it falls back to a normal window.
class Identifier : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY warningChanged)

public:
    explicit Identifier(const QList<MonitorInfo> *monitors, QObject *parent = nullptr);
    ~Identifier() override;

    // Pairs every screen with a Hyprland monitor: first by connector name, then
    // by make/model/serial, but only when that pairing is unambiguous (two
    // identical monitors cannot be told apart that way).
    static QList<ScreenMatch> matchScreens(const QList<ScreenDesc> &screens, const QList<MonitorInfo> &monitors);
    static ScreenDesc describe(const QScreen *screen);

    bool active() const { return m_active; }
    QString warning() const { return m_warning; }
    void setAccent(const QString &accent) { m_accent = accent; }

    // Opens the banners; `highlight` is a connector name to stress.
    Q_INVOKABLE void show(const QString &highlight = {}, int durationMs = 3000);
    Q_INVOKABLE void hide();
    Q_INVOKABLE void setHighlight(const QString &highlight);
    // The language changed: the warning is worded again.
    void retranslate();
    int windowCount() const { return m_windows.size(); }

signals:
    void activeChanged();
    void warningChanged();

private:
    void rebuild();
    void clearWindows();
    QString warningText() const;
    void setWarning(const QString &warning);

    const QList<MonitorInfo> *m_monitors;
    QList<QPointer<QQuickView>> m_windows;
    QTimer m_timer;
    QStringList m_unmatched;
    QString m_accent = "#FFD60A", m_highlight, m_warning;
    bool m_active = false;
};
