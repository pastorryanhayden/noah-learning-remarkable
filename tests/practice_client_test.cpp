#include "../src/PracticeClient.h"
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
class PracticeServer : public QTcpServer {
public:
  QJsonObject state{{"run_id", ""},
                    {"question_id", ""},
                    {"prompt", "Ready to practice"},
                    {"phase", "stopped"},
                    {"target_ms", 240000},
                    {"remaining_ms", 240000},
                    {"timer_running", false},
                    {"today_complete", false},
                    {"week_days", 0},
                    {"weekly_goal", 5},
                    {"feedback", ""},
                    {"last_event_seq", 0},
                    {"stats", QJsonObject{}}};
  QList<QJsonObject> writes, attempts, starts;
  QList<QByteArray> auth;
  bool failAttempt = false, failStart = false, failActivity = false;
  bool correct = true, holdAttempt = false, holdActivity = false;
  int heldActivities = 0;
  PracticeServer() {
    connect(this, &QTcpServer::newConnection, this, [this] {
      auto *s = nextPendingConnection();
      auto b = std::make_shared<QByteArray>();
      connect(s, &QTcpSocket::readyRead, this, [this, s, b] {
        *b += s->readAll();
        int n = b->indexOf("\r\n\r\n");
        if (n < 0)
          return;
        auto h = b->left(n);
        int len = 0;
        for (auto l : h.split('\n')) {
          if (l.toLower().startsWith("content-length:"))
            len = l.mid(15).trimmed().toInt();
          if (l.toLower().startsWith("authorization:"))
            auth << l.mid(14).trimmed();
        }
        if (b->size() < n + 4 + len)
          return;
        auto p = QJsonDocument::fromJson(b->mid(n + 4, len)).object();
        int status = 200;
        QJsonObject out;
        if (h.startsWith("POST /v2/runs ")) {
          starts << p;
          state["run_id"] = "11111111-1111-4111-8111-111111111111";
          state["question_id"] = "22222222-2222-4222-8222-222222222222";
          state["phase"] = "ready";
          state["prompt"] = "7 × 8 = ?";
          if (failStart)
            status = 503;
          out = state;
        } else if (h.startsWith("POST /v2/activity ")) {
          writes << p;
          if (holdActivity) {
            ++heldActivities;
            return;
          }
          if (failActivity) {
            status = 503;
            out = state;
          } else {
            state["last_event_seq"] = p["seq"];
            if (p["kind"] == "write") {
              state["phase"] = "active";
              state["timer_running"] = true;
            }
            if (p["kind"] == "stop") {
              state["phase"] = "stopped";
              state["timer_running"] = false;
            }
            if (p["kind"] == "ready" && state["phase"] == "waiting") {
              state["phase"] = "active";
              state["timer_running"] = true;
              state["wait_reason"] = "";
            }
            out = state;
          }
        } else if (h.startsWith("POST /v2/attempts ")) {
          attempts << p;
          if (holdAttempt)
            return;
          if (failAttempt)
            status = 503;
          else {
            state["phase"] = "waiting";
            state["wait_reason"] = "question";
            state["timer_running"] = false;
            if (correct) {
              state["question_id"] = "33333333-3333-4333-8333-333333333333";
              state["prompt"] = "6 × 7 = ?";
            }
            out = {
                {"attempt_id", p["attempt_id"]},
                {"run_id", p["run_id"]},
                {"question_id", p["question_id"]},
                {"recognized", correct ? "56" : "54"},
                {"correct", correct},
                {"feedback", correct ? "Great work!" : "Not quite. Try again!"},
                {"practice", state}};
          }
        } else
          out = state;
        auto body = QJsonDocument(out).toJson(QJsonDocument::Compact);
        s->write("HTTP/1.1 " + QByteArray::number(status) +
                 " OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                 "\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
      });
      connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    });
    listen(QHostAddress::LocalHost);
  }
  QUrl url() { return QUrl(QString("http://127.0.0.1:%1").arg(serverPort())); }
};
class PracticeTest : public QObject {
  Q_OBJECT
private slots:
  void startsAndOnlyPenIsWriting() {
    PracticeServer s;
    QVERIFY(s.isListening());
    QTemporaryDir d;
    PracticeClient c(s.url(), d.filePath("db"), "device-test");
    c.start();
    QTRY_VERIFY(!c.busy());
    QCOMPARE(c.phase(), QString("stopped"));
    c.startPractice();
    QTRY_VERIFY(c.editingEnabled());
    QCOMPARE(c.timerText(), QString("4:00"));
    c.clearFeedback();
    QTest::qWait(2100);
    for (auto p : s.writes)
      QVERIFY(p["kind"] != "write");
    c.noteWriting();
    QTRY_VERIFY(!s.writes.isEmpty() && s.writes.last()["kind"] == "write");
    QVERIFY(!s.auth.isEmpty());
    for (auto a : s.auth)
      QCOMPARE(a, QByteArray("Bearer device-test"));
    c.stopPractice();
    QTRY_COMPARE(c.phase(), QString("stopped"));
  }
  void savesAndRetriesAcrossRestart() {
    PracticeServer s;
    QTemporaryDir d;
    auto db = d.filePath("db");
    QJsonArray ink{QJsonArray{
        QJsonObject{{"x", 1}, {"y", 2}, {"ms", 0}, {"pressure", 1}}}};
    {
      PracticeClient c(s.url(), db, "t");
      c.start();
      QTRY_VERIFY(!c.busy());
      QCOMPARE(c.phase(), QString("stopped"));
      c.startPractice();
      QTRY_VERIFY(c.editingEnabled());
      s.failAttempt = true;
      c.submit(ink);
      QTRY_VERIFY(!c.busy());
      QVERIFY(!c.error().isEmpty());
      QVERIFY(!c.editingEnabled());
    }
    PracticeClient c(s.url(), db, "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    QCOMPARE(c.pendingStrokes(), ink);
    s.failAttempt = false;
    c.submit({});
    QTRY_COMPARE(c.prompt(), QString("6 × 7 = ?"));
    QCOMPARE(s.attempts.size(), 2);
    QCOMPARE(s.attempts[0], s.attempts[1]);
    QTRY_VERIFY(c.editingEnabled());
  }
  void lostStartUsesSameId() {
    PracticeServer s;
    QTemporaryDir d;
    auto db = d.filePath("db");
    {
      PracticeClient c(s.url(), db, "t");
      c.start();
      QTRY_VERIFY(!c.busy());
      QCOMPARE(c.phase(), QString("stopped"));
      s.failStart = true;
      c.startPractice();
      QTRY_VERIFY(!c.busy());
    }
    PracticeClient c(s.url(), db, "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    QCOMPARE(c.phase(), QString("stopped"));
    s.failStart = false;
    c.startPractice();
    QTRY_VERIFY(c.editingEnabled());
    QCOMPARE(s.starts.size(), 2);
    QCOMPARE(s.starts[0], s.starts[1]);
  }
  void heartbeatFailureKeepsSavedAnswerRetryInstructions() {
    PracticeServer s;
    QTemporaryDir d;
    PracticeClient c(s.url(), d.filePath("db"), "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    c.startPractice();
    QTRY_VERIFY(c.editingEnabled());
    s.failAttempt = true;
    const QJsonArray ink{QJsonArray{
        QJsonObject{{"x", 1}, {"y", 2}, {"ms", 0}, {"pressure", 1}}}};
    c.submit(ink);
    QTRY_VERIFY(!c.busy());
    QVERIFY(c.error().contains("saved"));
    QVERIFY(c.error().contains("Check"));
    const int eventsBefore = s.writes.size();
    s.failActivity = true;
    QTRY_VERIFY_WITH_TIMEOUT(s.writes.size() > eventsBefore, 4000);
    QTest::qWait(
        100); // Allow the failed heartbeat response to reach the client.
    QVERIFY(c.error().contains("saved"));
    QVERIFY(c.error().contains("Check"));
    QCOMPARE(c.pendingStrokes(), ink);
    QVERIFY(!c.editingEnabled());
    QVERIFY(c.checkEnabled());
  }

  void wrongGradeKeepsQuestionAndAllowsAnotherAnswer() {
    PracticeServer s;
    s.correct = false;
    QTemporaryDir d;
    PracticeClient c(s.url(), d.filePath("db"), "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    c.startPractice();
    QTRY_VERIFY(c.editingEnabled());
    const auto prompt = c.prompt();
    QSignalSpy changedQuestion(&c, &PracticeClient::exerciseChanged);
    const QJsonArray ink{QJsonArray{
        QJsonObject{{"x", 1}, {"y", 2}, {"ms", 0}, {"pressure", 1}}}};
    c.submit(ink);
    QTRY_COMPARE(c.feedback(), QString("Not quite. Try again!"));
    QVERIFY(!c.editingEnabled()); // Feedback hold must not permit editing a
                                  // submitted snapshot.
    QTRY_VERIFY(c.editingEnabled());
    QCOMPARE(c.prompt(), prompt);
    QCOMPARE(changedQuestion.size(), 0);
    QCOMPARE(s.attempts.size(), 1);
    QCOMPARE(s.attempts.first()["strokes"].toArray(), ink);
    QVERIFY(c.pendingStrokes().isEmpty());
    bool ready = false;
    for (const auto &event : s.writes)
      ready |= event["kind"] == "ready";
    QVERIFY(ready);
  }
  void stopDuringLostResponsePreservesHistoryAndDoesNotRestorePending() {
    PracticeServer s;
    s.holdAttempt = true;
    QTemporaryDir d;
    const auto path = d.filePath("db");
    const QJsonArray ink{QJsonArray{
        QJsonObject{{"x", 8}, {"y", 9}, {"ms", 10}, {"pressure", 0.5}}}};
    {
      PracticeClient c(s.url(), path, "t");
      c.start();
      QTRY_VERIFY(!c.busy());
      c.startPractice();
      QTRY_VERIFY(c.editingEnabled());
      c.submit(ink);
      QTRY_COMPARE(s.attempts.size(), 1);
      QVERIFY(c.busy());
      c.stopPractice();
      QTRY_VERIFY(!c.busy());
      QCOMPARE(c.phase(), QString("stopped"));
      QVERIFY(c.pendingStrokes().isEmpty());
      QVERIFY(!c.editingEnabled());
    }
    const QString connection = "stopped-history";
    {
      auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
      db.setDatabaseName(path);
      QVERIFY(db.open());
      QSqlQuery q(db);
      QVERIFY(q.exec("SELECT state,payload,result FROM timed_outbox WHERE "
                     "kind='attempt'"));
      QVERIFY(q.next());
      QCOMPARE(q.value(0).toString(), QString("stopped"));
      QCOMPARE(QJsonDocument::fromJson(q.value(1).toByteArray()).object(),
               s.attempts.first());
      QVERIFY(!q.next());
    }
    QSqlDatabase::removeDatabase(connection);
    PracticeClient restarted(s.url(), path, "t");
    restarted.start();
    QTRY_VERIFY(!restarted.busy());
    QCOMPARE(restarted.phase(), QString("stopped"));
    QVERIFY(restarted.pendingStrokes().isEmpty());
    QCOMPARE(s.attempts.size(), 1);
  }
  void completionShowsDayCreditAndAccuracySpeedSummary() {
    PracticeServer s;
    QTemporaryDir d;
    s.state["phase"] = "complete";
    s.state["remaining_ms"] = 0;
    s.state["today_complete"] = true;
    s.state["week_days"] = 4;
    s.state["stats"] = QJsonObject{
        {"attempts", 12}, {"correct", 9}, {"mean_response_ms", 1750}};
    PracticeClient c(s.url(), d.filePath("db"), "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    QVERIFY(c.complete());
    QVERIFY(!c.editingEnabled());
    QVERIFY(!c.checkEnabled());
    QCOMPARE(c.timerText(), QString("0:00"));
    QCOMPARE(c.progressText(),
             QString::fromUtf8("4 / 5 days · 9 / 12 correct · 1.8 s average"));
    s.state["phase"] = "stopped";
    c.start();
    QTRY_VERIFY(!c.busy());
    QVERIFY(c.progressText().contains("Today earned"));
  }
  void historicalTenMinuteRunKeepsItsExplicitTarget() {
    PracticeServer s;
    QTemporaryDir d;
    s.state["target_ms"] = 600000;
    s.state["remaining_ms"] = 600000;
    s.state["phase"] = "ready";
    PracticeClient c(s.url(), d.filePath("db"), "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    QCOMPARE(c.timerText(), QString("10:00"));
    QVERIFY(c.editingEnabled());
  }
  void connectionPauseAutomaticallyResumesWithoutReset() {
    PracticeServer s;
    QTemporaryDir d;
    PracticeClient c(s.url(), d.filePath("db"), "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    c.startPractice();
    QTRY_VERIFY(c.editingEnabled());
    s.state["phase"] = "waiting";
    s.state["wait_reason"] = "connection";
    s.state["remaining_ms"] = 180000;
    s.state["timer_running"] = false;
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
          for (const auto &p : s.writes)
            if (p["kind"] == "ready")
              return true;
          return false;
        }(),
        5000);
    QTRY_VERIFY(c.editingEnabled());
    QCOMPARE(s.state["remaining_ms"].toInt(), 180000);
    QCOMPARE(c.timerText(), QString("3:00"));
    QVERIFY(s.starts.size() == 1);
  }
  void thirteenSecondTransportGapPreservesEarnedTimeAndAutomaticallyResumes() {
    PracticeServer s;
    QTemporaryDir d;
    PracticeClient c(s.url(), d.filePath("db"), "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    c.startPractice();
    QTRY_VERIFY(c.editingEnabled());
    c.noteWriting();
    QTRY_COMPARE(c.phase(), QString("active"));
    s.state["remaining_ms"] = 180000;
    c.start();
    QTRY_VERIFY(!c.busy());
    QVERIFY(c.timerText() == "3:00");
    s.holdActivity = true;
    // Leave requests unanswered, exercising the real five-second transfer
    // timeout rather than an immediate HTTP error. Disconnected sockets are
    // deleted by the existing handler; no held reply is ever sent afterward.
    QTest::qWait(14000);
    QVERIFY(s.heldActivities >= 2);
    QVERIFY(c.error().contains("paus", Qt::CaseInsensitive));
    QVERIFY(!c.error().contains("reset", Qt::CaseInsensitive));
    QCOMPARE(s.state["remaining_ms"].toInt(), 180000);
    s.state["phase"] = "waiting";
    s.state["wait_reason"] = "connection";
    s.state["timer_running"] = false;
    s.holdActivity = false;
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
          for (const auto &event : s.writes)
            if (event["kind"] == "ready")
              return true;
          return false;
        }(),
        8000);
    QTRY_VERIFY(c.editingEnabled());
    QCOMPARE(c.phase(), QString("active"));
    QCOMPARE(c.timerText(), QString("3:00"));
    QCOMPARE(s.state["remaining_ms"].toInt(), 180000);
    QCOMPARE(s.starts.size(), 1);
    QVERIFY(s.attempts.isEmpty());
    QVERIFY(c.error().isEmpty());
  }
  void offlineStopIsDurableAndReplayedBeforeNewPractice() {
    PracticeServer s;
    QTemporaryDir d;
    const auto path = d.filePath("db");
    const QJsonArray ink{QJsonArray{
        QJsonObject{{"x", 1}, {"y", 2}, {"ms", 0}, {"pressure", 1}}}};
    {
      PracticeClient c(s.url(), path, "t");
      c.start();
      QTRY_VERIFY(!c.busy());
      c.startPractice();
      QTRY_VERIFY(c.editingEnabled());
      s.failAttempt = true;
      c.submit(ink);
      QTRY_VERIFY(!c.busy());
      s.failActivity = true;
      c.stopPractice();
      QTRY_VERIFY(!c.busy());
      QCOMPARE(c.phase(), QString("stopped"));
      QVERIFY(c.pendingStrokes().isEmpty());
      QCOMPARE(s.state["phase"].toString(),
               QString("ready")); // Failed Stop never reached the server.
    }
    const QString connection = "offline-stop-history";
    {
      auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
      db.setDatabaseName(path);
      QVERIFY(db.open());
      QSqlQuery q(db);
      QVERIFY(q.exec("SELECT count(*) FROM timed_outbox WHERE kind='stop' AND "
                     "state='pending'"));
      QVERIFY(q.next());
      QCOMPARE(q.value(0).toInt(), 1);
      QVERIFY(q.exec("SELECT state FROM timed_outbox WHERE kind='attempt'"));
      QVERIFY(q.next());
      QCOMPARE(q.value(0).toString(), QString("stopped"));
    }
    QSqlDatabase::removeDatabase(connection);
    const auto previousEvents = s.writes.size();
    s.failActivity = false;
    PracticeClient restarted(s.url(), path, "t");
    restarted.start();
    QTRY_VERIFY_WITH_TIMEOUT(s.writes.size() > previousEvents, 5000);
    QTRY_VERIFY(!restarted.busy());
    QCOMPARE(s.writes[previousEvents]["kind"].toString(), QString("stop"));
    QCOMPARE(s.state["phase"].toString(), QString("stopped"));
    QCOMPARE(restarted.phase(), QString("stopped"));
    QVERIFY(restarted.pendingStrokes().isEmpty());
    QCOMPARE(s.starts.size(), 1);
    QCOMPARE(s.attempts.size(), 1);
  }
  void networkErrorExplainsPauseInsteadOfReset() {
    PracticeServer s;
    QTemporaryDir d;
    PracticeClient c(s.url(), d.filePath("db"), "t");
    c.start();
    QTRY_VERIFY(!c.busy());
    c.startPractice();
    QTRY_VERIFY(c.editingEnabled());
    s.failActivity = true;
    QTRY_VERIFY_WITH_TIMEOUT(!c.error().isEmpty(), 4000);
    QVERIFY(c.error().contains("paus", Qt::CaseInsensitive));
    QVERIFY(!c.error().contains("reset", Qt::CaseInsensitive));
  }
  void invalidConnectionNeverExposesTokenOrSubmits_data() {
    QTest::addColumn<QString>("address");
    QTest::addColumn<QString>("token");
    QTest::newRow("remote-http") << QString("http://practice.invalid")
                                 << QString("private-device-token");
    QTest::newRow("embedded-userinfo")
        << QString("https://user@practice.invalid")
        << QString("private-device-token");
    QTest::newRow("missing-credential")
        << QString("https://practice.invalid") << QString();
  }
  void invalidConnectionNeverExposesTokenOrSubmits() {
    QFETCH(QString, address);
    QFETCH(QString, token);
    PracticeServer trap;
    QVERIFY(trap.isListening());
    QTemporaryDir d;
    const auto original = QNetworkProxy::applicationProxy();
    const auto restore =
        qScopeGuard([&] { QNetworkProxy::setApplicationProxy(original); });
    // If a bad configuration accidentally reaches the network, a local proxy
    // catches the request instead of contacting an external host.
    QNetworkProxy::setApplicationProxy(QNetworkProxy(
        QNetworkProxy::HttpProxy, "127.0.0.1", trap.serverPort()));
    PracticeClient c(QUrl(address), d.filePath("db"), token);
    c.start();
    QTRY_VERIFY(!c.busy());
    QVERIFY(!c.error().isEmpty());
    QVERIFY(!c.editingEnabled());
    QVERIFY(!c.error().contains("private-device-token"));
    c.submit(QJsonArray{QJsonArray{
        QJsonObject{{"x", 1}, {"y", 2}, {"ms", 0}, {"pressure", 1}}}});
    QTRY_VERIFY(!c.busy());
    QTest::qWait(100);
    QVERIFY(trap.auth.isEmpty());
    QVERIFY(trap.starts.isEmpty());
    QVERIFY(trap.attempts.isEmpty());
    QVERIFY(c.pendingStrokes().isEmpty());
    QVERIFY(!c.feedback().contains("Great work"));
  }
};
QTEST_GUILESS_MAIN(PracticeTest)
#include "practice_client_test.moc"
