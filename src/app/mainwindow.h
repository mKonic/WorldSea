#pragma once
// The window: a welcome page until a world is open, then the timeline strip
// on top, books and items on the left, and tabs of sheets, sheet templates
// and the graph.

#include "core/world.h"

#include <QMainWindow>

class QStackedWidget;
class QTabWidget;
class QMenu;

namespace worldsea {

class NavPane;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openWorld(const QString &path);

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void buildMenus();
    void newWorld();
    void chooseWorld();
    void closeWorld();
    void openEntity(Id id, bool focusName = false);
    void openType(Id id);
    void openTable(Id id);
    void openGraph();
    void openChecks();
    void openHierarchy();
    void quickOpen();
    void newItemHere();
    void updateTitle();
    void fillRecent();
    QWidget *buildWelcome();

    World *m_world;
    QStackedWidget *m_stack;
    QWidget *m_welcome;
    QWidget *m_workspace;
    NavPane *m_nav;
    QTabWidget *m_tabs;
    QMenu *m_recent;
    QWidget *m_recentList;
    QList<QAction *> m_worldActions;
};

} // namespace worldsea
