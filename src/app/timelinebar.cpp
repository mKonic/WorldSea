#include "app/timelinebar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>

namespace worldsea {

TimelineBar::TimelineBar(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    setObjectName("timeline");
    setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(12, 6, 12, 6);
    row->setSpacing(8);

    auto *asOf = new QLabel("As of");
    asOf->setObjectName("muted");
    m_prev = new QToolButton;
    m_prev->setText("‹");
    m_prev->setToolTip("Previous chapter (Alt+Left)");
    m_prev->setShortcut(QKeySequence("Alt+Left"));
    m_next = new QToolButton;
    m_next->setText("›");
    m_next->setToolTip("Next chapter (Alt+Right)");
    m_next->setShortcut(QKeySequence("Alt+Right"));
    for (auto *b : {m_prev, m_next}) b->setStyleSheet("QToolButton { font-size: 20px; padding: 0 8px 3px 8px; }");
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setMinimumWidth(200);
    m_slider->setPageStep(1);
    m_label = new QLabel;
    m_label->setMinimumWidth(220);

    m_correct = new QPushButton("Correct");
    m_correct->setToolTip("Edits fix what is already recorded, as if it was always so");
    m_change = new QPushButton("Change here");
    m_change->setToolTip("Edits are recorded as happening in this chapter; what came before is kept");
    for (auto *b : {m_correct, m_change}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
    }
    auto *group = new QButtonGroup(this);
    group->addButton(m_correct);
    group->addButton(m_change);
    auto *modes = new QHBoxLayout;
    modes->setSpacing(0);
    modes->addWidget(m_correct);
    modes->addWidget(m_change);

    row->addWidget(asOf);
    row->addWidget(m_prev);
    row->addWidget(m_slider, 1);
    row->addWidget(m_next);
    row->addWidget(m_label);
    row->addSpacing(12);
    row->addLayout(modes);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        const auto &t = m_world->timeline();
        if (v >= 0 && v < t.size()) m_world->setCursor(t[v]);
    });
    connect(m_prev, &QToolButton::clicked, this, [this] { m_slider->setValue(m_slider->value() - 1); });
    connect(m_next, &QToolButton::clicked, this, [this] { m_slider->setValue(m_slider->value() + 1); });
    connect(m_correct, &QPushButton::clicked, this, [this] { m_world->setEditMode(EditMode::Correct); });
    connect(m_change, &QPushButton::clicked, this, [this] { m_world->setEditMode(EditMode::Change); });
    connect(world, &World::changed, this, &TimelineBar::refresh);
    connect(world, &World::cursorChanged, this, &TimelineBar::refresh);
    connect(world, &World::editModeChanged, this, &TimelineBar::refresh);
    refresh();
}

void TimelineBar::refresh()
{
    if (!m_world->isOpen()) return;
    const auto &t = m_world->timeline();
    const int at = m_world->ordinal(m_world->cursor());
    {
        QSignalBlocker b(m_slider);
        m_slider->setRange(0, std::max(0, int(t.size()) - 1));
        m_slider->setValue(std::max(0, at));
    }
    m_slider->setEnabled(t.size() > 1);
    m_prev->setEnabled(at > 0);
    m_next->setEnabled(at >= 0 && at + 1 < t.size());
    m_label->setText(t.isEmpty() ? QStringLiteral("no chapters yet")
                                 : QStringLiteral("%1  <span style='color:#5c5c5c'>%2 / %3</span>")
                                       .arg(m_world->chapterLabel(m_world->cursor()).toHtmlEscaped())
                                       .arg(at + 1)
                                       .arg(t.size()));
    m_correct->setChecked(m_world->editMode() == EditMode::Correct);
    m_change->setChecked(m_world->editMode() == EditMode::Change);
    m_change->setEnabled(!t.isEmpty());
}

} // namespace worldsea

#include "moc_timelinebar.cpp"
