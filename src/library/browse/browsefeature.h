#pragma once

#include <QObject>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringListModel>
#include <QVariant>

#include "library/browse/browsetablemodel.h"
#include "library/libraryfeature.h"
#include "library/proxytrackmodel.h"
#include "preferences/usersettings.h"

#define QUICK_LINK_NODE "::mixxx_quick_lnk_node::"
#define DEVICE_NODE "::mixxx_device_node::"

class Library;
class TrackCollection;
class WLibrarySidebar;
class QModelIndex;
class FolderTreeModel;

class BrowseFeature : public LibraryFeature {
    Q_OBJECT
  public:
    BrowseFeature(Library* pLibrary,
            UserSettingsPointer pConfig,
            RecordingManager* pRecordingManager,
            const QString& customTitle = QString(),
            const QStringList& customRootPaths = QStringList());
    ~BrowseFeature() override;

    QVariant title() override;

    void bindLibraryWidget(WLibrary* libraryWidget,
            KeyboardEventFilter* keyboard) override;
    void bindSidebarWidget(WLibrarySidebar* pSidebarWidget) override;

    TreeItemModel* sidebarModel() const override;

    void releaseBrowseThread();

    // Set when this instance stands for a fixed set of directories rather
    // than the whole machine: the sidebar shows just those, and the quick
    // link machinery is left alone.
    bool hasCustomRoot() const {
        return !m_customRootPaths.isEmpty();
    }

  public slots:
    void slotAddQuickLink();
    void slotRemoveQuickLink();
    void slotAddToLibrary();
    void slotRefreshDirectoryTree();
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;
    void onLazyChildExpandation(const QModelIndex& index) override;
    void slotLibraryScanStarted();
    void slotLibraryScanFinished();
    void invalidateRightClickIndex();

  signals:
    void setRootIndex(const QModelIndex&);
    void requestAddDir(const QString&);
    void scanLibrary();

  private:
    // Both instances would otherwise register their landing page under the
    // same name, and WLibrary::registerView refuses the duplicate.
    QString viewName() const;

    QString getRootViewHtml() const;
    QString extractNameFromPath(const QString& spath);
    QStringList getDefaultQuickLinks() const;
    std::vector<std::unique_ptr<TreeItem>> getChildDirectoryItems(const QString& path) const;
    void saveQuickLinks();
    void loadQuickLinks();
    QString getLastRightClickedPath() const;

    TrackCollection* const m_pTrackCollection;

    BrowseTableModel m_browseModel;
    ProxyTrackModel m_proxyModel;
    FolderTreeModel* m_pSidebarModel;
    QString m_customTitle;
    QStringList m_customRootPaths;
    QAction* m_pAddQuickLinkAction;
    QAction* m_pRemoveQuickLinkAction;
    QAction* m_pAddtoLibraryAction;
    QAction* m_pRefreshDirTreeAction;

    // Caution: Make sure this is reset whenever the library tree is updated,
    // so that the internalPointer() does not become dangling
    QModelIndex m_lastRightClickedIndex;
    TreeItem* m_pQuickLinkItem{};
    QStringList m_quickLinkList;
    QPointer<WLibrarySidebar> m_pSidebarWidget;
};
