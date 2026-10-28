#include "EpaperRefresh.h"
#include <QFile>
#include <QLibrary>
#include <QMetaEnum>
#include <QMetaObject>
#include <QDebug>

bool configureEpaperRefresh(const QString &mode) {
    const QByteArray key = mode == "pen" ? "Pen" : mode == "mono" ? "Mono"
        : mode == "content" ? "Content" : QByteArray();
    if (key.isEmpty()) return false;
    QFile release("/etc/os-release");
    QFile machine("/sys/devices/soc0/machine");
    if (sizeof(void *) != 4 || !release.open(QIODevice::ReadOnly)
        || !release.readAll().split('\n').contains("IMG_VERSION=\"3.28.0.172\"")
        || !machine.open(QIODevice::ReadOnly) || machine.readAll().trimmed() != "reMarkable 2.0") {
        qWarning() << "E Ink refresh override requires the verified rm2 firmware; keeping default";
        return false;
    }
    // Private vendor ABI, measured on this firmware. Keep the library resident
    // so the setting applies when Qt subsequently creates its scenegraph backend.
    static QLibrary library("/usr/lib/plugins/scenegraph/libqsgepaper.so");
    library.setLoadHints(QLibrary::PreventUnloadHint);
    if (!library.load()) return false;
    const auto *meta = reinterpret_cast<const QMetaObject *>(
        library.resolve("_ZN16EPScreenModeItem16staticMetaObjectE"));
    auto *current = reinterpret_cast<int *>(library.resolve("_ZN15EPScreenModeMap12uiScreenModeE"));
    if (!meta || !current) return false;
    const int index = meta->indexOfEnumerator("Mode");
    if (index < 0) return false;
    const QMetaEnum modes = meta->enumerator(index);
    // Reject incompatible metadata before writing the exported integer.
    if (modes.keyToValue("Pen") != 0 || modes.keyToValue("Mono") != 1
        || modes.keyToValue("Content") != 4) return false;
    const int selected = modes.keyToValue(key.constData());
    if (selected < 0) return false;
    qInfo() << "E Ink refresh mode:" << key << "previous:" << *current;
    *current = selected;
    return true;
}
