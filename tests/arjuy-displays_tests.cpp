#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSet>
#include <QFile>
#include <QLocalServer>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "eventwatcher.h"
#include "hyprctl.h"
#include "identifier.h"
#include "layoutgeometry.h"
#include "monitorparser.h"
#include "theme.h"
#include "translator.h"
#include "versions.h"
#include "workspaces.h"

#ifndef SOURCE_DIR
#define SOURCE_DIR "."
#endif

class DisplaysTests : public QObject {
    Q_OBJECT

private:
    static QByteArray fixture(const QString &name) {
        QFile file(QString(SOURCE_DIR) + "/" + name);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static void write(const QString &path, const QByteArray &contents, bool executable = false) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
        file.close();
        if (executable)
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    // A hyprctl stand-in: answers `monitors` and `version` from files, or runs a custom body.
    static QString fakeHyprctl(const QTemporaryDir &dir, const QString &monitorsBody) {
        const QString path = dir.filePath("hyprctl");
        write(path,
              "#!/bin/sh\ncase \"$*\" in\n"
              "  *version*) echo '{\"version\":\"0.56.2\"}' ;;\n"
              "  *monitors*) " + monitorsBody.toUtf8() + " ;;\n"
              "  *) exit 2 ;;\nesac\n", true);
        return path;
    }
    static QString fakePacman(const QTemporaryDir &dir, const QString &line) {
        const QString path = dir.filePath("pacman");
        write(path, "#!/bin/sh\necho '" + line.toUtf8() + "'\n", true);
        return path;
    }

    // A hyprctl that answers monitors, workspaces and workspacerules from files.
    static QString fakeWorkspaceHyprctl(const QTemporaryDir &dir, const QString &monitors, const QString &workspaces,
                                        const QString &rules) {
        const QString path = dir.filePath("hyprctl");
        write(path,
              "#!/bin/sh\ncase \"$*\" in\n"
              "  *version*) echo '{\"version\":\"0.56.2\"}' ;;\n"
              "  *workspacerules*) " + rules.toUtf8() + " ;;\n"
              "  *workspaces*) " + workspaces.toUtf8() + " ;;\n"
              "  *monitors*) cat '" + monitors.toUtf8() + "' ;;\n"
              "  *) exit 2 ;;\nesac\n", true);
        return path;
    }
    static QString fixturePath(const QString &name) { return QString(SOURCE_DIR) + "/" + name; }
    struct RealState {
        QList<MonitorInfo> monitors;
        QList<WorkspaceInfo> workspaces;
        QList<WorkspaceRule> rules;
        BlockState blocks;
    };
    static RealState realState() {
        RealState s;
        QString error;
        s.monitors = MonitorParser::parse(fixture("ws-real-monitors.json"), &error);
        s.workspaces = Workspaces::parseWorkspaces(fixture("ws-real-workspaces.json"), &error);
        s.rules = Workspaces::parseRules(fixture("ws-real-rules.json"), &error);
        s.blocks = Workspaces::parseBlockState(fixture("ws-real-state.txt"));
        return s;
    }
    static QList<int> idsOf(const MonitorWorkspaces &m) {
        QList<int> ids;
        for (const auto &w : m.workspaces)
            ids << w.id;
        return ids;
    }

private slots:
    void initTestCase() {
        // Never read the real Omarchy state file from tests.
        qputenv("ARJUY_OMARCHY_WORKSPACES", QByteArray("/nonexistent/arjuy-test/monitor-workspaces"));
        // Nor the real language settings or Omarchy's language file.
        qputenv("ARJUY_SETTINGS", QByteArray("/nonexistent/arjuy-test/settings.json"));
        qputenv("ARJUY_OMARCHY_LANGUAGE", QByteArray("/nonexistent/arjuy-test/language"));
    }

    void parsesRealisticThreeMonitors() {
        QString error;
        const auto list = MonitorParser::parse(fixture("three-monitors.json"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].name, QString("eDP-1"));
        QCOMPARE(list[0].x, 3840);
        QCOMPARE(list[0].scale, 1.25);
        QCOMPARE(list[1].name, QString("DP-3"));
        QVERIFY(list[1].focused);
        QCOMPARE(list[2].x, 1920);
        QCOMPARE(list[2].scale, 1.0);
        QVERIFY(list[0].mirrorOf.isEmpty());
    }

    void logicalSizeDividesByScale() {
        QString error;
        const auto list = MonitorParser::parse(fixture("three-monitors.json"), &error);
        QCOMPARE(list[0].logicalWidth(), 1536.0);
        QCOMPARE(list[0].logicalHeight(), 864.0);
        QCOMPARE(list[2].logicalWidth(), 1920.0);
    }

    void logicalSizeSwapsWhenRotated() {
        QString error;
        const auto list = MonitorParser::parse(fixture("rotated.json"), &error);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].transform, 1);
        QCOMPARE(list[0].logicalWidth(), 1080.0);
        QCOMPARE(list[0].logicalHeight(), 1920.0);
        for (int t : {1, 3, 5, 7})
            QVERIFY(MonitorParser::swapsAxes(t));
        for (int t : {0, 2, 4, 6})
            QVERIFY(!MonitorParser::swapsAxes(t));
    }

    void modesAreDeduplicatedAndNormalized() {
        QString error;
        const auto list = MonitorParser::parse(fixture("three-monitors.json"), &error);
        QCOMPARE(list[1].availableModes,
                 QStringList({"1920x1080@60.00Hz", "1920x1080@74.97Hz", "1680x1050@59.88Hz"}));
        QCOMPARE(MonitorParser::normalizeMode("1920x1080@60Hz"), QString("1920x1080@60.00Hz"));
        QCOMPARE(MonitorParser::normalizeMode("2560x1440@143.912Hz"), QString("2560x1440@143.91Hz"));
        QVERIFY(MonitorParser::normalizeMode("garbage").isEmpty());
        QCOMPARE(MonitorParser::normalizeModes({"1x1@60Hz", "nope", "1x1@60.00Hz"}), QStringList({"1x1@60.00Hz"}));
    }

    void rejectsCorruptAndUnexpectedJson() {
        QString error;
        QVERIFY(MonitorParser::parse(fixture("corrupt.json"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        for (const char *bad : {"{}", "[]", "[1]", R"([{"name":""}])", R"([{"name":"A"}])",
                                R"([{"name":"A","x":0,"y":0,"width":1,"height":1,"scale":0,"refreshRate":60,"transform":0}])",
                                R"([{"name":"A","x":0,"y":0,"width":1,"height":1,"scale":1,"refreshRate":60,"transform":9}])"}) {
            error.clear();
            QVERIFY2(MonitorParser::parse(bad, &error).isEmpty(), bad);
            QVERIFY2(!error.isEmpty(), bad);
        }
    }

    void versionsAreChecked() {
        QVERIFY(Versions::hyprlandTested("0.56.2"));
        QVERIFY(Versions::hyprlandTested("0.56"));
        QVERIFY(!Versions::hyprlandTested("0.57.0"));
        QVERIFY(!Versions::hyprlandTested("0.560.1"));
        QVERIFY(Versions::omarchyTested("4.5.0-3"));
        QVERIFY(!Versions::omarchyTested("4.6.0-1"));
        QCOMPARE(Versions::parsePacmanVersion("omarchy 4.5.0-3\n"), QString("4.5.0-3"));
        QVERIFY(Versions::parsePacmanVersion("error: package not found").isEmpty());
    }

    void backendReadsFromFakeHyprctl() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
        QCOMPARE(backend.monitors().size(), 3);
        QCOMPARE(backend.selected().value("name").toString(), QString("DP-3"));  // the focused one
        QVERIFY(backend.versionsTested());
        QVERIFY(backend.warnings().isEmpty());
        backend.selectNext(1);
        QCOMPARE(backend.selected().value("name").toString(), QString("DP-5"));
        backend.selectNext(1);
        backend.selectNext(1);
        QCOMPARE(backend.selectedIndex(), 1);
        backend.selectNext(-2);
        QCOMPARE(backend.selectedIndex(), 2);
    }

    void backendWarnsOnUntestedVersion() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 9.0.0-1"), false);
        QVERIFY(!backend.versionsTested());
        QCOMPARE(backend.warnings().size(), 1);
        QVERIFY(backend.readOnly());
    }

    void backendReportsInvalidOutput() {
        QTemporaryDir dir;
        Backend backend(fakeHyprctl(dir, "echo 'not json at all'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(backend.monitors().isEmpty());
    }

    void backendReportsFailingHyprctl() {
        QTemporaryDir dir;
        Backend backend(fakeHyprctl(dir, "echo oops >&2; exit 1"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(backend.monitors().isEmpty());
    }

    void backendKeepsLastGoodLayoutOnFailure() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        write(json, "garbage");
        backend.refresh();
        QVERIFY(!backend.error().isEmpty());
        QCOMPARE(backend.monitors().size(), 3);
        write(json, fixture("three-monitors.json"));
        backend.refresh();
        QVERIFY(backend.error().isEmpty());
    }

    void hyprctlTimeoutKillsTheProcess() {
        QTemporaryDir dir;
        const QString slow = dir.filePath("slow");
        write(slow, "#!/bin/sh\nexec sleep 10\n", true);
        QElapsedTimer timer;
        timer.start();
        const auto result = Hyprctl::run(slow, {"-j", "monitors", "all"}, 300);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(timer.elapsed() < 3000);
    }

    void hyprctlMissingProgram() {
        QVERIFY(!Hyprctl::run("/nonexistent/hyprctl", {}).ok);
        QVERIFY(!Hyprctl::run({}, {}).ok);
    }

    void hyprctlPathCanBeOverridden() {
        qputenv("ARJUY_HYPRCTL", "/tmp/fake-hyprctl");
        QCOMPARE(Hyprctl::executable(), QString("/tmp/fake-hyprctl"));
        qunsetenv("ARJUY_HYPRCTL");
    }

    void eventWatcherFiltersEvents() {
        QVERIFY(EventWatcher::isRelevant("monitoradded>>DP-3"));
        QVERIFY(EventWatcher::isRelevant("monitorremoved>>DP-3"));
        QVERIFY(EventWatcher::isRelevant("configreloaded>>"));
        for (const char *e : {"workspace>>2", "workspacev2>>2,2", "createworkspace>>3", "createworkspacev2>>3,3",
                              "destroyworkspace>>3", "destroyworkspacev2>>3,3", "moveworkspace>>3,DP-3",
                              "moveworkspacev2>>3,3,DP-3", "focusedmon>>DP-3,2"})
            QVERIFY2(EventWatcher::isRelevant(e), e);
        QVERIFY(!EventWatcher::isRelevant("openwindow>>1,2,3,4"));
        QVERIFY(!EventWatcher::isRelevant("activewindow>>x"));
    }

    void eventWatcherDebouncesAndReconnects() {
        QTemporaryDir dir;
        const QString path = dir.filePath("socket2");
        QLocalServer server;
        QVERIFY(server.listen(path));
        EventWatcher watcher(path);
        QSignalSpy spy(&watcher, SIGNAL(refreshRequested()));
        QTRY_VERIFY(server.hasPendingConnections() || server.waitForNewConnection(100));
        auto *client = server.nextPendingConnection();
        QVERIFY(client);
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 2000);  // refresh on connect
        spy.clear();
        client->write("monitoradded>>DP-9\nmonitoraddedv2>>1,DP-9\nconfigreloaded>>\nworkspace>>1\n");
        client->flush();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        QTest::qWait(500);
        QCOMPARE(spy.count(), 1);  // three events, one refresh

        client->disconnectFromServer();
        spy.clear();
        QTRY_VERIFY_WITH_TIMEOUT(server.waitForNewConnection(100), 3000);  // reconnects with backoff
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 2000);
    }

    // ---- layout geometry --------------------------------------------------
private:
    static LayoutRect rect(const QString &name, int x, int y, int w, int h, int pw = 0, int ph = 0, double scale = 1) {
        LayoutRect r;
        r.name = name; r.x = x; r.y = y; r.w = w; r.h = h;
        r.pixelW = pw ? pw : w; r.pixelH = ph ? ph : h; r.scale = scale;
        return r;
    }
    // The layout the machine had before: DP-5 at 1920 and eDP-1 at 3840 leave a 384 px hole.
    static QList<LayoutRect> oldLayout() {
        return {rect("DP-3", 0, 0, 1536, 864, 1920, 1080, 1.25), rect("DP-5", 1920, 0, 1920, 1080),
                rect("eDP-1", 3840, 0, 1536, 864, 1920, 1080, 1.25)};
    }
    static QList<LayoutRect> currentLayout() {
        return {rect("DP-3", 0, 0, 1536, 864, 1920, 1080, 1.25), rect("DP-5", 1536, 0, 1920, 1080),
                rect("eDP-1", 3456, 0, 1536, 864, 1920, 1080, 1.25)};
    }
    static QList<LayoutIssue> ofKind(const QList<LayoutIssue> &all, LayoutIssue::Kind kind) {
        QList<LayoutIssue> out;
        for (const auto &i : all)
            if (i.kind == kind)
                out << i;
        return out;
    }

private slots:
    void geometryFindsTheOldGapWithScaleHint() {
        const auto issues = LayoutGeometry::analyze(oldLayout());
        const auto gaps = ofKind(issues, LayoutIssue::Gap);
        QCOMPARE(gaps.size(), 1);
        QCOMPARE(gaps[0].amount, 384);
        QCOMPARE(gaps[0].monitors, QStringList({"DP-3", "DP-5"}));
        QCOMPARE(gaps[0].region, QRect(1536, 0, 384, 864));
        QVERIFY2(gaps[0].hint.contains("DP-3 is 1536 logical px wide (1920 / 1.25)"), qPrintable(gaps[0].hint));
        QVERIFY(gaps[0].message().contains("384 px"));
        // DP-3 is cut off from everything; DP-5 and eDP-1 still touch.
        const auto isolated = ofKind(issues, LayoutIssue::Isolated);
        QCOMPARE(isolated.size(), 1);
        QCOMPARE(isolated[0].monitors, QStringList({"DP-3"}));
        QCOMPARE(isolated[0].amount, 384);
        QCOMPARE(ofKind(issues, LayoutIssue::Disconnected).size(), 0);
        QVERIFY(!LayoutGeometry::isConnected(oldLayout()));
    }

    void geometryCurrentLayoutHasNoWarningsOnlyPartialContact() {
        const auto issues = LayoutGeometry::analyze(currentLayout());
        for (const auto &i : issues)
            QVERIFY2(!i.isWarning(), qPrintable(i.message()));
        const auto partial = ofKind(issues, LayoutIssue::PartialContact);
        QCOMPARE(partial.size(), 2);
        QCOMPARE(partial[0].monitors, QStringList({"DP-3", "DP-5"}));
        QCOMPARE(partial[0].amount, 864);
        QCOMPARE(partial[0].sizeA, 864);
        QCOMPARE(partial[0].sizeB, 1080);
        QVERIFY(partial[0].message().contains("864"));
        QVERIFY(partial[0].message().contains("1080"));
        QVERIFY(LayoutGeometry::isConnected(currentLayout()));
    }

    void geometryEqualHeightsGiveNoPartialContact() {
        const QList<LayoutRect> r{rect("A", 0, 0, 100, 100), rect("B", 100, 0, 200, 100)};
        QVERIFY(LayoutGeometry::analyze(r).isEmpty());
        QVERIFY(LayoutGeometry::analyze({rect("A", 0, 0, 100, 100)}).isEmpty());
        QVERIFY(LayoutGeometry::analyze({}).isEmpty());
    }

    void geometryNoGapWhenAThirdMonitorIsInBetween() {
        // C sits in the hole between A and B, so there is no A-B gap (C touches both).
        const QList<LayoutRect> r{rect("A", 0, 0, 100, 100), rect("C", 100, 0, 50, 100), rect("B", 150, 0, 100, 100)};
        QVERIFY(ofKind(LayoutGeometry::analyze(r), LayoutIssue::Gap).isEmpty());
        // A third monitor that does not fill the hole still hides the A-B pair.
        const QList<LayoutRect> r2{rect("A", 0, 0, 100, 100), rect("C", 100, 0, 30, 100), rect("B", 150, 0, 100, 100)};
        const auto gaps = ofKind(LayoutGeometry::analyze(r2), LayoutIssue::Gap);
        QCOMPARE(gaps.size(), 1);  // C-B, 20 px
        QCOMPARE(gaps[0].monitors, QStringList({"C", "B"}));
        QCOMPARE(gaps[0].amount, 20);
    }

    void geometryVerticalGapAndNoGapForDiagonals() {
        const auto gaps = ofKind(LayoutGeometry::analyze({rect("A", 0, 0, 100, 100), rect("B", 0, 130, 100, 100)}), LayoutIssue::Gap);
        QCOMPARE(gaps.size(), 1);
        QCOMPARE(gaps[0].amount, 30);
        QVERIFY(!gaps[0].horizontal);
        // Diagonal: nothing lines up, so it is only an isolated pair, not a gap.
        const auto diag = LayoutGeometry::analyze({rect("A", 0, 0, 100, 100), rect("B", 150, 150, 100, 100)});
        QVERIFY(ofKind(diag, LayoutIssue::Gap).isEmpty());
        QCOMPARE(ofKind(diag, LayoutIssue::Isolated).size(), 2);
        // A corner touch is not a contact.
        QCOMPARE(LayoutGeometry::contact(rect("A", 0, 0, 100, 100), rect("B", 100, 100, 100, 100)), 0);
    }

    void geometryOverlaps() {
        const auto issues = LayoutGeometry::analyze({rect("A", 0, 0, 100, 100), rect("B", 60, 20, 100, 100)});
        const auto o = ofKind(issues, LayoutIssue::Overlap);
        QCOMPARE(o.size(), 1);
        QCOMPARE(o[0].region, QRect(60, 20, 40, 80));
        QCOMPARE(o[0].amount, 40);
        QVERIFY(LayoutGeometry::overlaps(rect("A", 0, 0, 100, 100), rect("B", 99, 99, 10, 10)));
        QVERIFY(!LayoutGeometry::overlaps(rect("A", 0, 0, 100, 100), rect("B", 100, 0, 10, 10)));
    }

    void geometryDisconnectedGroups() {
        const QList<LayoutRect> r{rect("A", 0, 0, 100, 100), rect("B", 100, 0, 100, 100),
                                  rect("C", 500, 0, 100, 100), rect("D", 600, 0, 100, 100)};
        const auto d = ofKind(LayoutGeometry::analyze(r), LayoutIssue::Disconnected);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].groups.size(), 2);
        QCOMPARE(d[0].groups[0], QStringList({"A", "B"}));
        QCOMPARE(d[0].groups[1], QStringList({"C", "D"}));
        QCOMPARE(LayoutGeometry::components(r).size(), 2);
    }

    void geometrySnapsEdgeToEdgeWithinThreshold() {
        const QList<LayoutRect> r{rect("A", 0, 0, 1536, 864), rect("B", 3000, 0, 1920, 1080)};
        // B's left edge 6 px away from A's right edge: snaps against it.
        auto s = LayoutGeometry::snap(r, 1, {1542, 0}, 10);
        QCOMPARE(s.pos, QPoint(1536, 0));
        QVERIFY(!s.guides.isEmpty());
        QVERIFY(s.guides.contains(SnapGuide{true, 1536, 0, 1080}));
        // 11 px away: no snap on x.
        s = LayoutGeometry::snap(r, 1, {1547, 400}, 10);
        QCOMPARE(s.pos, QPoint(1547, 400));
        // Bottom of B aligned with bottom of A when close enough, even though x is far.
        s = LayoutGeometry::snap(r, 1, {2000, -210}, 10);
        QCOMPARE(s.pos.y(), -216);  // B's bottom (h 1080) to A's bottom 864
        // Below A: top edge of B against A's bottom.
        s = LayoutGeometry::snap(r, 1, {5, 860}, 10);
        QCOMPARE(s.pos, QPoint(0, 864));  // x also aligns with A's left edge
    }

    void geometrySnapAdjacencyNeedsNearbyNeighbour() {
        // The far monitor is 1000 px above: its right edge must not pull B sideways.
        const QList<LayoutRect> r{rect("A", 0, -1000, 100, 100), rect("B", 500, 0, 100, 100)};
        const auto s = LayoutGeometry::snap(r, 1, {104, 0}, 10);
        QCOMPARE(s.pos, QPoint(104, 0));
    }

    void geometrySettlePushesOutOfOverlap() {
        const QList<LayoutRect> r{rect("A", 0, 0, 1000, 1000), rect("B", 3000, 0, 400, 400)};
        // Dropped over A, a little right of centre-left: nearest valid is to A's left or top edge.
        const QPoint p = LayoutGeometry::settle(r, 1, {900, 300});
        QCOMPARE(p, QPoint(1000, 300));  // pushed right by 100, still touching
        const QPoint q = LayoutGeometry::settle(r, 1, {300, 900});
        QCOMPARE(q, QPoint(300, 1000));  // pushed down
    }

    void geometrySettleClosesGapsAndKeepsPositionWhenValid() {
        const auto r = oldLayout();
        // DP-3 dropped in the hole with a 40 px gap: pulled to touch (nearest valid).
        QCOMPARE(LayoutGeometry::settle(r, 0, {344, 0}), QPoint(384, 0));
        // Already valid: unchanged (DP-3 at 400 would overlap; 384 touches).
        QCOMPARE(LayoutGeometry::settle(r, 0, {384, 0}), QPoint(384, 0));
        // Floating far away: ends up touching something, no overlap.
        auto spot = LayoutGeometry::settle(r, 0, {-5000, 3000});
        auto moved = r;
        moved[0].x = spot.x();
        moved[0].y = spot.y();
        QVERIFY(!LayoutGeometry::overlaps(moved[0], moved[1]));
        QVERIFY(LayoutGeometry::isConnected(moved));
        // Sliding along an edge keeps the desired coordinate (whole numbers).
        QCOMPARE(LayoutGeometry::settle({rect("A", 0, 0, 1000, 1000), rect("B", 5000, 5000, 400, 400)}, 1, {1000, 123}),
                 QPoint(1000, 123));
    }

    void geometrySettleKeepsEverythingConnected() {
        // B can only be valid if it bridges... A and C are apart; B must touch both when possible.
        const QList<LayoutRect> r{rect("A", 0, 0, 100, 100), rect("C", 300, 0, 100, 100), rect("B", 0, 500, 200, 100)};
        const QPoint p = LayoutGeometry::settle(r, 2, {100, 300});
        auto m = r;
        m[2].x = p.x();
        m[2].y = p.y();
        QVERIFY(LayoutGeometry::isConnected(m));
        QCOMPARE(p.x(), 100);  // the only column touching both
        QVERIFY(p.y() < 100);  // and it shares at least 1 px of edge with each
    }

    void backendDetectsOldLayoutProblems() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        bool gap = false, isolated = false;
        for (const auto &v : backend.issues()) {
            const auto m = v.toMap();
            gap = gap || (m["kind"] == "gap" && m["amount"] == 384 && m["hasRegion"] == true);
            isolated = isolated || (m["kind"] == "isolated" && m["monitors"].toStringList() == QStringList{"DP-3"});
        }
        QVERIFY(gap);
        QVERIFY(isolated);
        QVERIFY(!backend.hasPending());
        QVERIFY(backend.selected().value("movable").toBool());
        QVERIFY(backend.stateJson().value("issues").toArray().size() >= 2);
    }

    void backendDragPreviewsLiveAndCommitsOnRelease() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        const int dp3 = 1;
        QSignalSpy issuesSpy(&backend, &Backend::issuesChanged);
        const auto snap = backend.dragSnap(dp3, 380, 3, 10);  // DP-3's right edge lands on DP-5's left edge
        QCOMPARE(snap.value("x").toInt(), 384);
        QCOMPARE(snap.value("y").toInt(), 0);
        QVERIFY(!snap.value("guides").toList().isEmpty());
        QVERIFY(issuesSpy.count() >= 1);
        for (const auto &v : backend.issues())
            QVERIFY(v.toMap()["kind"] != "gap");  // the preview already closes the hole
        QVERIFY(!backend.hasPending());           // nothing pending until release
        QCOMPARE(backend.monitors()[dp3].toMap()["x"].toInt(), 0);

        backend.dragEnd(dp3, 380, 3, 10);
        QVERIFY(backend.hasPending());
        QCOMPARE(backend.pending().value("DP-3"), QPoint(384, 0));
        const auto m = backend.monitors()[dp3].toMap();
        QCOMPARE(m["x"].toInt(), 384);
        QCOMPARE(m["currentX"].toInt(), 0);
        QVERIFY(m["moved"].toBool());
        QCOMPARE(backend.pendingList().size(), 1);
        QCOMPARE(backend.pendingList()[0].toMap()["toX"].toInt(), 384);
    }

    void backendDragCancelLeavesNothingPending() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        backend.dragSnap(1, 500, 500, 10);
        backend.dragCancel();
        QVERIFY(!backend.hasPending());
        bool gap = false;
        for (const auto &v : backend.issues())
            gap = gap || v.toMap()["kind"] == "gap";
        QVERIFY(gap);  // back to the real layout's problems
    }

    void backendDropOnTopOfAnotherIsPushedAside() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        backend.dragEnd(1, 2200, 100, 10);  // DP-3 dropped on DP-5
        const QPoint p = backend.pending().value("DP-3");
        QList<LayoutRect> rects{rect("DP-3", p.x(), p.y(), 1536, 864), rect("DP-5", 1920, 0, 1920, 1080),
                                rect("eDP-1", 3840, 0, 1536, 864)};
        QVERIFY(!LayoutGeometry::overlaps(rects[0], rects[1]));
        QVERIFY(!LayoutGeometry::overlaps(rects[0], rects[2]));
        QVERIFY(LayoutGeometry::contact(rects[0], rects[1]) > 0 || LayoutGeometry::contact(rects[0], rects[2]) > 0);
    }

    void backendUndoRedoAndReset() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        QVERIFY(!backend.canUndo());
        backend.dragEnd(1, 380, 0, 10);              // DP-3 -> 384,0
        backend.setSelectedIndex(1);
        backend.moveSelected(0, 10);                 // -> 384,10
        QCOMPARE(backend.pending().value("DP-3"), QPoint(384, 10));
        backend.undo();
        QCOMPARE(backend.pending().value("DP-3"), QPoint(384, 0));
        backend.undo();
        QVERIFY(!backend.hasPending());
        QVERIFY(!backend.canUndo());
        QVERIFY(backend.canRedo());
        backend.redo();
        backend.redo();
        QCOMPARE(backend.pending().value("DP-3"), QPoint(384, 10));
        QVERIFY(!backend.canRedo());
        backend.resetPending();
        QVERIFY(!backend.hasPending());
        QVERIFY(backend.canUndo());                  // a reset can be undone
        backend.undo();
        QCOMPARE(backend.pending().value("DP-3"), QPoint(384, 10));
        // A new move after an undo drops the redo branch.
        backend.undo();
        backend.moveSelected(-5, 0);
        QCOMPARE(backend.pending().value("DP-3"), QPoint(379, 0));
        QVERIFY(!backend.canRedo());
        // Moving back onto the real spot (0,0) leaves nothing pending.
        backend.moveSelected(-379, 0);
        QVERIFY(!backend.hasPending());
        // Whole numbers always.
        backend.moveSelected(7, -3);
        QCOMPARE(backend.pending().value("DP-3"), QPoint(7, -3));
    }

    void backendKeepsPendingOnRefreshAndWarnsOnRealChange() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        write(json, fixture("three-monitors.json"));
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        backend.dragEnd(1, 380, 0, 10);
        backend.refresh();  // nothing changed: no warning
        QVERIFY(backend.hasPending());
        QVERIFY(backend.pendingNotice().isEmpty());
        // Hyprland now reports DP-5 elsewhere (e.g. the user edited monitors.lua).
        QByteArray changed = fixture("three-monitors.json");
        changed.replace("\"x\": 1920", "\"x\": 1600");
        write(json, changed);
        backend.refresh();
        QVERIFY(backend.hasPending());
        QCOMPARE(backend.pending().value("DP-3"), QPoint(384, 0));
        QVERIFY(!backend.pendingNotice().isEmpty());
        backend.dismissNotice();
        QVERIFY(backend.pendingNotice().isEmpty());
        backend.resetPending();
        QVERIFY(!backend.hasPending());
    }

    void backendDisabledAndMirrorMonitorsAreNotMovable() {
        QTemporaryDir dir;
        const QString json = dir.filePath("monitors.json");
        QByteArray data = fixture("three-monitors.json");
        data.replace("\"mirrorOf\": \"none\"", "\"mirrorOf\": \"DP-3\"");  // all three mirror something
        write(json, data);
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        QVERIFY(!backend.selected().value("movable").toBool());
        backend.moveSelected(10, 0);
        QVERIFY(!backend.hasPending());
        backend.dragEnd(1, 50, 50, 10);
        QVERIFY(!backend.hasPending());
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "es\n");
        write(dir.path() + "/es.json", R"({"Refresh": "Actualizar"})");
        Translator translator(dir.path() + "/language", dir.path(), nullptr, dir.path() + "/settings.json");
        QCOMPARE(translator.language(), QString("es"));
        QCOMPARE(translator.translate("Main", "Refresh"), QString("Actualizar"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void identifierMatchesScreensByName() {
        QString error;
        const auto monitors = MonitorParser::parse(fixture("three-monitors.json"), &error);
        const QList<ScreenDesc> screens{{"DP-3", "x", "y", "z"}, {"eDP-1", "", "", ""}, {"DP-5", "x", "y", "z"}};
        const auto m = Identifier::matchScreens(screens, monitors);
        QCOMPARE(monitors[m[0].monitorIndex].name, QString("DP-3"));
        QCOMPARE(monitors[m[1].monitorIndex].name, QString("eDP-1"));
        QCOMPARE(monitors[m[2].monitorIndex].name, QString("DP-5"));
        QVERIFY(m[0].byName && m[1].byName && m[2].byName);
    }

    void identifierFallsBackToMakeModelOnlyWhenUnique() {
        QString error;
        auto monitors = MonitorParser::parse(fixture("three-monitors.json"), &error);
        monitors[0].make = "AU Optronics";
        monitors[0].model = "0x403D";
        monitors[0].serial = "";
        // The internal panel is unique, so a renamed screen can be recognised.
        const auto &edp = monitors[0];
        auto m = Identifier::matchScreens({{"Unknown-1", edp.make, edp.model, edp.serial}}, monitors);
        QCOMPARE(monitors[m[0].monitorIndex].name, QString("eDP-1"));
        QVERIFY(!m[0].byName);
        // DP-3 and DP-5 are identical: renamed screens must not be guessed.
        const auto &dp = monitors[1];
        m = Identifier::matchScreens({{"A", dp.make, dp.model, dp.serial}, {"B", dp.make, dp.model, dp.serial}}, monitors);
        QCOMPARE(m[0].monitorIndex, -1);
        QCOMPARE(m[1].monitorIndex, -1);
        // Nothing to go on at all.
        m = Identifier::matchScreens({{"Z", "", "", ""}}, monitors);
        QCOMPARE(m[0].monitorIndex, -1);
    }

    void identifierWorksOffscreenWithoutLayerShell() {
        QString error;
        const auto monitors = MonitorParser::parse(fixture("three-monitors.json"), &error);
        Identifier identifier(&monitors);
        identifier.show("DP-3", 50);
        QVERIFY(identifier.active());
        QVERIFY(identifier.windowCount() >= 1);
        QTRY_VERIFY_WITH_TIMEOUT(!identifier.active(), 2000);
        QCOMPARE(identifier.windowCount(), 0);
    }

    // ---- workspaces ------------------------------------------------------------

    void workspacesParseRealState() {
        QString error;
        const auto list = Workspaces::parseWorkspaces(fixture("ws-real-workspaces.json"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(list.size(), 10);
        int specials = 0;
        for (const auto &w : list)
            specials += w.special;
        QCOMPARE(specials, 1);
        const auto rules = Workspaces::parseRules(fixture("ws-real-rules.json"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(rules.size(), 10);
        QCOMPARE(rules[0].workspace, QString("special:qconsole"));
        QCOMPARE(rules[0].id, -1);
        QCOMPARE(rules[1].id, 1);
        QCOMPARE(rules[1].monitor, QString("DP-3"));
        QVERIFY(rules[1].persistent && rules[1].isDefault);
        QVERIFY(rules[7].monitor.isEmpty());  // 17 has no monitor
        const auto monitors = MonitorParser::parse(fixture("ws-real-monitors.json"), &error);
        QCOMPARE(monitors[1].activeWorkspaceId, 11);
    }

    void workspacesRejectCorruptJson() {
        QString error;
        QVERIFY(Workspaces::parseWorkspaces(fixture("ws-corrupt.json"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        for (const char *bad : {"{}", "[1]", R"([{"id":1}])", R"([{"id":"1","name":"1","monitor":"A","windows":0}])",
                                R"([{"id":1,"name":"1","monitor":"A","windows":-1}])",
                                R"([{"id":1.5,"name":"1","monitor":"A","windows":0}])"}) {
            error.clear();
            QVERIFY2(Workspaces::parseWorkspaces(bad, &error).isEmpty(), bad);
            QVERIFY2(!error.isEmpty(), bad);
        }
        for (const char *bad : {"", "{}", "[1]", R"([{"monitor":"A"}])", "[{"}) {
            error.clear();
            QVERIFY2(Workspaces::parseRules(bad, &error).isEmpty(), bad);
            QVERIFY2(!error.isEmpty(), bad);
        }
        error.clear();
        QVERIFY(Workspaces::parseWorkspaces("[]", &error).isEmpty());
        QVERIFY(error.isEmpty());  // an empty list is valid
    }

    void blockStateParsingIsTolerant() {
        auto s = Workspaces::parseBlockState(fixture("ws-real-state.txt"));
        QCOMPARE(s.status, BlockState::Ok);
        QCOMPARE(s.entries.size(), 4);
        QCOMPARE(s.entries[2].connector, QString("DP-5"));
        QCOMPARE(s.entries[2].identity, QString("Technical Concepts Ltd MK27X7100 000000000000 #2"));
        QCOMPARE(s.entries[3].identity, QString("FALLBACK"));
        QVERIFY(s.entries[3].connector.isEmpty());
        // garbage lines and CRLF are skipped, good lines stay
        s = Workspaces::parseBlockState("junk\n0\teDP-1\tPanel X\r\nnot a number\tx\ty\n1\t\tOther\n\n");
        QCOMPARE(s.status, BlockState::Ok);
        QCOMPARE(s.entries.size(), 2);
        QCOMPARE(s.entries[0].identity, QString("Panel X"));
        // nothing usable, or the same block twice: not understood
        QCOMPARE(Workspaces::parseBlockState("").status, BlockState::Invalid);
        QCOMPARE(Workspaces::parseBlockState("hello world\n").status, BlockState::Invalid);
        QCOMPARE(Workspaces::parseBlockState(QByteArray("\xff\xfe\x00\x01", 4)).status, BlockState::Invalid);
        QCOMPARE(Workspaces::parseBlockState("0\tA\tx\n0\tB\ty\n").status, BlockState::Invalid);
    }

    void blockStateFileIsReadFromDisk() {
        QTemporaryDir dir;
        QCOMPARE(Workspaces::readBlockState(dir.filePath("nope")).status, BlockState::Missing);
        QCOMPARE(Workspaces::readBlockState(dir.path()).status, BlockState::Unreadable);  // a directory
        write(dir.filePath("state"), fixture("ws-real-state.txt"));
        QCOMPARE(Workspaces::readBlockState(dir.filePath("state")).status, BlockState::Ok);
        write(dir.filePath("bad"), "\x01\x02 nonsense");
        QCOMPARE(Workspaces::readBlockState(dir.filePath("bad")).status, BlockState::Invalid);
        // the default path honours the override (tests never use the real one)
        QCOMPARE(Workspaces::defaultStatePath(), QString("/nonexistent/arjuy-test/monitor-workspaces"));
    }

    void workspacesCombineRealState() {
        const auto s = realState();
        const auto model = Workspaces::build(s.monitors, s.workspaces, s.rules, s.blocks);
        QVERIFY(model.ok);
        QCOMPARE(model.fallbackBlock, 3);
        QCOMPARE(model.special.size(), 1);
        QCOMPARE(model.monitors.size(), 3);

        const auto *edp = model.forMonitor("eDP-1"), *dp3 = model.forMonitor("DP-3"), *dp5 = model.forMonitor("DP-5");
        QVERIFY(edp && dp3 && dp5);
        // Twin monitors (same make/model/serial) get their own blocks through the connector.
        QVERIFY(edp->block.known && dp3->block.known && dp5->block.known);
        QCOMPARE(edp->block.first, 1);
        QCOMPARE(edp->block.last, 10);
        QCOMPARE(dp3->block.first, 11);
        QCOMPARE(dp5->block.first, 21);
        QCOMPARE(dp5->block.last, 30);
        QCOMPARE(dp5->block.identity, QString("Technical Concepts Ltd MK27X7100 000000000000 #2"));

        QCOMPARE(idsOf(*edp), QList<int>({5, 6}));
        QCOMPARE(idsOf(*dp3), QList<int>({1, 2, 11, 12}));
        QCOMPARE(idsOf(*dp5), QList<int>({3, 4, 21}));
        QCOMPARE(dp3->activeId, 11);
        for (const auto &w : dp3->workspaces)
            QCOMPARE(w.active, w.id == 11);
        QVERIFY(dp3->workspaces[0].guest && dp3->workspaces[1].guest);
        QVERIFY(!dp3->workspaces[2].guest && !dp3->workspaces[3].guest);
        QVERIFY(dp3->workspaces[0].persistent && !dp3->workspaces[2].persistent);
        QVERIFY(dp3->workspaces[0].pinned);
        QCOMPARE(dp3->workspaces[0].pinnedTo, QString("DP-3"));
        QCOMPARE(dp3->workspaces[2].windows, 1);
        QVERIFY(!dp3->workspaces[3].pinned);  // 12 has a rule but no monitor
        QVERIFY(!edp->workspaces[0].guest && !edp->workspaces[1].guest);
        QVERIFY(edp->workspaces[0].active && !edp->workspaces[1].active);
        QVERIFY(dp5->workspaces[0].guest && !dp5->workspaces[2].guest);

        // Two notes: workspaces 1,2 on DP-3 and 3,4 on DP-5 belong to eDP-1's block.
        QCOMPARE(model.conflicts.size(), 2);
        for (const auto &c : model.conflicts) {
            QCOMPARE(c.kind, QString("pinned-outside-block"));
            QCOMPARE(c.monitors.value(1), QString("eDP-1"));
        }
        QCOMPARE(model.conflicts[0].monitors.value(0), QString("DP-3"));
        QCOMPARE(model.conflicts[0].ids, QList<int>({1, 2}));
        QCOMPARE(model.conflicts[1].monitors.value(0), QString("DP-5"));
        QCOMPARE(model.conflicts[1].ids, QList<int>({3, 4}));
        QVERIFY2(model.conflicts[0].message.contains("Workspaces 1, 2 are pinned to DP-3"), qPrintable(model.conflicts[0].message));
        QVERIFY(model.conflicts[0].message.contains("eDP-1's block (1-10)"));
    }

    void workspaceConflictSingularMessage() {
        auto s = realState();
        QList<WorkspaceRule> rules;
        for (const auto &r : s.rules)
            if (r.id == 1 || r.id < 0 || r.monitor.isEmpty())
                rules << r;
        const auto model = Workspaces::build(s.monitors, s.workspaces, rules, s.blocks);
        QCOMPARE(model.conflicts.size(), 1);
        QCOMPARE(model.conflicts[0].message,
                 QString("Workspace 1 is pinned to DP-3 by a workspace rule but belongs to eDP-1's block (1-10) in Omarchy."));
    }

    void workspaceUnknownBlockNeverFails() {
        const auto s = realState();
        for (const auto &blocks : {BlockState{}, BlockState{BlockState::Invalid, {}}, BlockState{BlockState::Unreadable, {}}}) {
            const auto model = Workspaces::build(s.monitors, s.workspaces, s.rules, blocks);
            QVERIFY(model.ok);
            QVERIFY(model.conflicts.isEmpty());  // no block to compare with
            for (const auto &m : model.monitors) {
                QVERIFY(!m.block.known);
                for (const auto &w : m.workspaces)
                    QVERIFY(!w.guest);
            }
            QCOMPARE(model.forMonitor("DP-3")->workspaces.size(), 4);
        }
        // A state file that does not list a monitor leaves just that one unknown.
        const auto partial = Workspaces::parseBlockState("0\teDP-1\tAU Optronics 0x403D\n");
        const auto model = Workspaces::build(s.monitors, s.workspaces, s.rules, partial);
        QVERIFY(model.forMonitor("eDP-1")->block.known);
        QVERIFY(!model.forMonitor("DP-3")->block.known);
    }

    void workspaceConflictsForWindowsDisconnectedAndDuplicates() {
        auto s = realState();
        // A window on workspace 2 (DP-3, whose block is 11-20), a rule to a monitor that is gone,
        // and workspace 12 listed twice.
        for (auto &w : s.workspaces)
            if (w.id == 2)
                w.windows = 3;
        WorkspaceRule gone;
        gone.workspace = "40";
        gone.id = 40;
        gone.monitor = "DP-9";
        s.rules << gone;
        WorkspaceInfo twin;
        twin.id = 12;
        twin.name = "12";
        twin.monitor = "DP-5";
        s.workspaces << twin;
        const auto model = Workspaces::build(s.monitors, s.workspaces, s.rules, s.blocks);
        QMap<QString, WorkspaceConflict> byKind;
        for (const auto &c : model.conflicts)
            byKind.insert(c.kind, c);
        QVERIFY(byKind.contains("windows-outside-block"));
        QCOMPARE(byKind["windows-outside-block"].ids, QList<int>({2}));
        QCOMPARE(byKind["windows-outside-block"].monitors, QStringList({"DP-3"}));
        QVERIFY(byKind["windows-outside-block"].message.contains("block is 11-20"));
        QVERIFY(byKind.contains("rule-disconnected"));
        QCOMPARE(byKind["rule-disconnected"].monitors, QStringList({"DP-9"}));
        QVERIFY(byKind["rule-disconnected"].message.contains("not connected"));
        QVERIFY(byKind.contains("duplicate-id"));
        QCOMPARE(byKind["duplicate-id"].ids, QList<int>({12}));
        QCOMPARE(byKind["duplicate-id"].monitors, QStringList({"DP-3", "DP-5"}));
        QVERIFY(byKind.contains("pinned-outside-block"));  // still there
    }

    void workspacesDescPinsAndIdenticalTwinsWithoutConnectorRows() {
        // Identical twins listed only by identity (unplugged): the connector row decides when present.
        auto s = realState();
        s.rules.clear();
        WorkspaceRule byDesc;
        byDesc.workspace = "21";
        byDesc.id = 21;
        byDesc.monitor = "desc:AU Optronics 0x403D";  // resolves to eDP-1, but 21 is DP-5's
        s.rules << byDesc;
        const auto model = Workspaces::build(s.monitors, s.workspaces, s.rules, s.blocks);
        QCOMPARE(model.conflicts.size(), 1);
        QCOMPARE(model.conflicts[0].monitors, QStringList({"eDP-1", "DP-5"}));
        // Without connector rows, two identical connected monitors are ambiguous: block unknown, no crash.
        const auto ambiguous = Workspaces::parseBlockState(
            "1\t\tTechnical Concepts Ltd MK27X7100 000000000000\n2\t\tTechnical Concepts Ltd MK27X7100 000000000000 #2\n");
        const auto m2 = Workspaces::build(s.monitors, s.workspaces, {}, ambiguous);
        QVERIFY(!m2.forMonitor("DP-3")->block.known);
        QVERIFY(!m2.forMonitor("DP-5")->block.known);
        // With only one of the twins connected there is no doubt: the exact identity wins.
        const auto m3 = Workspaces::build({s.monitors[1]}, s.workspaces, {}, ambiguous);
        QVERIFY(m3.forMonitor("DP-3")->block.known);
        QCOMPARE(m3.forMonitor("DP-3")->block.first, 11);
    }

    void workspacesReadWithFakeHyprctl() {
        QTemporaryDir dir;
        QString error;
        const auto monitors = MonitorParser::parse(fixture("ws-real-monitors.json"), &error);
        write(dir.filePath("state"), fixture("ws-real-state.txt"));
        const QString ok = fakeWorkspaceHyprctl(dir, fixturePath("ws-real-monitors.json"),
                                                "cat '" + fixturePath("ws-real-workspaces.json") + "'",
                                                "cat '" + fixturePath("ws-real-rules.json") + "'");
        auto model = Workspaces::read(ok, monitors, dir.filePath("state"));
        QVERIFY2(model.error.isEmpty(), qPrintable(model.error));
        QVERIFY(model.ok);
        QCOMPARE(model.conflicts.size(), 2);
        QCOMPARE(model.blockStatus, BlockState::Ok);

        // rules fail: workspaces still shown, error mentioned, no conflicts
        const QString noRules = fakeWorkspaceHyprctl(dir, fixturePath("ws-real-monitors.json"),
                                                     "cat '" + fixturePath("ws-real-workspaces.json") + "'", "exit 3");
        model = Workspaces::read(noRules, monitors, dir.filePath("state"));
        QVERIFY(model.ok);
        QVERIFY(!model.error.isEmpty());
        QVERIFY(model.conflicts.isEmpty());

        // workspaces corrupt: not ok, nothing shown
        const QString bad = fakeWorkspaceHyprctl(dir, fixturePath("ws-real-monitors.json"),
                                                 "cat '" + fixturePath("ws-corrupt.json") + "'",
                                                 "cat '" + fixturePath("ws-real-rules.json") + "'");
        model = Workspaces::read(bad, monitors, dir.filePath("state"));
        QVERIFY(!model.ok);
        QVERIFY(!model.error.isEmpty());
        QVERIFY(model.monitors.isEmpty());

        // missing hyprctl
        model = Workspaces::read(dir.filePath("does-not-exist"), monitors, dir.filePath("state"));
        QVERIFY(!model.ok);
        QVERIFY(!model.error.isEmpty());
    }

    void backendExposesWorkspaces() {
        QTemporaryDir dir;
        write(dir.filePath("state"), fixture("ws-real-state.txt"));
        qputenv("ARJUY_OMARCHY_WORKSPACES", dir.filePath("state").toUtf8());
        const QString hypr = fakeWorkspaceHyprctl(dir, fixturePath("ws-real-monitors.json"),
                                                  "cat '" + fixturePath("ws-real-workspaces.json") + "'",
                                                  "cat '" + fixturePath("ws-real-rules.json") + "'");
        Backend backend(hypr, fakePacman(dir, "omarchy 4.5.0-3"), false);
        qputenv("ARJUY_OMARCHY_WORKSPACES", QByteArray("/nonexistent/arjuy-test/monitor-workspaces"));
        QVERIFY2(backend.error().isEmpty(), qPrintable(backend.error()));
        QCOMPARE(backend.workspaceNotices().size(), 2);
        QCOMPARE(backend.specialWorkspaces().size(), 1);
        const auto dp3 = backend.monitors()[1].toMap();
        QCOMPARE(dp3.value("name").toString(), QString("DP-3"));
        QVERIFY(dp3.value("blockKnown").toBool());
        QCOMPARE(dp3.value("blockFirst").toInt(), 11);
        const auto chips = dp3.value("workspaces").toList();
        QCOMPARE(chips.size(), 4);
        QVERIFY(chips[0].toMap().value("guest").toBool());
        QVERIFY(chips[2].toMap().value("active").toBool());
        const auto json = backend.stateJson().value("workspaces").toObject();
        QVERIFY(json.value("ok").toBool());
        QCOMPARE(json.value("conflicts").toArray().size(), 2);
        QCOMPARE(json.value("monitors").toArray().size(), 3);

        // A hyprctl that cannot list workspaces does not break the monitors.
        Backend plain(fakeHyprctl(dir, "cat '" + fixturePath("three-monitors.json") + "'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        QVERIFY(plain.error().isEmpty());
        QCOMPARE(plain.monitors().size(), 3);
        QVERIFY(!plain.workspaceError().isEmpty());
        QVERIFY(plain.workspaceNotices().isEmpty());
        QVERIFY(plain.monitors()[0].toMap().value("workspaces").toList().isEmpty());
    }


    // ---- language selector -------------------------------------------------

    void languageSettingsPersist() {
        QTemporaryDir dir;
        const QString settings = dir.filePath("state/arjuy-displays/settings.json");  // folder created on demand
        write(dir.filePath("language"), "en\n");
        {
            Translator t(dir.filePath("language"), ":/i18n", nullptr, settings);
            QCOMPARE(t.mode(), QString("auto"));
            QSignalSpy modeSpy(&t, &Translator::modeChanged), changedSpy(&t, &Translator::changed);
            t.setMode("es");
            QCOMPARE(t.language(), QString("es"));
            QCOMPARE(modeSpy.count(), 1);
            QCOMPARE(changedSpy.count(), 1);
            t.setMode("es");  // no change, no signal
            t.setMode("fr");  // unknown value is ignored
            QCOMPARE(t.mode(), QString("es"));
            QCOMPARE(modeSpy.count(), 1);
        }
        QFile file(settings);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        QCOMPARE(doc.object().value("language").toString(), QString("es"));
        // Only the settings file is left behind (the temporary file was renamed).
        QCOMPARE(QDir(QFileInfo(settings).absolutePath()).entryList(QDir::Files).size(), 1);

        Translator again(dir.filePath("language"), ":/i18n", nullptr, settings);
        QCOMPARE(again.mode(), QString("es"));
        QCOMPARE(again.language(), QString("es"));  // forced, although Omarchy says en
        again.setMode("auto");
        QCOMPARE(again.language(), QString("en"));
        QCOMPARE(Translator::readMode(settings), QString("auto"));
    }

    void languageSettingsToleratesAbsentOrCorruptFiles() {
        QTemporaryDir dir;
        const QString settings = dir.filePath("settings.json");
        QCOMPARE(Translator::readMode(settings), QString("auto"));  // absent
        for (const char *bad : {"", "not json", "[]", "{}", R"({"language": "fr"})", R"({"language": 5})",
                                R"({"language": null})", "{\"language\": \"es\""}) {
            write(settings, bad);
            QCOMPARE(Translator::readMode(settings), QString("auto"));
            Translator t(dir.filePath("language"), ":/i18n", nullptr, settings);
            QCOMPARE(t.mode(), QString("auto"));
        }
        // A corrupt file is replaced by a valid one on the next choice.
        write(settings, "garbage");
        Translator t(dir.filePath("language"), ":/i18n", nullptr, settings);
        t.setMode("en");
        QCOMPARE(Translator::readMode(settings), QString("en"));
        // Other keys in the file survive.
        write(settings, R"({"language": "es", "other": 1})");
        QVERIFY(Translator::writeMode(settings, "en"));
        QFile file(settings);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(object.value("other").toInt(), 1);
        QCOMPARE(object.value("language").toString(), QString("en"));
        QVERIFY(!Translator::writeMode(settings, "klingon"));
    }

    void languageAutoFollowsOmarchyFile() {
        QTemporaryDir dir;
        const QString language = dir.filePath("language"), settings = dir.filePath("settings.json");
        write(language, "es\n");
        Translator t(language, ":/i18n", nullptr, settings);
        QCOMPARE(t.mode(), QString("auto"));
        QCOMPARE(t.language(), QString("es"));
        QCOMPARE(t.translate("Main", "Refresh"), QString("Actualizar"));
        // Omarchy changes its language: Auto follows, live.
        write(language, "en\n");
        QTRY_COMPARE_WITH_TIMEOUT(t.language(), QString("en"), 3000);
        QVERIFY(t.translate("Main", "Refresh").isNull());
        // A forced language ignores the Omarchy file.
        t.setMode("es");
        write(language, "de\n");
        QTest::qWait(300);
        QCOMPARE(t.language(), QString("es"));
        // No Omarchy language file at all: Auto is English.
        t.setMode("auto");
        QFile::remove(language);
        QTRY_COMPARE_WITH_TIMEOUT(t.language(), QString("en"), 3000);
    }

    void languageEnvironmentOverridesPaths() {
        QCOMPARE(Translator::defaultSettingsPath(), QString("/nonexistent/arjuy-test/settings.json"));
        QCOMPARE(Translator::defaultLanguageFile(), QString("/nonexistent/arjuy-test/language"));
    }

    void languageTransientModeIsNotSaved() {
        QTemporaryDir dir;
        const QString settings = dir.filePath("settings.json");
        write(dir.filePath("language"), "en\n");
        Translator t(dir.filePath("language"), ":/i18n", nullptr, settings);
        t.setTransientMode("es");
        QCOMPARE(t.language(), QString("es"));
        t.cycleMode();  // later choices of this run are not saved either
        QVERIFY(!QFile::exists(settings));
    }

    void languageCyclesAutoEnglishSpanish() {
        QTemporaryDir dir;
        write(dir.filePath("language"), "en\n");
        Translator t(dir.filePath("language"), ":/i18n", nullptr, dir.filePath("settings.json"));
        QStringList seen{t.mode()};
        for (int i = 0; i < 3; ++i) {
            t.cycleMode();
            seen << t.mode();
        }
        QCOMPARE(seen, (QStringList{"auto", "en", "es", "auto"}));
    }

    void languageChangesBackendMessagesLive() {
        QTemporaryDir dir;
        write(dir.filePath("language"), "en\n");
        const QString json = fixturePath("three-monitors.json");
        Translator t(dir.filePath("language"), ":/i18n", nullptr, dir.filePath("settings.json"));
        QCoreApplication::installTranslator(&t);
        // Untested Omarchy version => a warning built in C++.
        Backend backend(fakeHyprctl(dir, "cat '" + json + "'"), fakePacman(dir, "omarchy 9.0.0-1"), false);
        connect(&t, &Translator::changed, &backend, &Backend::retranslate);
        QSignalSpy spy(&backend, &Backend::warningsChanged);
        QVERIFY(backend.warnings().join("\n").contains("has not been tested"));

        t.setMode("es");
        QVERIFY(spy.count() >= 1);
        QVERIFY2(backend.warnings().join("\n").contains("no fue probado"), qPrintable(backend.warnings().join("\n")));
        QVERIFY(!backend.warnings().join("\n").contains("has not been tested"));
        t.setMode("en");
        QVERIFY(backend.warnings().join("\n").contains("has not been tested"));

        // The error shown for a bad hyprctl answer follows too.
        Backend broken(fakeHyprctl(dir, "echo 'not json at all'"), fakePacman(dir, "omarchy 4.5.0-3"), false);
        connect(&t, &Translator::changed, &broken, &Backend::retranslate);
        QVERIFY(broken.error().contains("not valid JSON"));
        t.setMode("es");
        QVERIFY2(broken.error().contains("no es JSON válido"), qPrintable(broken.error()));
        QCoreApplication::removeTranslator(&t);
    }

    void languageChangesQmlTextLive() {
        QTemporaryDir dir;
        write(dir.filePath("language"), "en\n");
        Translator t(dir.filePath("language"), ":/i18n", nullptr, dir.filePath("settings.json"));
        QCoreApplication::installTranslator(&t);
        QQmlEngine engine;
        connect(&t, &Translator::changed, &engine, &QQmlEngine::retranslate);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nQtObject { property string text: qsTr(\"Refresh\")\n"
                          "property string plural: qsTr(\"Available modes (%n)\", \"\", 3) }", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("text").toString(), QString("Refresh"));
        QCOMPARE(object->property("plural").toString(), QString("Available modes (3)"));
        t.setMode("es");
        QCOMPARE(object->property("text").toString(), QString("Actualizar"));
        QCOMPARE(object->property("plural").toString(), QString("Modos disponibles (3)"));
        t.setMode("en");
        QCOMPARE(object->property("text").toString(), QString("Refresh"));
        QCoreApplication::removeTranslator(&t);
    }

    // ---- string audit ------------------------------------------------------

    // Every literal passed to qsTr(), tr() or translate() in src/.
    static QSet<QString> sourceStrings(QStringList *whereOut = nullptr) {
        QSet<QString> found;
        const QDir src(QString(SOURCE_DIR) + "/../src");
        static const QRegularExpression call(R"((?<![A-Za-z0-9_])(qsTr|tr|translate)\s*\()");
        for (const QString &name : src.entryList({"*.cpp", "*.h", "*.qml"}, QDir::Files)) {
            QFile file(src.filePath(name));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QString text = QString::fromUtf8(file.readAll());
            auto skipSpace = [&](int &i) { while (i < text.size() && text[i].isSpace()) ++i; };
            // Reads adjacent "a" "b" literals (C++ concatenation) starting at i.
            auto literals = [&](int &i, QString *out) {
                bool any = false;
                skipSpace(i);
                while (i < text.size() && (text[i] == '"' || (text[i] == '\'' && name.endsWith(".qml")))) {
                    const QChar quote = text[i++];
                    for (; i < text.size() && text[i] != quote; ++i) {
                        if (text[i] != '\\') { *out += text[i]; continue; }
                        const QChar e = text[++i];
                        if (e == 'n') *out += '\n';
                        else if (e == 't') *out += '\t';
                        else if (e == 'u') { *out += QChar(text.mid(i + 1, 4).toUShort(nullptr, 16)); i += 4; }
                        else *out += e;
                    }
                    ++i;
                    any = true;
                    skipSpace(i);
                }
                return any;
            };
            auto it = call.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                // Ignore examples inside comments (// line and /* block */).
                const int lineStart = text.lastIndexOf('\n', m.capturedStart()) + 1;
                const QString before = text.left(m.capturedStart());
                const QString linePrefix = text.mid(lineStart, m.capturedStart() - lineStart);
                const int slashes = linePrefix.indexOf("//");
                if ((slashes >= 0 && linePrefix.left(slashes).count('"') % 2 == 0)
                    || before.lastIndexOf("/*") > before.lastIndexOf("*/"))
                    continue;
                int i = m.capturedEnd();
                skipSpace(i);
                if (m.captured(1) == "translate") {  // translate("Context", "text")
                    QString context;
                    if (!literals(i, &context)) continue;
                    skipSpace(i);
                    if (i >= text.size() || text[i] != ',') continue;
                    ++i;
                }
                QString source;
                if (literals(i, &source) && !source.isEmpty()) {
                    found.insert(source);
                    if (whereOut) whereOut->append(source + "  [" + name + "]");
                }
            }
        }
        return found;
    }
    static QJsonObject catalog(const char *code) {
        QFile file(QString(SOURCE_DIR) + "/../src/i18n/" + code + ".json");
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }
    static QSet<QString> placeholders(const QString &text) {
        QSet<QString> set;
        static const QRegularExpression re("%(\\d+|n)");
        auto it = re.globalMatch(text);
        while (it.hasNext()) set.insert(it.next().captured(0));
        return set;
    }

    void everySourceStringIsTranslatedToSpanish() {
        const QSet<QString> sources = sourceStrings();
        QVERIFY2(sources.size() > 100, "the string extractor found too few strings");
        const QJsonObject es = catalog("es"), en = catalog("en");
        QStringList problems;
        for (const QString &source : sources) {
            const QJsonValue value = es.value(source);
            const bool counted = source.contains("%n");
            QStringList forms;
            if (counted) {
                if (!value.isObject() || value.toObject().value("one").toString().isEmpty()
                    || value.toObject().value("other").toString().isEmpty()) {
                    problems << "es.json needs one/other forms for: " + source;
                    continue;
                }
                const auto o = en.value(source).toObject();
                if (o.value("one").toString().isEmpty() || o.value("other").toString().isEmpty())
                    problems << "en.json needs one/other forms for: " + source;
                for (const QString &key : value.toObject().keys())
                    forms << value.toObject().value(key).toString();
            } else {
                if (!value.isString() || value.toString().isEmpty()) {
                    problems << "missing in es.json: " + source;
                    continue;
                }
                forms << value.toString();
            }
            QSet<QString> expected = placeholders(source);
            if (counted) expected.remove("%n");
            for (const QString &form : forms) {
                QSet<QString> got = placeholders(form);
                got.remove("%n");
                if (got != expected)
                    problems << "placeholders differ in es.json for: " + source;
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable("\n" + problems.join("\n")));
    }

    void catalogsHaveNoOrphanEntries() {
        const QSet<QString> sources = sourceStrings();
        QStringList orphans;
        for (const char *code : {"es", "en"}) {
            const QJsonObject object = catalog(code);
            QVERIFY2(!object.isEmpty(), code);
            for (const QString &key : object.keys())
                if (!sources.contains(key))
                    orphans << QString("%1.json has an entry no source uses: %2").arg(code, key);
        }
        QVERIFY2(orphans.isEmpty(), qPrintable("\n" + orphans.join("\n")));
    }

    void languageSelectorStringsAreTranslated() {
        // Spot check of the strings added with the selector.
        const QJsonObject es = catalog("es");
        QCOMPARE(es.value("Language: %1").toString(), QString("Idioma: %1"));
        QVERIFY(es.contains("Auto"));
    }

    void bundledCatalogsAreValid() {
        for (const char *code : {"en", "es"}) {
            QFile file(QString(SOURCE_DIR) + "/../src/i18n/" + code + ".json");
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            QVERIFY(QJsonDocument::fromJson(file.readAll(), &error).isObject());
            QCOMPARE(error.error, QJsonParseError::NoError);
        }
    }
};

// The offscreen platform has no GPU: render the banner test in software.
static const bool softwareQuick = qputenv("QT_QUICK_BACKEND", "software");

QTEST_MAIN(DisplaysTests)
#include "arjuy-displays_tests.moc"
