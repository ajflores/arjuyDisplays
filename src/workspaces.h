#pragma once

#include <QCoreApplication>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "monitorparser.h"

// Read-only view of the workspaces of each monitor. Nothing here writes a
// file or changes Hyprland: it only parses `hyprctl -j workspaces`,
// `-j workspacerules` and Omarchy's monitor-workspaces state file.

// One workspace as reported by `hyprctl -j workspaces`, plus what the rules say.
struct WorkspaceInfo {
    int id = 0;
    QString name, monitor, lastTitle;
    int windows = 0;
    bool persistent = false;
    bool active = false;   // the one shown on its monitor right now
    bool special = false;  // special:* (scratchpads)
    bool pinned = false;   // a workspace rule fixes it to a monitor
    QString pinnedTo;      // that monitor's name (or the rule's own text)
    bool guest = false;    // its id lies outside the Omarchy block of its monitor

    QString label() const { return name.isEmpty() ? QString::number(id) : name; }
    QVariantMap toVariant() const;
};

// One entry of `hyprctl -j workspacerules` (only what we show).
struct WorkspaceRule {
    QString workspace;   // the rule's selector: "1", "name:web", "special:x"...
    int id = -1;         // set when the selector is a plain number
    QString monitor;     // empty when the rule does not pin a monitor
    bool persistent = false, isDefault = false;
    QVariantMap toVariant() const;
};

// One line of Omarchy's state file: "<block>\t<connector>\t<identity>".
struct BlockEntry {
    int block = 0;
    QString connector, identity;
};

struct BlockState {
    enum Status { Missing, Unreadable, Invalid, Ok };
    Status status = Missing;
    QList<BlockEntry> entries;
};

// The block of ten workspace ids that Omarchy gave to a monitor.
struct MonitorBlock {
    bool known = false;
    int block = -1, first = 0, last = 0;
    QString identity;
    QVariantMap toVariant() const;
};

struct MonitorWorkspaces {
    QString monitor;
    MonitorBlock block;
    int activeId = 0;
    QString special;                  // special workspace open on it, if any
    QList<WorkspaceInfo> workspaces;  // sorted by id, specials not included
};

// Informational notice about workspaces; never an error.
struct WorkspaceConflict {
    QString kind;  // pinned-outside-block, windows-outside-block, rule-disconnected, duplicate-id
    QStringList monitors;
    QList<int> ids;
    QString message;
    QVariantMap toVariant() const;
};

struct WorkspaceModel {
    bool ok = false;      // `workspaces` was read and understood
    QString error;        // what went wrong (also for the rules), empty if all fine
    BlockState::Status blockStatus = BlockState::Missing;
    int fallbackBlock = -1;  // the block Omarchy lists as FALLBACK, if any
    QList<MonitorWorkspaces> monitors;
    QList<WorkspaceInfo> special;
    QList<WorkspaceRule> rules;
    QList<WorkspaceConflict> conflicts;

    const MonitorWorkspaces *forMonitor(const QString &name) const;
    QJsonObject toJson() const;
};

class Workspaces {
    Q_DECLARE_TR_FUNCTIONS(Workspaces)

public:
    static constexpr int kSlots = 10;  // workspaces per Omarchy block

    // Validated parsing: anything unexpected is rejected as a whole.
    static QList<WorkspaceInfo> parseWorkspaces(const QByteArray &json, QString *error);
    static QList<WorkspaceRule> parseRules(const QByteArray &json, QString *error);
    // Tolerant: skips lines it does not understand; Invalid if nothing usable
    // (or two lines claim the same block).
    static BlockState parseBlockState(const QByteArray &contents);
    // Default: $ARJUY_OMARCHY_WORKSPACES, else ~/.local/state/omarchy/monitor-workspaces.
    static QString defaultStatePath();
    // Reads (never writes) the state file.
    static BlockState readBlockState(const QString &path);

    // Pure combination of everything into a per-monitor model with notices.
    static WorkspaceModel build(const QList<MonitorInfo> &monitors, const QList<WorkspaceInfo> &workspaces,
                                const QList<WorkspaceRule> &rules, const BlockState &blocks);

    // Runs the two read-only hyprctl queries and combines them with the
    // state file. `monitors` is what `hyprctl -j monitors all` gave.
    static WorkspaceModel read(const QString &hyprctl, const QList<MonitorInfo> &monitors,
                               const QString &statePath = {});

    static int blockOfId(int id) { return (id - 1) / kSlots; }
};
