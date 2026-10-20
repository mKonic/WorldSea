#include "app/mainwindow.h"
#include "app/theme.h"
#include "worldsea_version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

#include <cstdio>

using namespace worldsea;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("worldsea");
    app.setApplicationDisplayName("WorldSea");
    app.setOrganizationName("worldsea");
    app.setApplicationVersion(QStringLiteral("%1 (%2)").arg(kVersionName).arg(kBuild));

    QCommandLineParser cli;
    cli.setApplicationDescription("A relationship manager for the worlds in novels.");
    cli.addVersionOption();
    cli.addHelpOption();
    cli.addPositionalArgument("world", "The .worldsea file to open (created when it does not exist).");
    cli.process(app);

    app.setStyle(QStyleFactory::create("Fusion"));
    const Theme &t = theme();
    QPalette pal;
    pal.setColor(QPalette::Window, t.bg);
    pal.setColor(QPalette::WindowText, t.text);
    pal.setColor(QPalette::Base, t.bgSoft);
    pal.setColor(QPalette::AlternateBase, t.pane);
    pal.setColor(QPalette::Text, t.text);
    pal.setColor(QPalette::Button, t.bgSoft);
    pal.setColor(QPalette::ButtonText, t.text);
    pal.setColor(QPalette::Highlight, t.accent);
    pal.setColor(QPalette::HighlightedText, t.bg);
    pal.setColor(QPalette::PlaceholderText, t.faint);
    pal.setColor(QPalette::Link, t.accent);
    pal.setColor(QPalette::ToolTipBase, t.bgSoft);
    pal.setColor(QPalette::ToolTipText, t.text);
    app.setPalette(pal);
    app.setStyleSheet(t.styleSheet(t.writeIcons()));
    QFont ui = app.font();
    ui.setPixelSize(13);
    app.setFont(ui);

    std::fprintf(stderr, "worldsea %s (%d)\n", kVersionName, kBuild);

    MainWindow w;
    const QStringList args = cli.positionalArguments();
    const QString last = QSettings().value("lastWorld").toString();
    if (!args.isEmpty()) w.openWorld(args.first());
    else if (!last.isEmpty() && QFileInfo::exists(last)) w.openWorld(last);
    w.show();
    return app.exec();
}
