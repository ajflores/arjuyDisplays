#include "layoutgeometry.h"

#include <QHash>
#include <QSet>
#include <QtGlobal>
#include <algorithm>
#include <limits>

static int overlapLength(int a1, int a2, int b1, int b2) {
    return qMax(0, qMin(a2, b2) - qMax(a1, b1));
}

bool LayoutGeometry::overlaps(const LayoutRect &a, const LayoutRect &b) {
    return overlapLength(a.x, a.right(), b.x, b.right()) > 0 && overlapLength(a.y, a.bottom(), b.y, b.bottom()) > 0;
}

int LayoutGeometry::contact(const LayoutRect &a, const LayoutRect &b) {
    if (a.right() == b.x || b.right() == a.x)
        return overlapLength(a.y, a.bottom(), b.y, b.bottom());
    if (a.bottom() == b.y || b.bottom() == a.y)
        return overlapLength(a.x, a.right(), b.x, b.right());
    return 0;
}

QList<QList<int>> LayoutGeometry::components(const QList<LayoutRect> &rects) {
    const int n = rects.size();
    QList<int> parent(n);
    for (int i = 0; i < n; ++i)
        parent[i] = i;
    auto find = [&](int i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (contact(rects[i], rects[j]) > 0 || overlaps(rects[i], rects[j]))
                parent[find(i)] = find(j);
    QList<QList<int>> out;
    QHash<int, int> slot;
    for (int i = 0; i < n; ++i) {
        const int root = find(i);
        if (!slot.contains(root)) {
            slot.insert(root, out.size());
            out.append(QList<int>());
        }
        out[slot[root]].append(i);
    }
    return out;
}

bool LayoutGeometry::isConnected(const QList<LayoutRect> &rects) {
    return components(rects).size() <= 1;
}

// ---- issues ----------------------------------------------------------------

QString LayoutIssue::kindName() const {
    switch (kind) {
    case Overlap: return "overlap";
    case Gap: return "gap";
    case Isolated: return "isolated";
    case Disconnected: return "disconnected";
    case PartialContact: return "partial";
    }
    return {};
}

QString LayoutIssue::message() const {
    const QString a = monitors.value(0), b = monitors.value(1);
    switch (kind) {
    case Overlap:
        return LayoutGeometry::tr("%1 and %2 overlap by %3 px.").arg(a, b).arg(amount);
    case Gap:
        return LayoutGeometry::tr("There is a %1 px gap between %2 and %3: the cursor cannot cross it.")
            .arg(amount).arg(a, b);
    case Isolated:
        return LayoutGeometry::tr("%1 touches no other monitor: the cursor cannot reach it.").arg(a);
    case Disconnected: {
        QStringList parts;
        for (const auto &g : groups)
            parts << g.join(", ");
        return LayoutGeometry::tr("The monitors form separate groups (%1): the cursor cannot move between them.")
            .arg(parts.join(" | "));
    }
    case PartialContact:
        return (horizontal ? LayoutGeometry::tr("%1 (%4 px tall) and %2 (%5 px tall) share only %3 px of edge: the cursor crosses only there.")
                           : LayoutGeometry::tr("%1 (%4 px wide) and %2 (%5 px wide) share only %3 px of edge: the cursor crosses only there."))
            .arg(a, b).arg(amount).arg(sizeA).arg(sizeB);
    }
    return {};
}

QVariantMap LayoutIssue::toVariant() const {
    return {
        {"kind", kindName()},
        {"severity", isWarning() ? "warning" : "info"},
        {"monitors", monitors},
        {"amount", amount},
        {"hasRegion", !region.isNull()},
        {"x", region.x()}, {"y", region.y()}, {"w", region.width()}, {"h", region.height()},
        {"hint", hint},
        {"message", message()},
    };
}

static QString scaleText(double scale) {
    return QString::number(scale, 'g', 4);
}

// "DP-3 is 1536 logical px wide (1920 / 1.25)" when the scale explains a gap.
static QString scaleHint(const LayoutRect &r, bool horizontal, int gap) {
    const int logical = horizontal ? r.w : r.h, pixels = horizontal ? r.pixelW : r.pixelH;
    if (r.scale == 1.0 || pixels - logical != gap)
        return {};
    return horizontal ? LayoutGeometry::tr("%1 is %2 logical px wide (%3 / %4), not %3.")
                            .arg(r.name).arg(logical).arg(pixels).arg(scaleText(r.scale))
                      : LayoutGeometry::tr("%1 is %2 logical px tall (%3 / %4), not %3.")
                            .arg(r.name).arg(logical).arg(pixels).arg(scaleText(r.scale));
}

QList<LayoutIssue> LayoutGeometry::analyze(const QList<LayoutRect> &rects) {
    QList<LayoutIssue> overlapsOut, gaps, isolated, disconnected, partial;
    const int n = rects.size();
    if (n < 2)
        return {};

    QList<int> touching(n, 0);
    QList<int> nearest(n, std::numeric_limits<int>::max());

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            const LayoutRect &p = rects[i], &q = rects[j];

            if (overlaps(p, q)) {
                LayoutIssue issue;
                issue.kind = LayoutIssue::Overlap;
                issue.monitors = {p.name, q.name};
                const int ow = overlapLength(p.x, p.right(), q.x, q.right());
                const int oh = overlapLength(p.y, p.bottom(), q.y, q.bottom());
                issue.amount = qMin(ow, oh);
                issue.region = QRect(qMax(p.x, q.x), qMax(p.y, q.y), ow, oh);
                overlapsOut << issue;
                touching[i]++;
                touching[j]++;
                nearest[i] = nearest[j] = 0;
                continue;
            }

            const int c = contact(p, q);
            if (c > 0) {
                touching[i]++;
                touching[j]++;
                nearest[i] = nearest[j] = 0;
                const bool horiz = p.right() == q.x || q.right() == p.x;
                const int sizeP = horiz ? p.h : p.w, sizeQ = horiz ? q.h : q.w;
                // Only worth a note when the shared edges differ in extent.
                const bool sameSpan = horiz ? (p.y == q.y && p.h == q.h) : (p.x == q.x && p.w == q.w);
                if (!sameSpan) {
                    LayoutIssue issue;
                    issue.kind = LayoutIssue::PartialContact;
                    const bool pFirst = horiz ? p.x < q.x : p.y < q.y;
                    issue.monitors = pFirst ? QStringList{p.name, q.name} : QStringList{q.name, p.name};
                    issue.sizeA = pFirst ? sizeP : sizeQ;
                    issue.sizeB = pFirst ? sizeQ : sizeP;
                    issue.amount = c;
                    issue.horizontal = horiz;
                    partial << issue;
                }
                continue;
            }

            // Gap: neighbours along one axis whose ranges overlap on the other
            // axis, with nothing in the space between them.
            auto tryGap = [&](const LayoutRect &first, const LayoutRect &second, bool horiz) {
                const int gapStart = horiz ? first.right() : first.bottom();
                const int gapEnd = horiz ? second.x : second.y;
                if (gapEnd <= gapStart)
                    return false;
                const int lo = horiz ? qMax(first.y, second.y) : qMax(first.x, second.x);
                const int hi = horiz ? qMin(first.bottom(), second.bottom()) : qMin(first.right(), second.right());
                if (hi <= lo)
                    return false;
                const QRect region = horiz ? QRect(gapStart, lo, gapEnd - gapStart, hi - lo)
                                           : QRect(lo, gapStart, hi - lo, gapEnd - gapStart);
                for (int k = 0; k < n; ++k) {
                    if (k == i || k == j)
                        continue;
                    const QRect other(rects[k].x, rects[k].y, rects[k].w, rects[k].h);
                    if (!other.intersected(region).isEmpty())
                        return false;
                }
                LayoutIssue issue;
                issue.kind = LayoutIssue::Gap;
                issue.monitors = {first.name, second.name};
                issue.amount = gapEnd - gapStart;
                issue.region = region;
                issue.horizontal = horiz;
                issue.hint = scaleHint(first, horiz, issue.amount);
                if (issue.hint.isEmpty())
                    issue.hint = scaleHint(second, horiz, issue.amount);
                gaps << issue;
                return true;
            };
            if (p.right() < q.x) tryGap(p, q, true);
            else if (q.right() < p.x) tryGap(q, p, true);
            else if (p.bottom() < q.y) tryGap(p, q, false);
            else if (q.bottom() < p.y) tryGap(q, p, false);

            const int dx = qMax(qMax(p.x - q.right(), q.x - p.right()), 0);
            const int dy = qMax(qMax(p.y - q.bottom(), q.y - p.bottom()), 0);
            const int dist = qMax(dx, dy);
            nearest[i] = qMin(nearest[i], dist);
            nearest[j] = qMin(nearest[j], dist);
        }
    }

    for (int i = 0; i < n; ++i) {
        if (touching[i] == 0) {
            LayoutIssue issue;
            issue.kind = LayoutIssue::Isolated;
            issue.monitors = {rects[i].name};
            issue.amount = nearest[i] == std::numeric_limits<int>::max() ? 0 : nearest[i];
            isolated << issue;
        }
    }

    const auto comps = components(rects);
    int multi = 0;
    for (const auto &c : comps)
        multi += c.size() >= 2 ? 1 : 0;
    // Lone monitors are already reported as isolated; groups only add news
    // when at least two real groups exist.
    if (multi >= 2) {
        LayoutIssue issue;
        issue.kind = LayoutIssue::Disconnected;
        for (const auto &c : comps) {
            QStringList names;
            for (int i : c)
                names << rects[i].name;
            issue.groups << names;
            issue.monitors << names;
        }
        disconnected << issue;
    }

    return overlapsOut + gaps + isolated + disconnected + partial;
}

// ---- snapping --------------------------------------------------------------

SnapResult LayoutGeometry::snap(const QList<LayoutRect> &rects, int moving, QPoint proposed, int threshold) {
    SnapResult result;
    result.pos = proposed;
    if (moving < 0 || moving >= rects.size())
        return result;
    const LayoutRect &m = rects[moving];
    int x = proposed.x(), y = proposed.y();

    // One axis: pick the smallest correction among aligned edges (always) and
    // edges placed against each other (only for neighbours near on the other axis).
    auto best = [&](bool horizontal, int pos, int otherPos) {
        const int len = horizontal ? m.w : m.h, otherLen = horizontal ? m.h : m.w;
        int bestDelta = 0, bestAbs = std::numeric_limits<int>::max();
        for (int k = 0; k < rects.size(); ++k) {
            if (k == moving)
                continue;
            const LayoutRect &o = rects[k];
            const int lo = horizontal ? o.x : o.y, hi = horizontal ? o.right() : o.bottom();
            const int olo = horizontal ? o.y : o.x, ohi = horizontal ? o.bottom() : o.right();
            const bool near = otherPos < ohi + threshold && otherPos + otherLen > olo - threshold;
            const int deltas[4] = {lo - pos, hi - (pos + len), hi - pos, lo - (pos + len)};
            for (int d = 0; d < 4; ++d) {
                if (d >= 2 && !near)
                    continue;
                const int a = qAbs(deltas[d]);
                if (a <= threshold && a < bestAbs) {
                    bestAbs = a;
                    bestDelta = deltas[d];
                }
            }
        }
        return bestAbs == std::numeric_limits<int>::max() ? 0 : bestDelta;
    };
    y += best(false, y, x);
    x += best(true, x, y);
    result.pos = {x, y};

    auto add = [&](SnapGuide g) {
        if (!result.guides.contains(g))
            result.guides << g;
    };
    const int mTop = y, mBottom = y + m.h, mLeft = x, mRight = x + m.w;
    for (int k = 0; k < rects.size(); ++k) {
        if (k == moving)
            continue;
        const LayoutRect &o = rects[k];
        const int spanFrom = qMin(mTop, o.y), spanTo = qMax(mBottom, o.bottom());
        const int spanFromX = qMin(mLeft, o.x), spanToX = qMax(mRight, o.right());
        const bool yNear = mTop <= o.bottom() + threshold && mBottom >= o.y - threshold;
        const bool xNear = mLeft <= o.right() + threshold && mRight >= o.x - threshold;
        for (int line : {mLeft, mRight}) {
            const bool aligned = line == o.x || line == o.right();
            const bool against = (line == mLeft && line == o.right()) || (line == mRight && line == o.x);
            if ((aligned && !against) || (against && yNear))
                add({true, line, spanFrom, spanTo});
        }
        for (int line : {mTop, mBottom}) {
            const bool aligned = line == o.y || line == o.bottom();
            const bool against = (line == mTop && line == o.bottom()) || (line == mBottom && line == o.y);
            if ((aligned && !against) || (against && xNear))
                add({false, line, spanFromX, spanToX});
        }
    }
    return result;
}

// ---- overlap resolution ------------------------------------------------------

QPoint LayoutGeometry::settle(const QList<LayoutRect> &rects, int moving, QPoint desired) {
    if (moving < 0 || moving >= rects.size() || rects.size() < 2)
        return desired;
    const LayoutRect &m = rects[moving];

    QSet<int> xs{desired.x()}, ys{desired.y()};
    for (int k = 0; k < rects.size(); ++k) {
        if (k == moving)
            continue;
        const LayoutRect &o = rects[k];
        for (int v : {o.x, o.right(), o.x - m.w, o.right() - m.w})
            xs.insert(v);
        for (int v : {o.y, o.bottom(), o.y - m.h, o.bottom() - m.h})
            ys.insert(v);
        // Sliding along an edge keeps the desired coordinate while touching.
        xs.insert(qBound(o.x - m.w + 1, desired.x(), o.right() - 1));
        ys.insert(qBound(o.y - m.h + 1, desired.y(), o.bottom() - 1));
    }

    QList<int> xl = xs.values(), yl = ys.values();
    std::sort(xl.begin(), xl.end());
    std::sort(yl.begin(), yl.end());

    qint64 bestCost[4] = {std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::max(),
                          std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::max()};
    QPoint bestPos[4] = {desired, desired, desired, desired};

    QList<LayoutRect> trial = rects;
    for (int x : xl) {
        for (int y : yl) {
            trial[moving].x = x;
            trial[moving].y = y;
            bool overlap = false, touches = false;
            for (int k = 0; k < trial.size(); ++k) {
                if (k == moving)
                    continue;
                if (overlaps(trial[moving], trial[k])) {
                    overlap = true;
                    break;
                }
                touches = touches || contact(trial[moving], trial[k]) > 0;
            }
            if (overlap)
                continue;
            // Tier 0: touching and the whole layout connected; 1: touching;
            // 2: not overlapping at all.
            const int tier = touches ? (isConnected(trial) ? 0 : 1) : 2;
            const qint64 dx = x - desired.x(), dy = y - desired.y();
            const qint64 cost = dx * dx + dy * dy;
            if (cost < bestCost[tier]) {
                bestCost[tier] = cost;
                bestPos[tier] = {x, y};
            }
        }
    }
    for (int tier = 0; tier < 3; ++tier)
        if (bestCost[tier] != std::numeric_limits<qint64>::max())
            return bestPos[tier];
    return desired;
}
