#pragma once

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QStringList>
#include <QVariantList>

#include "eventwatcher.h"
#include "layoutgeometry.h"
#include "monitorparser.h"

// Reads the monitor layout from Hyprland and exposes it to QML. It also keeps
// a pending (preview) arrangement with undo/redo and checks it for problems.
// Nothing here writes a file or changes Hyprland: applying is a later phase.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY monitorsChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectedIndexChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY warningsChanged)
    Q_PROPERTY(QString hyprlandVersion READ hyprlandVersion NOTIFY versionsChanged)
    Q_PROPERTY(QString omarchyVersion READ omarchyVersion NOTIFY versionsChanged)
    Q_PROPERTY(bool versionsTested READ versionsTested NOTIFY versionsChanged)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT)
    Q_PROPERTY(QVariantList issues READ issues NOTIFY issuesChanged)
    Q_PROPERTY(bool hasPending READ hasPending NOTIFY pendingChanged)
    Q_PROPERTY(QVariantList pendingList READ pendingList NOTIFY pendingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY pendingChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY pendingChanged)
    Q_PROPERTY(QString pendingNotice READ pendingNotice NOTIFY pendingChanged)

public:
    // Empty arguments use the real tools; tests pass fakes.
    explicit Backend(const QString &hyprctl = {}, const QString &pacman = {}, bool watchEvents = true,
                     QObject *parent = nullptr);

    QVariantList monitors() const { return m_monitorVariants; }
    int selectedIndex() const { return m_selected; }
    void setSelectedIndex(int index);
    QVariantMap selected() const;
    QString error() const { return m_error; }
    QStringList warnings() const { return m_warnings; }
    QString hyprlandVersion() const { return m_hyprlandVersion; }
    QString omarchyVersion() const { return m_omarchyVersion; }
    bool versionsTested() const { return m_versionsTested; }
    // Always true in phase 1; kept so the UI says so.
    bool readOnly() const { return true; }
    const QList<MonitorInfo> &monitorList() const { return m_monitors; }

    QVariantList issues() const { return m_issues; }
    bool hasPending() const { return !m_pending.isEmpty(); }
    QVariantList pendingList() const { return m_pendingList; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString pendingNotice() const { return m_notice; }
    const QMap<QString, QPoint> &pending() const { return m_pending; }

    // Dragging. dragSnap previews the snapped position (and re-checks the
    // layout live); dragEnd settles it (no overlap, touching, whole numbers)
    // and records it as a pending change. Neither touches Hyprland.
    Q_INVOKABLE QVariantMap dragSnap(int index, int x, int y, double thresholdLogical);
    Q_INVOKABLE void dragEnd(int index, int x, int y, double thresholdLogical);
    Q_INVOKABLE void dragCancel();
    Q_INVOKABLE void moveSelected(int dx, int dy);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void resetPending();
    Q_INVOKABLE void dismissNotice();

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectNext(int step);
    // The full state as JSON, used by `arjuy-displays check --json`.
    QJsonObject stateJson() const;

signals:
    void monitorsChanged();
    void selectedIndexChanged();
    void errorChanged();
    void warningsChanged();
    void versionsChanged();
    void issuesChanged();
    void pendingChanged();

private:
    using PendingMap = QMap<QString, QPoint>;

    void readVersions();
    void rebuild();
    void commit(const PendingMap &next);
    bool movable(int index) const;
    QPoint effectivePos(int index) const;
    QList<LayoutRect> layoutRects(QList<int> *monitorIndex = nullptr, int override = -1, QPoint overridePos = {}) const;
    static QString layoutSignature(const QList<MonitorInfo> &monitors);

    QString m_hyprctl, m_pacman;
    QList<MonitorInfo> m_monitors;
    QVariantList m_monitorVariants;
    int m_selected = -1;
    QString m_error, m_hyprlandVersion, m_omarchyVersion;
    QStringList m_warnings;
    bool m_versionsTested = false;
    EventWatcher *m_events = nullptr;

    PendingMap m_pending;
    QList<PendingMap> m_undo, m_redo;
    QVariantList m_issues, m_pendingList;
    QString m_notice;
    int m_dragIndex = -1;
    QPoint m_dragPos;
};
