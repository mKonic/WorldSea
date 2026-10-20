#include "app/theme.h"

#include <QAbstractItemView>
#include <QDir>
#include <QStyledItemDelegate>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>

#include <functional>

namespace worldsea {

const Theme &theme()
{
    static const Theme t;
    return t;
}

namespace {
class NoFocusDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem o = option;
        o.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(p, o, index);
    }
};
} // namespace

void dropFocusFrame(QAbstractItemView *view) { view->setItemDelegate(new NoFocusDelegate(view)); }

QColor typeColor(const QString &color)
{
    const QColor c(color);
    return c.isValid() ? c : theme().accent;
}

QString Theme::writeIcons() const
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("ui");
    QDir().mkpath(dir);
    // drawn at twice the size they show at, so they stay sharp on scaled screens
    auto draw = [&](const QString &name, int w, int h, const std::function<void(QPainter &)> &paint) {
        for (int scale : {1, 2}) {
            QImage img(w * scale, h * scale, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(scale, scale);
            paint(p);
            p.end();
            img.save(QDir(dir).filePath(name + (scale == 2 ? "@2x.png" : ".png")));
        }
    };
    auto chevron = [&](const QColor &c) {
        return [c](QPainter &p) {
            p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath path(QPointF(2.5, 4.5));
            path.lineTo(6, 8);
            path.lineTo(9.5, 4.5);
            p.drawPath(path);
        };
    };
    draw("chevron", 12, 12, chevron(muted));
    draw("chevron-up", 12, 12, [&](QPainter &p) {
        p.setPen(QPen(muted, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path(QPointF(2.5, 7.5));
        path.lineTo(6, 4);
        path.lineTo(9.5, 7.5);
        p.drawPath(path);
    });
    draw("chevron-right", 12, 12, [&](QPainter &p) {
        p.setPen(QPen(muted, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path(QPointF(4.5, 2.5));
        path.lineTo(8, 6);
        path.lineTo(4.5, 9.5);
        p.drawPath(path);
    });
    draw("chevron-hot", 12, 12, chevron(text));
    auto cross = [&](const QColor &c) {
        return [c](QPainter &p) {
            p.setPen(QPen(c, 1.4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(3.5, 3.5), QPointF(8.5, 8.5));
            p.drawLine(QPointF(8.5, 3.5), QPointF(3.5, 8.5));
        };
    };
    draw("close", 12, 12, cross(faint));
    draw("close-hot", 12, 12, cross(text));
    draw("check", 13, 13, [&](QPainter &p) {
        p.setPen(QPen(bg, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path(QPointF(3, 6.8));
        path.lineTo(5.6, 9.2);
        path.lineTo(10, 4);
        p.drawPath(path);
    });
    return dir;
}

QString Theme::styleSheet(const QString &iconDir) const
{
    return QStringLiteral(R"(
QWidget { color: %1; }
QDialogButtonBox { dialogbuttonbox-buttons-have-icons: 0; }
QMainWindow, #room, QDialog { background: %2; }
QToolTip { background: %3; color: %1; border: 1px solid %4; padding: 4px 6px; }
QLabel#title { color: %5; font-size: 16px; }
QLabel#muted, QLabel#since { color: %6; }
QLabel#fieldName { color: %6; }
QLabel#heading { color: %6; font-size: 11px; letter-spacing: 1px; }
QLineEdit, QPlainTextEdit { background: %3; border: 1px solid %4; border-radius: 6px; padding: 6px 9px; color: %1; selection-background-color: %5; selection-color: #191919; }
QLineEdit:focus, QPlainTextEdit:focus { border-color: %5; }
QLineEdit#ghost { background: transparent; border: 1px dashed transparent; color: %1; }
QLineEdit#ghost:hover { border-color: %4; }
QLineEdit#ghost:focus { background: %3; border: 1px solid %5; }
QLineEdit#nameEdit { background: transparent; border: none; border-bottom: 1px solid transparent; border-radius: 0; font-size: 24px; padding: 2px 0 4px 0; }
QLineEdit#nameEdit:hover { border-bottom-color: %4; }
QLineEdit#nameEdit:focus { border-bottom-color: %5; }
QSpinBox, QComboBox { background: %3; border: 1px solid %4; border-radius: 6px; padding: 5px 8px; color: %1; }
QComboBox:focus { border-color: %5; }
QComboBox { padding-right: 24px; }
QComboBox:hover { border-color: #5c5c5c; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: url(ICONS/chevron.png); width: 12px; height: 12px; }
QComboBox::down-arrow:hover { image: url(ICONS/chevron-hot.png); }
QComboBox QAbstractItemView { background: #262626; color: %1; border: 1px solid %4; selection-background-color: #333333; outline: none; }
QPushButton { background: %3; border: 1px solid %4; border-radius: 6px; color: %1; padding: 6px 12px; }
QPushButton:hover { border-color: %5; }
QPushButton:checked { background: %5; color: #191919; border-color: %5; }
QPushButton#flat { background: none; border: none; color: %6; padding: 4px 6px; }
QPushButton#flat:hover { color: %1; }
QPushButton#gold { background: %5; color: #191919; border-color: %5; }
QToolButton { background: none; border: none; color: %6; padding: 4px 6px; border-radius: 5px; }
QToolButton:hover { color: %1; background: %3; }
QToolButton:checked { color: #191919; background: %5; }
QToolButton::menu-indicator, QPushButton::menu-indicator { image: none; width: 0; }
QToolButton[popupMode="2"] { padding-right: 6px; }
#segment { background: %3; border: 1px solid %4; border-radius: 8px; }
#segment QToolButton { color: %6; padding: 4px 12px; border-radius: 6px; }
#segment QToolButton:hover { color: %1; background: transparent; }
#segment QToolButton:checked { color: #191919; background: %5; }
#segment QToolButton:disabled { color: #4a4a4a; }
QToolButton#square { background: %3; border: 1px solid %4; border-radius: 6px; color: %1; font-size: 16px; padding: 0; }
QToolButton#square:hover { border-color: %5; }
QToolButton#step { font-size: 20px; padding: 0 8px 3px 8px; }
QCheckBox { color: %1; spacing: 8px; }
QCheckBox::indicator, QListWidget::indicator, QTreeWidget::indicator { width: 13px; height: 13px; border: 1px solid #5c5c5c; border-radius: 3px; background: %3; }
QCheckBox::indicator:checked, QListWidget::indicator:checked, QTreeWidget::indicator:checked { background: %5; border-color: %5; image: url(ICONS/check.png); }
QCheckBox::indicator:hover { border-color: %5; }
QTreeWidget, QListWidget { background: %7; border: none; outline: none; }
QTreeWidget::item, QListWidget::item { padding: 3px 2px; border-radius: 4px; }
QTreeWidget::item:selected, QListWidget::item:selected { background: #333333; color: %1; }
QTreeWidget::item:hover, QListWidget::item:hover { background: #2a2a2a; }
QTreeView::branch { background: transparent; border: none; }
QTreeView::branch:selected { background: #333333; }
QTreeView::branch:has-children:closed { image: url(ICONS/chevron-right.png); }
QTreeView::branch:has-children:open { image: url(ICONS/chevron.png); }
QHeaderView::section { background: %7; color: %6; border: none; border-bottom: 1px solid %4; padding: 7px 10px; }
QHeaderView::section:hover { color: %1; }
QHeaderView::up-arrow { image: url(ICONS/chevron-up.png); width: 10px; height: 10px; }
QHeaderView::down-arrow { image: url(ICONS/chevron.png); width: 10px; height: 10px; }
QTableWidget { background: %2; border: none; outline: none; gridline-color: %4; selection-background-color: #333333; selection-color: %1; }
QTableWidget::item { padding: 4px 10px; border-bottom: 1px solid #242424; }
QTableWidget::item:selected { background: #333333; color: %1; }
QTableCornerButton::section { background: %7; border: none; }
QTabWidget::pane { border: none; }
QTabWidget::tab-bar { left: 0; }
QTabBar { background: %7; qproperty-drawBase: 0; }
QTabBar::tab { background: %7; color: %6; padding: 9px 10px 9px 16px; border: none; border-bottom: 2px solid transparent; margin-right: 1px; }
QTabBar::tab:selected { color: %1; border-bottom-color: %5; }
QTabBar::tab:hover { color: %1; }
QTabBar::close-button { image: url(ICONS/close.png); subcontrol-position: right; margin: 2px; border-radius: 4px; }
QTabBar::close-button:hover { image: url(ICONS/close-hot.png); background: #333333; }
QSplitter::handle { background: %4; }
QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %5; border-radius: 2px; }
QSlider::handle:horizontal { background: %5; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }
QMenu { background: #262626; border: 1px solid %4; border-radius: 8px; padding: 6px; color: %1; }
QMenu::item { padding: 6px 22px 6px 12px; border-radius: 5px; }
QMenu::item:selected { background: #333333; }
QMenu::item:disabled { color: #666666; }
QMenu::separator { height: 1px; background: %4; margin: 4px 6px; }
QMenuBar { background: %2; color: %6; }
QMenuBar::item:selected { background: %3; color: %1; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: rgba(128,128,128,0.35); border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle:horizontal { background: rgba(128,128,128,0.35); border-radius: 4px; min-width: 30px; margin: 2px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
#timeline { background: %7; border-bottom: 1px solid %4; }
#whatIf { background: #3a3220; border-bottom: 1px solid #5a4a28; }
#history { background: %7; }
)")
        .arg(text.name(), bg.name(), bgSoft.name(), line.name(), accent.name(), muted.name(), pane.name())
        .replace("ICONS", QDir::fromNativeSeparators(iconDir));
}

} // namespace worldsea
