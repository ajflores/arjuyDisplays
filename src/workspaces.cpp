#include "workspaces.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

#include "hyprctl.h"

QVariantMap WorkspaceInfo::toVariant() const {
    return {{"id", id}, {"name", name}, {"label", label()}, {"monitor", monitor}, {"windows", windows},
            {"persistent", persistent}, {"active", active}, {"special", special}, {"pinned", pinned},
            {"pinnedTo", pinnedTo}, {"guest", guest}, {"lastTitle", lastTitle}};
}

QVariantMap WorkspaceRule::toVariant() const {
    return {{"workspace", workspace}, {"id", id}, {"monitor", monitor}, {"persistent", persistent},
            {"default", isDefault}};
}

QVariantMap MonitorBlock::toVariant() const {
    return {{"known", known}, {"block", block}, {"first", first}, {"last", last}, {"identity", identity}};
}

QVariantMap WorkspaceConflict::toVariant() const {
    QVariantList idList;
    for (int id : ids)
        idList << id;
    return {{"kind", kind}, {"monitors", monitors}, {"ids", idList}, {"message", message}};
}

const MonitorWorkspaces *WorkspaceModel::forMonitor(const QString &name) const {
    for (const auto &m : monitors)
        if (m.monitor == name)
            return &m;
    return nullptr;
}

QJsonObject WorkspaceModel::toJson() const {
    static const char *const statuses[] = {"missing", "unreadable", "invalid", "ok"};
    QJsonArray mons;
    for (const auto &m : monitors) {
        QJsonArray list;
        for (const auto &w : m.workspaces)
            list << QJsonObject::fromVariantMap(w.toVariant());
        mons << QJsonObject{{"monitor", m.monitor}, {"block", QJsonObject::fromVariantMap(m.block.toVariant())},
                            {"activeId", m.activeId}, {"special", m.special}, {"workspaces", list}};
    }
    QJsonArray sp, rl, cf;
    for (const auto &w : special)
        sp << QJsonObject::fromVariantMap(w.toVariant());
    for (const auto &r : rules)
        rl << QJsonObject::fromVariantMap(r.toVariant());
    for (const auto &c : conflicts)
        cf << QJsonObject::fromVariantMap(c.toVariant());
    return {{"ok", ok}, {"error", error}, {"blockFile", statuses[blockStatus]}, {"fallbackBlock", fallbackBlock},
            {"monitors", mons}, {"special", sp}, {"rules", rl}, {"conflicts", cf}};
}

// ---- parsing ---------------------------------------------------------------

static bool isInt(const QJsonValue &v) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() == std::floor(v.toDouble())
           && std::abs(v.toDouble()) < 1e9;
}

QList<WorkspaceInfo> Workspaces::parseWorkspaces(const QByteArray &json, QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return QList<WorkspaceInfo>{};
    };
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(tr("hyprctl returned workspaces that are not valid JSON (%1).").arg(parseError.errorString()));
    if (!doc.isArray())
        return fail(tr("hyprctl returned an unexpected shape: a list of workspaces was expected."));
    QList<WorkspaceInfo> out;
    for (const auto &item : doc.array()) {
        if (!item.isObject())
            return fail(tr("hyprctl returned an unexpected shape: a workspace entry is not an object."));
        const auto o = item.toObject();
        if (!isInt(o.value("id")) || !o.value("name").isString() || !o.value("monitor").isString()
            || !isInt(o.value("windows")) || o.value("windows").toInt() < 0)
            return fail(tr("A workspace has a missing or invalid field (id, name, monitor or windows)."));
        WorkspaceInfo w;
        w.id = o.value("id").toInt();
        w.name = o.value("name").toString();
        w.monitor = o.value("monitor").toString();
        w.windows = o.value("windows").toInt();
        w.persistent = o.value("ispersistent").toBool(false);
        w.lastTitle = o.value("lastwindowtitle").toString();
        w.special = w.id < 0 || w.name.startsWith("special:");
        out << w;
    }
    return out;
}

QList<WorkspaceRule> Workspaces::parseRules(const QByteArray &json, QString *error) {
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return QList<WorkspaceRule>{};
    };
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(tr("hyprctl returned workspace rules that are not valid JSON (%1).").arg(parseError.errorString()));
    if (!doc.isArray())
        return fail(tr("hyprctl returned an unexpected shape: a list of workspace rules was expected."));
    static const QRegularExpression number("^[0-9]{1,9}$");
    QList<WorkspaceRule> out;
    for (const auto &item : doc.array()) {
        if (!item.isObject() || !item.toObject().value("workspaceString").isString())
            return fail(tr("A workspace rule has a missing or invalid workspaceString."));
        const auto o = item.toObject();
        WorkspaceRule r;
        r.workspace = o.value("workspaceString").toString();
        if (number.match(r.workspace).hasMatch())
            r.id = r.workspace.toInt();
        r.monitor = o.value("monitor").toString();
        r.persistent = o.value("persistent").toBool(false);
        r.isDefault = o.value("default").toBool(false);
        out << r;
    }
    return out;
}

BlockState Workspaces::parseBlockState(const QByteArray &contents) {
    BlockState state;
    static const QRegularExpression line(R"(^(\d{1,4})\t([^\t]*)\t(.+)$)");
    QSet<int> seen;
    const auto lines = QString::fromUtf8(contents).split('\n');
    for (QString text : lines) {
        if (text.endsWith('\r'))
            text.chop(1);
        const auto m = line.match(text);
        if (!m.hasMatch())
            continue;
        BlockEntry e;
        e.block = m.captured(1).toInt();
        e.connector = m.captured(2);
        e.identity = m.captured(3);
        if (seen.contains(e.block)) {  // two owners for one block: not trustworthy
            state.entries.clear();
            state.status = BlockState::Invalid;
            return state;
        }
        seen.insert(e.block);
        state.entries << e;
    }
    state.status = state.entries.isEmpty() ? BlockState::Invalid : BlockState::Ok;
    return state;
}

QString Workspaces::defaultStatePath() {
    const QString override = qEnvironmentVariable("ARJUY_OMARCHY_WORKSPACES");
    if (!override.isEmpty())
        return override;
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty())
        state = QDir::homePath() + "/.local/state";
    return state + "/omarchy/monitor-workspaces";
}

BlockState Workspaces::readBlockState(const QString &path) {
    QFile file(path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly))
        return {BlockState::Unreadable, {}};
    return parseBlockState(file.read(1 << 20));  // a few lines in practice; cap it anyway
}

// ---- combination -----------------------------------------------------------

static QString identityOf(const MonitorInfo &m) {
    return m.description.isEmpty() ? m.name : m.description;
}

static QString stripDuplicateSuffix(const QString &identity) {
    static const QRegularExpression suffix(R"( #\d+$)");
    QString s = identity;
    s.remove(suffix);
    return s;
}

// Omarchy names a monitor by its description (or connector without one), with
// " #2" for identical twins. Its state file also records the connector.
static MonitorBlock blockFor(const MonitorInfo &m, const QList<MonitorInfo> &all, const BlockState &blocks) {
    MonitorBlock result;
    if (blocks.status != BlockState::Ok)
        return result;
    const QString described = identityOf(m);
    const BlockEntry *found = nullptr;
    for (const auto &e : blocks.entries)
        if (e.identity != "FALLBACK" && e.connector == m.name && stripDuplicateSuffix(e.identity) == described)
            found = &e;
    int twins = 0;  // identical monitors cannot be told apart without a connector row
    for (const auto &other : all)
        if (identityOf(other) == described)
            ++twins;
    if (!found && twins == 1) {
        int matches = 0;
        for (const auto &e : blocks.entries)
            if (e.identity == described && e.connector.isEmpty()) {
                found = &e;
                ++matches;
            }
        if (matches != 1)
            found = nullptr;
    }
    if (!found)
        return result;
    result.known = true;
    result.block = found->block;
    result.first = found->block * Workspaces::kSlots + 1;
    result.last = found->block * Workspaces::kSlots + Workspaces::kSlots;
    result.identity = found->identity;
    return result;
}

WorkspaceModel Workspaces::build(const QList<MonitorInfo> &monitors, const QList<WorkspaceInfo> &workspaces,
                                 const QList<WorkspaceRule> &rules, const BlockState &blocks) {
    WorkspaceModel model;
    model.ok = true;
    model.blockStatus = blocks.status;
    model.rules = rules;
    for (const auto &e : blocks.entries)
        if (e.identity == "FALLBACK")
            model.fallbackBlock = e.block;

    // Rules: resolve the monitor a rule names ("DP-3" or "desc:...").
    auto resolve = [&](const QString &spec) -> QString {
        for (const auto &m : monitors)
            if (m.name == spec || (spec.startsWith("desc:") && m.description == spec.mid(5)))
                return m.name;
        return {};
    };
    QMap<int, QString> pinnedTo;  // workspace id -> monitor (as written in the rule)
    for (const auto &r : rules)
        if (r.id > 0 && !r.monitor.isEmpty() && !pinnedTo.contains(r.id))
            pinnedTo.insert(r.id, r.monitor);

    // Monitors and their blocks. Mirrors and a block shared by two monitors
    // (a stale file) are not trusted as owners.
    QMap<int, QString> owner;
    QSet<int> contested;
    for (const auto &m : monitors) {
        MonitorWorkspaces mw;
        mw.monitor = m.name;
        mw.block = blockFor(m, monitors, blocks);
        mw.activeId = m.activeWorkspaceId;
        if (m.specialWorkspace.startsWith("special:"))
            mw.special = m.specialWorkspace.mid(8);
        else
            mw.special = m.specialWorkspace;
        if (mw.block.known && m.mirrorOf.isEmpty()) {
            if (owner.contains(mw.block.block))
                contested.insert(mw.block.block);
            owner.insert(mw.block.block, m.name);
        }
        model.monitors << mw;
    }
    for (int b : contested)
        owner.remove(b);

    // Workspaces to their monitors.
    QMap<int, QStringList> idMonitors;
    for (auto w : workspaces) {
        if (w.special) {
            model.special << w;
            continue;
        }
        idMonitors[w.id] << w.monitor;
        if (pinnedTo.contains(w.id)) {
            w.pinned = true;
            const QString resolved = resolve(pinnedTo.value(w.id));
            w.pinnedTo = resolved.isEmpty() ? pinnedTo.value(w.id) : resolved;
        }
        for (auto &mw : model.monitors) {
            if (mw.monitor != w.monitor)
                continue;
            w.active = w.id == mw.activeId;
            w.guest = mw.block.known && (w.id < mw.block.first || w.id > mw.block.last);
            mw.workspaces << w;
            break;
        }
    }
    for (auto &mw : model.monitors)
        std::sort(mw.workspaces.begin(), mw.workspaces.end(),
                  [](const WorkspaceInfo &a, const WorkspaceInfo &b) { return a.id < b.id; });

    // Notices ----------------------------------------------------------------
    auto idList = [](const QList<int> &ids) {
        QStringList s;
        for (int id : ids)
            s << QString::number(id);
        return s.join(", ");
    };
    auto blockText = [&](const QString &monitor, int *first, int *last) {
        for (const auto &mw : model.monitors)
            if (mw.monitor == monitor) {
                *first = mw.block.first;
                *last = mw.block.last;
            }
    };

    // 1. A fixed rule puts a workspace on a monitor other than the owner of its block.
    QMap<QPair<QString, QString>, QList<int>> pinnedOutside;  // (pinned monitor, owner) -> ids
    QSet<QString> disconnectedSeen;
    for (const auto &r : rules) {
        if (r.id <= 0 || r.monitor.isEmpty())
            continue;
        const QString target = resolve(r.monitor);
        if (target.isEmpty()) {
            if (disconnectedSeen.contains(r.workspace + "@" + r.monitor))
                continue;
            disconnectedSeen.insert(r.workspace + "@" + r.monitor);
            WorkspaceConflict c;
            c.kind = "rule-disconnected";
            c.monitors = {r.monitor};
            c.ids = {r.id};
            c.message = tr("A workspace rule pins workspace %1 to %2, but that monitor is not connected.")
                            .arg(r.id).arg(r.monitor);
            model.conflicts << c;
            continue;
        }
        const QString blockOwner = owner.value(blockOfId(r.id));
        if (!blockOwner.isEmpty() && blockOwner != target) {
            auto &ids = pinnedOutside[{target, blockOwner}];
            if (!ids.contains(r.id))
                ids << r.id;
        }
    }
    for (auto it = pinnedOutside.begin(); it != pinnedOutside.end(); ++it) {
        auto ids = it.value();
        std::sort(ids.begin(), ids.end());
        int first = 0, last = 0;
        blockText(it.key().second, &first, &last);
        // The ids can belong to different blocks only if one monitor owns several; report their block.
        WorkspaceConflict c;
        c.kind = "pinned-outside-block";
        c.monitors = {it.key().first, it.key().second};
        c.ids = ids;
        c.message = ids.size() == 1
            ? tr("Workspace %1 is pinned to %2 by a workspace rule but belongs to %3's block (%4-%5) in Omarchy.")
                  .arg(idList(ids), it.key().first, it.key().second).arg(first).arg(last)
            : tr("Workspaces %1 are pinned to %2 by workspace rules but belong to %3's block (%4-%5) in Omarchy.")
                  .arg(idList(ids), it.key().first, it.key().second).arg(first).arg(last);
        model.conflicts << c;
    }

    // 2. Workspaces with windows outside the block of the monitor they are on.
    for (const auto &mw : model.monitors)
        for (const auto &w : mw.workspaces)
            if (w.guest && w.windows > 0) {
                WorkspaceConflict c;
                c.kind = "windows-outside-block";
                c.monitors = {mw.monitor};
                c.ids = {w.id};
                c.message = tr("Workspace %1 is on %2 and has windows (%3), but %2's Omarchy block is %4-%5.")
                                .arg(w.id).arg(mw.monitor).arg(w.windows).arg(mw.block.first).arg(mw.block.last);
                model.conflicts << c;
            }

    // 3. Duplicate ids in Hyprland's list.
    for (auto it = idMonitors.begin(); it != idMonitors.end(); ++it)
        if (it.value().size() > 1) {
            WorkspaceConflict c;
            c.kind = "duplicate-id";
            c.monitors = it.value();
            c.monitors.removeDuplicates();
            c.ids = {it.key()};
            c.message = tr("Workspace id %1 appears more than once in Hyprland's list (monitors: %2).")
                            .arg(it.key()).arg(it.value().join(", "));
            model.conflicts << c;
        }
    return model;
}

WorkspaceModel Workspaces::read(const QString &hyprctl, const QList<MonitorInfo> &monitors, const QString &statePath) {
    WorkspaceModel failed;
    const auto w = Hyprctl::run(hyprctl, {"-j", "workspaces"});
    QString error;
    QList<WorkspaceInfo> workspaces;
    if (!w.ok)
        error = tr("Could not read the workspaces from Hyprland. %1").arg(w.error);
    else
        workspaces = parseWorkspaces(w.output, &error);
    if (!error.isEmpty()) {
        failed.error = error;
        return failed;
    }

    QList<WorkspaceRule> rules;
    QString rulesError;
    const auto r = Hyprctl::run(hyprctl, {"-j", "workspacerules"});
    if (!r.ok)
        rulesError = tr("Could not read the workspace rules from Hyprland. %1").arg(r.error);
    else
        rules = parseRules(r.output, &rulesError);
    if (!rulesError.isEmpty())
        rules.clear();

    const auto blocks = readBlockState(statePath.isEmpty() ? defaultStatePath() : statePath);
    auto model = build(monitors, workspaces, rules, blocks);
    model.error = rulesError;
    return model;
}
