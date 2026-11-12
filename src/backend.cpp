#include "backend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

#include "hyprctl.h"
#include "versions.h"

Backend::Backend(const QString &hyprctl, const QString &pacman, bool watchEvents, QObject *parent)
    : QObject(parent),
      m_hyprctl(hyprctl.isEmpty() ? Hyprctl::executable() : hyprctl),
      m_pacman(pacman.isEmpty() ? QStandardPaths::findExecutable("pacman") : pacman) {
    if (watchEvents) {
        m_events = new EventWatcher({}, this);
        connect(m_events, &EventWatcher::refreshRequested, this, &Backend::refresh);
    }
    refresh();
}

void Backend::setSelectedIndex(int index) {
    if (index < 0 || index >= m_monitors.size() || index == m_selected)
        return;
    m_selected = index;
    emit selectedIndexChanged();
}

QVariantMap Backend::selected() const {
    return m_selected >= 0 && m_selected < m_monitorVariants.size() ? m_monitorVariants[m_selected].toMap()
                                                                    : QVariantMap();
}

void Backend::selectNext(int step) {
    if (m_monitors.isEmpty())
        return;
    const int n = m_monitors.size();
    setSelectedIndex(((m_selected < 0 ? 0 : m_selected) + step % n + n) % n);
}

void Backend::readVersions() {
    QString hypr, omarchy;
    const auto v = Hyprctl::run(m_hyprctl, {"-j", "version"});
    if (v.ok) {
        const auto doc = QJsonDocument::fromJson(v.output);
        hypr = doc.object().value("version").toString();
    }
    if (!m_pacman.isEmpty()) {
        const auto p = Hyprctl::run(m_pacman, {"-Q", "omarchy"});
        if (p.ok)
            omarchy = Versions::parsePacmanVersion(QString::fromUtf8(p.output));
    }
    QStringList warnings;
    const bool hyprOk = Versions::hyprlandTested(hypr), omarchyOk = Versions::omarchyTested(omarchy);
    if (!hyprOk)
        warnings << (hypr.isEmpty() ? tr("The Hyprland version could not be read.")
                                    : tr("Hyprland %1 has not been tested (tested: 0.56.x).").arg(hypr));
    if (!omarchyOk)
        warnings << (omarchy.isEmpty() ? tr("The Omarchy version could not be read.")
                                       : tr("Omarchy %1 has not been tested (tested: 4.5.x).").arg(omarchy));
    const bool tested = hyprOk && omarchyOk;

    if (hypr != m_hyprlandVersion || omarchy != m_omarchyVersion || tested != m_versionsTested
        || warnings != m_warnings) {
        m_hyprlandVersion = hypr;
        m_omarchyVersion = omarchy;
        m_versionsTested = tested;
        m_warnings = warnings;
        emit versionsChanged();
        emit warningsChanged();
    }
}

QString Backend::layoutSignature(const QList<MonitorInfo> &monitors) {
    QStringList parts;
    for (const auto &m : monitors)
        parts << QString("%1:%2,%3:%4x%5:%6:%7").arg(m.name).arg(m.x).arg(m.y).arg(qRound(m.logicalWidth()))
                     .arg(qRound(m.logicalHeight())).arg(m.disabled).arg(m.mirrorOf);
    return parts.join('|');
}

void Backend::refresh() {
    const auto result = Hyprctl::run(m_hyprctl, {"-j", "monitors", "all"});
    QString error;
    QList<MonitorInfo> monitors;
    if (!result.ok)
        error = tr("Could not read the monitors from Hyprland. %1").arg(result.error);
    else
        monitors = MonitorParser::parse(result.output, &error);

    // On failure keep the last good layout on screen and say what went wrong.
    if (error != m_error) {
        m_error = error;
        emit errorChanged();
    }
    if (error.isEmpty()) {
        const QString previous = m_selected >= 0 && m_selected < m_monitors.size() ? m_monitors[m_selected].name
                                                                                    : QString();
        const bool layoutChanged = layoutSignature(m_monitors) != layoutSignature(monitors);
        m_monitors = monitors;
        m_dragIndex = -1;

        // Pending positions survive a refresh. Drop the ones for monitors that
        // are gone or that now already sit where they were put.
        PendingMap kept;
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
            for (const auto &m : m_monitors)
                if (m.name == it.key() && QPoint(m.x, m.y) != it.value())
                    kept.insert(it.key(), it.value());
        const bool hadPending = !m_pending.isEmpty();
        m_pending = kept;
        if (hadPending && layoutChanged && !m_pending.isEmpty())
            m_notice = tr("The monitor layout changed while you had unsaved changes. Your pending positions were kept; please check them.");
        else if (m_pending.isEmpty())
            m_notice.clear();

        int focused = 0, keep = -1;
        for (int i = 0; i < monitors.size(); ++i) {
            if (monitors[i].focused)
                focused = i;
            if (monitors[i].name == previous)
                keep = i;
        }
        m_selected = keep >= 0 ? keep : focused;
        rebuild();
        emit selectedIndexChanged();
    }
    readVersions();
}

// ---- pending layout --------------------------------------------------------

bool Backend::movable(int index) const {
    return index >= 0 && index < m_monitors.size() && !m_monitors[index].disabled && m_monitors[index].mirrorOf.isEmpty();
}

QPoint Backend::effectivePos(int index) const {
    const auto &m = m_monitors[index];
    return m_pending.value(m.name, QPoint(m.x, m.y));
}

QList<LayoutRect> Backend::layoutRects(QList<int> *monitorIndex, int override, QPoint overridePos) const {
    QList<LayoutRect> rects;
    if (monitorIndex)
        monitorIndex->clear();
    for (int i = 0; i < m_monitors.size(); ++i) {
        const auto &m = m_monitors[i];
        if (!movable(i))
            continue;  // disabled monitors and mirrors take no space of their own
        const QPoint p = i == override ? overridePos : effectivePos(i);
        const bool swap = MonitorParser::swapsAxes(m.transform);
        LayoutRect r;
        r.name = m.name;
        r.x = p.x();
        r.y = p.y();
        r.w = qRound(m.logicalWidth());
        r.h = qRound(m.logicalHeight());
        r.pixelW = swap ? m.height : m.width;
        r.pixelH = swap ? m.width : m.height;
        r.scale = m.scale;
        rects << r;
        if (monitorIndex)
            monitorIndex->append(i);
    }
    return rects;
}

void Backend::rebuild() {
    // Issues of the arrangement as it stands, or as previewed during a drag.
    QList<int> idx;
    const auto rects = layoutRects(&idx, m_dragIndex, m_dragPos);
    const auto found = LayoutGeometry::analyze(rects);
    QVariantList issues;
    QSet<QString> flagged;
    for (const auto &issue : found) {
        issues << issue.toVariant();
        if (issue.isWarning())
            for (const auto &name : issue.monitors)
                flagged.insert(name);
    }
    m_issues = issues;

    m_monitorVariants.clear();
    m_pendingList.clear();
    for (int i = 0; i < m_monitors.size(); ++i) {
        const auto &m = m_monitors[i];
        QVariantMap v = m.toVariant();
        const QPoint p = effectivePos(i);
        v["currentX"] = m.x;
        v["currentY"] = m.y;
        v["x"] = p.x();
        v["y"] = p.y();
        v["moved"] = p != QPoint(m.x, m.y);
        v["movable"] = movable(i);
        v["warn"] = flagged.contains(m.name);
        m_monitorVariants << v;
        if (p != QPoint(m.x, m.y))
            m_pendingList << QVariantMap{{"name", m.name}, {"fromX", m.x}, {"fromY", m.y},
                                         {"toX", p.x()}, {"toY", p.y()}};
    }
    emit monitorsChanged();
    emit issuesChanged();
    emit pendingChanged();
}

void Backend::commit(const PendingMap &next) {
    m_dragIndex = -1;
    if (next != m_pending) {
        m_undo.append(m_pending);
        if (m_undo.size() > 200)
            m_undo.removeFirst();
        m_redo.clear();
        m_pending = next;
        if (m_pending.isEmpty())
            m_notice.clear();
    }
    rebuild();
    emit selectedIndexChanged();
}

QVariantMap Backend::dragSnap(int index, int x, int y, double thresholdLogical) {
    QVariantMap out{{"x", x}, {"y", y}, {"guides", QVariantList()}};
    if (!movable(index))
        return out;
    QList<int> idx;
    const auto rects = layoutRects(&idx);
    const int ri = idx.indexOf(index);
    const auto snapped = LayoutGeometry::snap(rects, ri, {x, y}, qMax(0, qRound(thresholdLogical)));
    QVariantList guides;
    for (const auto &g : snapped.guides)
        guides << QVariantMap{{"vertical", g.vertical}, {"pos", g.pos}, {"from", g.from}, {"to", g.to}};
    m_dragIndex = index;
    m_dragPos = snapped.pos;
    // Re-check the layout live without touching the monitors themselves.
    const auto found = LayoutGeometry::analyze(layoutRects(nullptr, m_dragIndex, m_dragPos));
    m_issues.clear();
    for (const auto &issue : found)
        m_issues << issue.toVariant();
    emit issuesChanged();
    return {{"x", snapped.pos.x()}, {"y", snapped.pos.y()}, {"guides", guides}};
}

void Backend::dragEnd(int index, int x, int y, double thresholdLogical) {
    if (!movable(index)) {
        dragCancel();
        return;
    }
    QList<int> idx;
    const auto rects = layoutRects(&idx);
    const int ri = idx.indexOf(index);
    const auto snapped = LayoutGeometry::snap(rects, ri, {x, y}, qMax(0, qRound(thresholdLogical)));
    const QPoint settled = LayoutGeometry::settle(rects, ri, snapped.pos);
    PendingMap next = m_pending;
    const auto &m = m_monitors[index];
    if (settled == QPoint(m.x, m.y))
        next.remove(m.name);
    else
        next.insert(m.name, settled);
    commit(next);
}

void Backend::dragCancel() {
    if (m_dragIndex < 0)
        return;
    m_dragIndex = -1;
    rebuild();
}

void Backend::moveSelected(int dx, int dy) {
    if (!movable(m_selected) || (dx == 0 && dy == 0))
        return;
    PendingMap next = m_pending;
    const auto &m = m_monitors[m_selected];
    const QPoint p = effectivePos(m_selected) + QPoint(dx, dy);
    if (p == QPoint(m.x, m.y))
        next.remove(m.name);
    else
        next.insert(m.name, p);
    commit(next);
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_pending);
    m_pending = m_undo.takeLast();
    m_dragIndex = -1;
    if (m_pending.isEmpty())
        m_notice.clear();
    rebuild();
    emit selectedIndexChanged();
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_pending);
    m_pending = m_redo.takeLast();
    m_dragIndex = -1;
    rebuild();
    emit selectedIndexChanged();
}

void Backend::resetPending() {
    commit({});
}

void Backend::dismissNotice() {
    if (m_notice.isEmpty())
        return;
    m_notice.clear();
    emit pendingChanged();
}

QJsonObject Backend::stateJson() const {
    QJsonArray monitors;
    for (const auto &m : m_monitors) {
        QJsonObject o = QJsonObject::fromVariantMap(m.toVariant());
        o["availableModes"] = QJsonArray::fromStringList(m.availableModes);
        monitors << o;
    }
    return {
        {"ok", m_error.isEmpty()},
        {"readOnly", true},
        {"error", m_error},
        {"hyprlandVersion", m_hyprlandVersion},
        {"omarchyVersion", m_omarchyVersion},
        {"versionsTested", m_versionsTested},
        {"warnings", QJsonArray::fromStringList(m_warnings)},
        {"monitors", monitors},
        {"issues", QJsonArray::fromVariantList(m_issues)},
    };
}
