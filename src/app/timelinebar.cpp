#include "app/timelinebar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>

namespace worldsea {

TimelineBar::TimelineBar(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    setObjectName("timeline");
    setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(14, 8, 14, 8);
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
    for (auto *b : {m_prev, m_next}) b->setObjectName("step");
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setMinimumWidth(200);
    m_slider->setPageStep(1);
    m_label = new QLabel;
    m_label->setMinimumWidth(220);

    m_correct = new QToolButton;
    m_correct->setText("Correct");
    m_correct->setToolTip("Edits fix what is already recorded, as if it was always so");
    m_change = new QToolButton;
    m_change->setText("Change here");
    m_change->setToolTip("Edits are recorded as happening in this chapter; what came before is kept");
    for (auto *b : {m_correct, m_change}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
    }
    auto *group = new QButtonGroup(this);
    group->addButton(m_correct);
    group->addButton(m_change);
    // one control with two halves: how the next edit lands
    auto *segment = new QFrame;
    segment->setObjectName("segment");
    auto *modes = new QHBoxLayout(segment);
    modes->setContentsMargins(3, 3, 3, 3);
    modes->setSpacing(2);
    modes->addWidget(m_correct);
    modes->addWidget(m_change);
    auto *editsLabel = new QLabel("Edits");
    editsLabel->setObjectName("muted");

    row->addWidget(asOf);
    row->addWidget(m_prev);
    row->addWidget(m_slider, 1);
    row->addWidget(m_next);
    row->addWidget(m_label);
    row->addSpacing(16);
    row->addWidget(editsLabel);
    row->addWidget(segment);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        const auto &t = m_world->timeline();
        if (v >= 0 && v < t.size()) m_world->setCursor(t[v]);
    });
    connect(m_prev, &QToolButton::clicked, this, [this] { m_slider->setValue(m_slider->value() - 1); });
    connect(m_next, &QToolButton::clicked, this, [this] { m_slider->setValue(m_slider->value() + 1); });
    connect(m_correct, &QToolButton::clicked, this, [this] { m_world->setEditMode(EditMode::Correct); });
    connect(m_change, &QToolButton::clicked, this, [this] { m_world->setEditMode(EditMode::Change); });
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
    const auto ch = m_world->chapter(m_world->cursor());
    const auto book = ch ? m_world->book(ch->book) : std::nullopt;
    m_label->setText(!ch ? QStringLiteral("<span style='color:#8a8a8a'>no chapters yet</span>")
                         : QStringLiteral("<span style='color:#8a8a8a'>%1</span>&nbsp; %2 &nbsp;<span style='color:#5c5c5c'>%3/%4</span>")
                               .arg((book ? book->name : QString()).toHtmlEscaped(), ch->title.toHtmlEscaped())
                               .arg(at + 1)
                               .arg(t.size()));
    m_correct->setChecked(m_world->editMode() == EditMode::Correct);
    m_change->setChecked(m_world->editMode() == EditMode::Change);
    m_change->setEnabled(!t.isEmpty());
}

} // namespace worldsea

#include "moc_timelinebar.cpp"
