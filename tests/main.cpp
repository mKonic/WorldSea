#include <QGuiApplication>

#include <gtest/gtest.h>

// World is a QObject and images go through QtGui; offscreen keeps the tests
// off any display.
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
