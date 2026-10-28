#include <QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "../src/InkCanvas.h"

class InkTest : public QObject {
    Q_OBJECT
private slots:
    void capturesAndSerializesActualPoints();
    void gradesRecognizedDigits();
    void savesWrongAttemptWithStrokes();
    void recognizesStrokeBuiltAnswer();
    void recognizesCapturedMarkerAnswer_data();
    void recognizesCapturedMarkerAnswer();
};

void InkTest::capturesAndSerializesActualPoints() {
    InkCanvas ink;
    ink.beginStroke(QPointF(10, 20), 0.6, 100);
    ink.appendPoint(QPointF(12, 23), 0.8, 110);
    ink.endStroke();
    const auto json = ink.strokesJson();
    QCOMPARE(json.size(), 1);
    QCOMPARE(json.at(0).toArray().size(), 2);
    QCOMPARE(json.at(0).toArray().at(1).toObject().value("x").toDouble(), 12.0);
    QCOMPARE(json.at(0).toArray().at(1).toObject().value("pressure").toDouble(), 0.8);
    QCOMPARE(json.at(0).toArray().at(1).toObject().value("ms").toInt(), 110);
}

void InkTest::gradesRecognizedDigits() {
    QCOMPARE(InkCanvas::feedbackFor("56"), QString("Great work!"));
    QCOMPARE(InkCanvas::feedbackFor("55"), QString("Not quite. Try again!"));
    QCOMPARE(InkCanvas::feedbackFor(""), QString("I couldn't read that. Try writing it again."));
}

void InkTest::savesWrongAttemptWithStrokes() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InkCanvas ink;
    ink.setDatabasePath(dir.filePath("attempts.sqlite"));
    ink.beginStroke(QPointF(4, 7), 0.5, 42);
    ink.endStroke();
    QVERIFY(ink.saveAttempt("55"));
    auto db = QSqlDatabase::addDatabase("QSQLITE", "verify");
    db.setDatabaseName(dir.filePath("attempts.sqlite"));
    QVERIFY(db.open());
    QSqlQuery query(db);
    QVERIFY(query.exec("SELECT recognized, correct, strokes_json FROM attempts"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString("55"));
    QCOMPARE(query.value(1).toInt(), 0);
    QVERIFY(query.value(2).toString().contains("\"x\":4"));
    query.finish();
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase("verify");
}

void InkTest::recognizesStrokeBuiltAnswer() {
    QTemporaryDir dir;
    InkCanvas ink;
    ink.setDatabasePath(dir.filePath("attempts.sqlite"));
    const QVector<QVector<QPointF>> digits = {
        {{35, 20}, {15, 20}, {12, 45}, {32, 45}, {34, 70}, {14, 72}},
        {{75, 20}, {55, 20}, {47, 38}, {47, 68}, {72, 70}, {74, 49}, {48, 49}}
    };
    for (const auto &digit : digits) {
        ink.beginStroke(digit.first(), 1, 1);
        for (int i = 1; i < digit.size(); ++i) ink.appendPoint(digit[i], 1, i + 1);
        ink.endStroke();
    }
    ink.submit();
    QCOMPARE(ink.error(), QString());
    QCOMPARE(ink.feedback(), QString("Great work!"));
}

void InkTest::recognizesCapturedMarkerAnswer_data() {
    QTest::addColumn<QString>("fixtureName");
    QTest::addColumn<QString>("expected");
    QTest::newRow("original-56") << QString("marker-56.json") << QString("56");
    for (int id : {10, 14, 16})
        QTest::newRow(qPrintable(QString("attempt-%1").arg(id)))
            << QString("marker-attempt-%1.json").arg(id) << QString("56");
    QTest::newRow("wrong-54") << QString("marker-attempt-11.json") << QString("54");
    QTest::newRow("unreadable") << QString("marker-attempt-12.json") << QString("");
}

void InkTest::recognizesCapturedMarkerAnswer() {
    QFETCH(QString, fixtureName);
    QFETCH(QString, expected);
    const QByteArray relativePath = ("fixtures/" + fixtureName).toUtf8();
    QFile fixture(QTest::qFindTestData(relativePath.constData(), __FILE__, __LINE__));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto strokes = QJsonDocument::fromJson(fixture.readAll()).array();
    QTemporaryDir dir;
    InkCanvas ink;
    ink.setDatabasePath(dir.filePath("attempts.sqlite"));
    for (const auto &stroke : strokes) {
        bool first = true;
        for (const auto &value : stroke.toArray()) {
            const auto point = value.toObject();
            const QPointF position(point["x"].toDouble(), point["y"].toDouble());
            if (first) ink.beginStroke(position, point["pressure"].toDouble(), qint64(point["ms"].toDouble()));
            else ink.appendPoint(position, point["pressure"].toDouble(), qint64(point["ms"].toDouble()));
            first = false;
        }
        ink.endStroke();
    }
    ink.submit();
    QCOMPARE(ink.error(), QString());
    QCOMPARE(ink.feedback(), InkCanvas::feedbackFor(expected));
    const QString connection = "captured-result";
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(dir.filePath("attempts.sqlite"));
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec("SELECT recognized, correct FROM attempts"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), expected);
        QCOMPARE(query.value(1).toBool(), expected == "56");
    }
    QSqlDatabase::removeDatabase(connection);
}

QTEST_MAIN(InkTest)
#include "ink_test.moc"
