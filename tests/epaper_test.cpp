#include <QtTest>
#include <QLibrary>
#include <QScopeGuard>
#include "../src/EpaperRefresh.h"

class EpaperTest : public QObject {
    Q_OBJECT
private slots:
    void rejectsUnknownMode() { QVERIFY(!configureEpaperRefresh("unknown")); }
    void selectsRefreshModesOnMatchedTablet() {
        QLibrary library("/usr/lib/plugins/scenegraph/libqsgepaper.so");
        if (!library.load()) QSKIP("Requires the matched tablet E Ink backend");
        auto *mode = reinterpret_cast<int *>(library.resolve("_ZN15EPScreenModeMap12uiScreenModeE"));
        QVERIFY(mode);
        const int original = *mode;
        const auto restore = qScopeGuard([&] { *mode = original; });
        QVERIFY(configureEpaperRefresh("mono"));
        QCOMPARE(*mode, 1); // Verified device metadata: Mono=1, Content=4.
        QVERIFY(configureEpaperRefresh("pen"));
        QCOMPARE(*mode, 0);
        QVERIFY(configureEpaperRefresh("content"));
        QCOMPARE(*mode, 4);
    }
};
QTEST_GUILESS_MAIN(EpaperTest)
#include "epaper_test.moc"
