#include "app/checksview.h"

#include "app/theme.h"
#include "core/continuity.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

namespace worldsea {

namespace {

constexpr int kEntityRole = Qt::UserRole;
constexpr int kChapterRole = Qt::UserRole + 1;

QIcon mark(const QColor &c)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(3, 3, 8, 8));
    return QIcon(pm);
}

} // namespace

ChecksView::ChecksView(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *bar = new QWidget;
    bar->setObjectName("timeline");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(14, 10, 14, 10);
    m_summary = new QLabel;
    auto *hint = new QLabel("Double-click to open the item at that chapter");
    hint->setObjectName("muted");
    row->addWidget(m_summary);
    row->addStretch();
    row->addWidget(hint);
    col->addWidget(bar);

    m_list = new QListWidget;
    m_list->setWordWrap(true);
    m_list->setSpacing(2);
    dropFocusFrame(m_list);
    col->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *i) {
        const Id ch = i->data(kChapterRole).toLongLong();
        if (ch) m_world->setCursor(ch);
        if (const Id e = i->data(kEntityRole).toLongLong()) emit openEntity(e);
    });
    connect(world, &World::changed, this, &ChecksView::rebuild);
    rebuild();
}

void ChecksView::rebuild()
{
    if (!m_world->isOpen()) return;
    const auto issues = checkContinuity(*m_world);
    const int warnings = int(std::count_if(issues.begin(), issues.end(), [](const Issue &i) { return i.severity == Issue::Warning; }));
    const Theme &th = theme();
    m_list->clear();
    for (const auto &i : issues) {
        auto *item = new QListWidgetItem(mark(i.severity == Issue::Warning ? QColor("#d9822b") : th.muted), i.text);
        item->setData(kEntityRole, i.entity);
        item->setData(kChapterRole, i.chapter);
        item->setSizeHint(QSize(0, 34));
        if (i.severity == Issue::Note) item->setForeground(th.muted);
        m_list->addItem(item);
    }
    if (issues.isEmpty()) {
        auto *none = new QListWidgetItem("Nothing contradicts itself. Checks look at appearances, deaths (a Status reading "
                                         "“dead”), ladders going down, link spans and single-valued fields.");
        none->setFlags(Qt::NoItemFlags);
        none->setForeground(th.muted);
        m_list->addItem(none);
    }
    m_summary->setText(issues.isEmpty() ? QStringLiteral("All clear")
                                        : QStringLiteral("%1 to fix · %2 worth a look").arg(warnings).arg(issues.size() - warnings));
    emit titleChanged(warnings ? QStringLiteral("Checks (%1)").arg(warnings) : QStringLiteral("Checks"));
}

} // namespace worldsea

#include "moc_checksview.cpp"
