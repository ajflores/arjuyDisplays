#pragma once

#include <QCoreApplication>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// One active monitor as a rectangle in layout (logical) coordinates, in whole
// pixels. pixelW/pixelH are the real pixels along the same axes (swapped when
// rotated); they only feed the "caused by scale" hint.
struct LayoutRect {
    QString name;
    int x = 0, y = 0, w = 0, h = 0;
    int pixelW = 0, pixelH = 0;
    double scale = 1;

    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

// A finding about a layout. Everything here is read-only information.
struct LayoutIssue {
    enum Kind { Overlap, Gap, Isolated, Disconnected, PartialContact };

    Kind kind = Gap;
    QStringList monitors;      // implicated monitors (for a gap or contact: first, then second)
    int amount = 0;            // px: gap size, overlap depth, distance to the nearest monitor, contact length
    QRect region;              // where to draw it on the map (gap strip, overlap area); null if none
    bool horizontal = true;    // gap/contact along the x axis (monitors side by side) or the y axis
    QString hint;              // likely cause, when known
    QList<QStringList> groups; // Disconnected: the separate groups
    int sizeA = 0, sizeB = 0;  // PartialContact: edge lengths of both monitors

    bool isWarning() const { return kind != PartialContact; }
    QString kindName() const;
    QString message() const;
    QVariantMap toVariant() const;
};

struct SnapGuide {
    bool vertical = true;  // a vertical line at x=pos spanning y from..to (else horizontal)
    int pos = 0, from = 0, to = 0;
    bool operator==(const SnapGuide &o) const { return vertical == o.vertical && pos == o.pos && from == o.from && to == o.to; }
};

struct SnapResult {
    QPoint pos;
    QList<SnapGuide> guides;
};

// Pure geometry: snapping, overlap resolution and layout checks. No Qt GUI,
// no Hyprland, no files.
class LayoutGeometry {
    Q_DECLARE_TR_FUNCTIONS(LayoutGeometry)

public:
    static bool overlaps(const LayoutRect &a, const LayoutRect &b);
    // Length of the edge shared by two monitors that touch side by side or
    // top to bottom; 0 when they only meet at a corner or not at all.
    static int contact(const LayoutRect &a, const LayoutRect &b);
    static bool isConnected(const QList<LayoutRect> &rects);
    static QList<QList<int>> components(const QList<LayoutRect> &rects);

    static QList<LayoutIssue> analyze(const QList<LayoutRect> &rects);

    // Snaps rects[moving] placed at `proposed` to the edges of the others
    // (edge against edge, and aligned edges) within `threshold` logical px.
    static SnapResult snap(const QList<LayoutRect> &rects, int moving, QPoint proposed, int threshold);

    // Nearest position to `desired` where rects[moving] overlaps nothing and
    // touches another monitor, keeping the whole layout connected when possible.
    static QPoint settle(const QList<LayoutRect> &rects, int moving, QPoint desired);
};
