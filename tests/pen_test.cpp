#include <QtTest>
#include <linux/input.h>
#include "../src/InkCanvas.h"
#include "../src/PenInput.h"

class PenTest : public QObject {
    Q_OBJECT
private slots:
    void mapsOnePenStrokeIntoCanvas();
    void ignoresContactOutsideAnswerBox();
};

void PenTest::mapsOnePenStrokeIntoCanvas() {
    InkCanvas ink;
    ink.setWidth(100);
    ink.setHeight(100);
    PenInput pen(&ink);
    pen.setRanges(0, 1000, 0, 2000);
    pen.processEvent(EV_ABS, ABS_X, 500, 1);
    pen.processEvent(EV_ABS, ABS_Y, 1000, 1);
    pen.processEvent(EV_ABS, ABS_PRESSURE, 2048, 1);
    pen.processEvent(EV_KEY, BTN_TOUCH, 1, 1);
    pen.processEvent(EV_SYN, SYN_REPORT, 0, 1);
    pen.processEvent(EV_ABS, ABS_X, 750, 2);
    pen.processEvent(EV_SYN, SYN_REPORT, 0, 2);
    pen.processEvent(EV_KEY, BTN_TOUCH, 0, 3);
    pen.processEvent(EV_SYN, SYN_REPORT, 0, 3);
    const auto strokes = ink.strokesJson();
    QCOMPARE(strokes.size(), 1);
    const auto points = strokes.at(0).toArray();
    QCOMPARE(points.size(), 2);
    QCOMPARE(points.at(0).toObject().value("x").toDouble(), 50.0);
    QCOMPARE(points.at(0).toObject().value("y").toDouble(), 50.0);
    QCOMPARE(points.at(0).toObject().value("pressure").toDouble(), 0.5);
    QCOMPARE(points.at(1).toObject().value("x").toDouble(), 75.0);
}

void PenTest::ignoresContactOutsideAnswerBox() {
    InkCanvas ink;
    ink.setWidth(100);
    ink.setHeight(100);
    PenInput pen(&ink);
    pen.setRanges(0, 1000, 0, 1000);
    pen.processEvent(EV_ABS, ABS_X, 1000, 1);
    pen.processEvent(EV_ABS, ABS_Y, 1000, 1);
    pen.processEvent(EV_KEY, BTN_TOUCH, 1, 1);
    pen.processEvent(EV_SYN, SYN_REPORT, 0, 1);
    pen.processEvent(EV_ABS, ABS_X, 500, 2);
    pen.processEvent(EV_ABS, ABS_Y, 500, 2);
    pen.processEvent(EV_SYN, SYN_REPORT, 0, 2);
    pen.processEvent(EV_KEY, BTN_TOUCH, 0, 3);
    pen.processEvent(EV_SYN, SYN_REPORT, 0, 3);
    QVERIFY(ink.strokesJson().isEmpty());
    // A subsequent contact inside must still produce a complete stroke.
    pen.processEvent(EV_KEY, BTN_TOUCH, 1, 4);
    pen.processEvent(EV_SYN, SYN_REPORT, 0, 4);
    pen.processEvent(EV_KEY, BTN_TOUCH, 0, 5);
    pen.processEvent(EV_SYN, SYN_REPORT, 0, 5);
    QCOMPARE(ink.strokesJson().size(), 1);
}
QTEST_MAIN(PenTest)
#include "pen_test.moc"
