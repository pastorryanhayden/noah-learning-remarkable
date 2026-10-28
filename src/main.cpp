#include "InkCanvas.h"
#include "PenInput.h"
#include "EpaperRefresh.h"
#include <QGuiApplication>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QDebug>
#include <qqml.h>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    if (app.platformName() == "epaper") {
        const QString mode = qEnvironmentVariable("RM_EPAPER_UI_MODE");
        if (!mode.isEmpty() && !configureEpaperRefresh(mode))
            qWarning() << "Requested E Ink refresh mode unavailable; using backend default";
    }
    qmlRegisterType<InkCanvas>("Learning", 1, 0, "InkCanvas");
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("RemarkableLearning", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto *answer = engine.rootObjects().first()->findChild<InkCanvas *>("answerCanvas");
    const QString serverUrl = qEnvironmentVariable("RM_SERVER_URL");
    if (answer && qEnvironmentVariableIsSet("RM_TIMED_PRACTICE")) {
        QString token = qEnvironmentVariable("RM_DEVICE_TOKEN").trimmed();
        const QString tokenPath = qEnvironmentVariable("RM_DEVICE_TOKEN_FILE");
        bool credentialFileError = false;
        if (token.isEmpty() && !tokenPath.isEmpty()) {
            QFile file(tokenPath);
            const QFileInfo info(file);
            const auto publicPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup
                | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther
                | QFileDevice::ExeOther;
            if (!info.isFile() || info.isSymLink() || (info.permissions() & publicPermissions)
                || info.size() > 4096 || !file.open(QIODevice::ReadOnly)) {
                credentialFileError = true;
            } else token = QString::fromUtf8(file.readAll()).trimmed();
        }
        answer->connectPractice(QUrl(serverUrl), token);
        if (credentialFileError) answer->setExternalError("The private device credential file could not be read safely.");
    } else if (answer && !serverUrl.isEmpty()) answer->connectServer(QUrl(serverUrl));
    if (app.platformName() == "epaper" || qEnvironmentVariableIsSet("RM_PEN_DEVICE")) {
        auto *canvas = engine.rootObjects().first()->findChild<InkCanvas *>("answerCanvas");
        if (canvas) {
            canvas->setAcceptedMouseButtons(Qt::NoButton);
            auto *pen = new PenInput(canvas, &app);
            pen->start();
        }
    }
    return app.exec();
}
