#include <QDir>
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

private slots:
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
        QVERIFY(!EventWatcher::isRelevant("workspace>>2"));
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
        Translator translator(dir.path() + "/language", dir.path());
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
