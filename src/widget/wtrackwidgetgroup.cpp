#include "widget/wtrackwidgetgroup.h"

#include <QChildEvent>

#include <QStyle>

#include <QStylePainter>

#include "control/controlobject.h"
#include "moc_wtrackwidgetgroup.cpp"
#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "util/dnd.h"
#include "widget/wtrackmenu.h"

namespace {

constexpr int kDefaultTrackColorAlpha = 255;

} // anonymous namespace

WTrackWidgetGroup::WTrackWidgetGroup(QWidget* pParent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        const QString& group,
        bool isMainDeck)
        : WWidgetGroup(pParent),
          m_group(group),
          m_pConfig(pConfig),
          m_pLibrary(pLibrary),
          m_trackColorAlpha(kDefaultTrackColorAlpha),
          m_isMainDeck(isMainDeck) {
    setAcceptDrops(true);
}

WTrackWidgetGroup::~WTrackWidgetGroup() = default;

void WTrackWidgetGroup::setup(const QDomNode& node, const SkinContext& context) {
    WWidgetGroup::setup(node, context);

    bool ok = false;
    int trackColorAlpha = context.selectInt(
            node,
            QStringLiteral("TrackColorAlpha"),
            &ok);
    if (ok && trackColorAlpha >= 0 && trackColorAlpha <= 255) {
        m_trackColorAlpha = trackColorAlpha;
    }
}

void WTrackWidgetGroup::slotTrackLoaded(TrackPointer pTrack) {
    if (!pTrack) {
        return;
    }
    m_pCurrentTrack = pTrack;
    connect(pTrack.get(),
            &Track::changed,
            this,
            &WTrackWidgetGroup::slotTrackChanged);
    updateColor();
}

void WTrackWidgetGroup::slotLoadingTrack(TrackPointer pNewTrack, TrackPointer pOldTrack) {
    Q_UNUSED(pNewTrack);
    Q_UNUSED(pOldTrack);
    if (m_pCurrentTrack) {
        disconnect(m_pCurrentTrack.get(), nullptr, this, nullptr);
    }
    m_pCurrentTrack.reset();
    updateColor();
}

void WTrackWidgetGroup::slotTrackChanged(TrackId trackId) {
    Q_UNUSED(trackId);
    updateColor();
}

void WTrackWidgetGroup::updateColor() {
    if (m_pCurrentTrack) {
        m_trackColor = mixxx::RgbColor::toQColor(m_pCurrentTrack->getColor());
        if (m_trackColor.isValid()) {
            m_trackColor.setAlpha(m_trackColorAlpha);
        }
    } else {
        m_trackColor = QColor();
    }
    update();
}

void WTrackWidgetGroup::paintEvent(QPaintEvent* pe) {
    WWidgetGroup::paintEvent(pe);

    if (m_trackColor.isValid()) {
        QStylePainter p(this);

        p.fillRect(rect(), QBrush(m_trackColor));
    }
}

void WTrackWidgetGroup::mousePressEvent(QMouseEvent* pEvent) {
    DragAndDropHelper::mousePressed(pEvent);
}

void WTrackWidgetGroup::mouseMoveEvent(QMouseEvent* pEvent) {
    if (m_pCurrentTrack && DragAndDropHelper::mouseMoveInitiatesDrag(pEvent)) {
        DragAndDropHelper::dragTrack(m_pCurrentTrack, this, m_group);
    }
}

void WTrackWidgetGroup::setDropHover(bool hover) {
    // QSS has no drag-hover pseudo-state; expose one as a dynamic property so
    // the skin can highlight the card a track is about to drop onto:
    //   TrackWidgetGroup[dropHover="true"] { border: 2px solid #f2d13c; }
    if (property("dropHover").toBool() == hover) {
        return;
    }
    setProperty("dropHover", hover);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void WTrackWidgetGroup::childEvent(QChildEvent* pEvent) {
    WWidgetGroup::childEvent(pEvent);
    if (pEvent->added()) {
        watchDescendants(pEvent->child());
    }
}

void WTrackWidgetGroup::watchDescendants(QObject* pRoot) {
    // The card children (overview, labels) swallow drag events, so the
    // group alone only sees drags over its bare strips. Filter the whole
    // subtree so hover and drop cover the entire card area.
    pRoot->installEventFilter(this);
    const QObjectList& children = pRoot->children();
    for (QObject* pChild : children) {
        watchDescendants(pChild);
    }
}

bool WTrackWidgetGroup::eventFilter(QObject* pObj, QEvent* pEvent) {
    switch (pEvent->type()) {
    case QEvent::ChildAdded: {
        // Descendants are built after their parent is added, so cascade the
        // filter onto every new node to cover the whole card subtree.
        QChildEvent* pChild = static_cast<QChildEvent*>(pEvent);
        watchDescendants(pChild->child());
        break;
    }
    case QEvent::DragEnter: {
        QDragEnterEvent* pDrag = static_cast<QDragEnterEvent*>(pEvent);
        DragAndDropHelper::handleTrackDragEnterEvent(pDrag, m_group, m_pConfig);
        if (pDrag->isAccepted()) {
            setDropHover(true);
            return true;
        }
        break;
    }
    case QEvent::DragLeave:
        setDropHover(false);
        break;
    case QEvent::Drop: {
        setDropHover(false);
        QDropEvent* pDrop = static_cast<QDropEvent*>(pEvent);
        DragAndDropHelper::handleTrackDropEvent(pDrop, *this, m_group, m_pConfig);
        if (pDrop->isAccepted()) {
            return true;
        }
        break;
    }
    default:
        break;
    }
    return WWidgetGroup::eventFilter(pObj, pEvent);
}

void WTrackWidgetGroup::dragEnterEvent(QDragEnterEvent* pEvent) {
    DragAndDropHelper::handleTrackDragEnterEvent(pEvent, m_group, m_pConfig);
    if (pEvent->isAccepted()) {
        setDropHover(true);
    }
}

void WTrackWidgetGroup::dragLeaveEvent(QDragLeaveEvent* pEvent) {
    Q_UNUSED(pEvent);
    setDropHover(false);
}

void WTrackWidgetGroup::dropEvent(QDropEvent* pEvent) {
    setDropHover(false);
    DragAndDropHelper::handleTrackDropEvent(pEvent, *this, m_group, m_pConfig);
}

void WTrackWidgetGroup::contextMenuEvent(QContextMenuEvent* pEvent) {
    pEvent->accept();
    if (m_pCurrentTrack) {
        ensureTrackMenuIsCreated();
        m_pTrackMenu->loadTrack(m_pCurrentTrack, m_group);
        // Create the right-click menu
        m_pTrackMenu->popup(pEvent->globalPos());
    }
}

void WTrackWidgetGroup::ensureTrackMenuIsCreated() {
    if (m_pTrackMenu.get() != nullptr) {
        return;
    }
    m_pTrackMenu = make_parented<WTrackMenu>(
            this, m_pConfig, m_pLibrary, WTrackMenu::kDeckTrackMenuFeatures);

    // The show control exists onlyfor main decks.
    // See WTrackProperty for info
    if (!m_isMainDeck) {
        return;
    }
    connect(m_pTrackMenu,
            &WTrackMenu::trackMenuVisible,
            this,
            [this](bool visible) {
                ControlObject::set(ConfigKey(m_group, kShowTrackMenuKey),
                        visible ? 1.0 : 0.0);
            });
}
