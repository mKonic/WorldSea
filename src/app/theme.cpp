#include "app/theme.h"

namespace worldsea {

const Theme &theme()
{
    static const Theme t;
    return t;
}

QColor typeColor(const QString &color)
{
    const QColor c(color);
    return c.isValid() ? c : theme().accent;
}

QString Theme::styleSheet() const
{
    return QStringLiteral(R"(
QWidget { color: %1; }
QMainWindow, #room, QDialog { background: %2; }
QToolTip { background: %3; color: %1; border: 1px solid %4; padding: 4px 6px; }
QLabel#title { color: %5; font-size: 16px; }
QLabel#muted, QLabel#since { color: %6; }
QLabel#fieldName { color: %6; }
QLabel#heading { color: %6; font-size: 11px; letter-spacing: 1px; }
QLineEdit, QPlainTextEdit { background: %3; border: 1px solid %4; border-radius: 6px; padding: 6px 9px; color: %1; selection-background-color: %5; selection-color: #191919; }
QLineEdit:focus, QPlainTextEdit:focus { border-color: %5; }
QLineEdit#nameEdit { background: transparent; border: 1px solid transparent; font-size: 22px; padding: 2px 4px; }
QLineEdit#nameEdit:hover { border-color: %4; }
QLineEdit#nameEdit:focus { border-color: %5; }
QSpinBox, QComboBox { background: %3; border: 1px solid %4; border-radius: 6px; padding: 5px 8px; color: %1; }
QComboBox:focus { border-color: %5; }
QComboBox::drop-down { border: none; width: 18px; }
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
QCheckBox { color: %1; spacing: 8px; }
QCheckBox::indicator, QListWidget::indicator, QTreeWidget::indicator { width: 13px; height: 13px; border: 1px solid #5c5c5c; border-radius: 3px; background: %3; }
QCheckBox::indicator:checked, QListWidget::indicator:checked, QTreeWidget::indicator:checked { background: %5; border-color: %5; }
QTreeWidget, QListWidget { background: %7; border: none; outline: none; }
QTreeWidget::item, QListWidget::item { padding: 3px 2px; border-radius: 4px; }
QTreeWidget::item:selected, QListWidget::item:selected { background: #333333; color: %1; }
QTreeWidget::item:hover, QListWidget::item:hover { background: #2a2a2a; }
QHeaderView::section { background: %7; color: %6; border: none; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: %7; color: %6; padding: 7px 14px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: %1; border-bottom-color: %5; }
QTabBar::tab:hover { color: %1; }
QTabBar::close-button { image: none; }
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
#history { background: %7; }
)")
        .arg(text.name(), bg.name(), bgSoft.name(), line.name(), accent.name(), muted.name(), pane.name());
}

} // namespace worldsea
