#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "../src/ExerciseClient.h"

class FakeServer : public QTcpServer {
public:
    QList<QJsonObject> posts;
    bool failPost = false;
    int rejectionStatus = 0;
    int position = 0;
    bool correctAnswer = true, dropGradedResponse = false, failGet = false;
    int gets = 0;
    QHash<QString,QJsonObject> cached;
    const QStringList ids = {"multiply-7-8-v1","multiply-6-7-v1","multiply-8-9-v1"};
    const QStringList prompts = {QString::fromUtf8("7 × 8 = ?"),QString::fromUtf8("6 × 7 = ?"),QString::fromUtf8("8 × 9 = ?")};
    const QStringList answers = {"56","42","72"};
    FakeServer() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto *socket = nextPendingConnection();
            auto data = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [this,socket,data] {
                *data += socket->readAll();
                int split = data->indexOf("\r\n\r\n");
                if (split < 0) return;
                const auto head = data->left(split);
                int length = 0;
                for (auto line : head.split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (data->size() < split+4+length) return;
                QJsonObject response;
                int status = 200;
                if (head.startsWith("GET /v1/exercise ")) {
                    ++gets;
                    const bool done = position>=3;
                    response = {{"exercise_id",done?QString():ids[position]}, {"prompt",done?QString("All done!"):prompts[position]},
                        {"complete",done},{"position",done?3:position+1},{"total",3},{"feedback",done?QString("You finished all 3 questions!"):QString()}};
                    if (failGet) { status=503; response={{"error","Unavailable"}}; }
                } else {
                    auto payload = QJsonDocument::fromJson(data->mid(split+4,length)).object();
                    posts.append(payload);
                    if (failPost) { status = 503; response = {{"error","Unavailable"}}; }
                    else if (rejectionStatus) { status = rejectionStatus; response = {{"error","Invalid attempt"}}; }
                    else {
                        const auto id=payload["attempt_id"].toString();
                        if (cached.contains(id)) response=cached[id];
                        else {
                            response={{"attempt_id",payload["attempt_id"]},{"exercise_id",payload["exercise_id"]},
                                {"recognized",correctAnswer?answers[qMin(position,2)]:QString("54")},
                                {"correct",correctAnswer},{"feedback",correctAnswer?"Great work!":"Not quite. Try again!"}};
                            cached[id]=response;
                            if (correctAnswer) ++position;
                        }
                        if (dropGradedResponse) { dropGradedResponse=false;status=503;response={{"error","Lost result"}}; }
                    }
                }
                auto body = QJsonDocument(response).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 "+QByteArray::number(status)+" OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl url() const { return QUrl(QString("http://127.0.0.1:%1").arg(serverPort())); }
};

class ClientTest : public QObject {
    Q_OBJECT
private slots:
    void fetchesAndPreservesServerResult() {
        FakeServer server;
        QVERIFY(server.isListening());
        QTemporaryDir dir;
        ExerciseClient client(server.url(),dir.filePath("attempts.sqlite"));
        client.start();
        QTRY_VERIFY(client.ready());
        QCOMPARE(client.prompt(),QString::fromUtf8("7 × 8 = ?"));
        QJsonArray strokes{QJsonArray{QJsonObject{{"x",2},{"y",3},{"pressure",0.6},{"ms",10}}}};
        client.submit(strokes);
        QVERIFY(client.busy());
        client.submit(strokes); // A second Check while busy must not duplicate work.
        QTRY_COMPARE(client.feedback(),QString("Great work!"));
        QCOMPARE(server.posts.size(),1);
        QCOMPARE(server.posts[0]["strokes"].toArray(),strokes);
        QVERIFY(!client.editingEnabled()); // Hold correct feedback before loading the next question.
        QTRY_COMPARE(client.prompt(),QString::fromUtf8("6 × 7 = ?"));
        QVERIFY(client.editingEnabled());
        auto db = QSqlDatabase::addDatabase("QSQLITE","verify-remote");
        db.setDatabaseName(dir.filePath("attempts.sqlite"));
        QVERIFY(db.open());
        { QSqlQuery q(db); QVERIFY(q.exec("SELECT state,recognized,correct,strokes_json FROM remote_attempts"));
          QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("graded")); QCOMPARE(q.value(1).toString(),QString("56"));
          QCOMPARE(q.value(2).toInt(),1); QCOMPARE(QJsonDocument::fromJson(q.value(3).toByteArray()).array(),strokes); }
        db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase("verify-remote");
    }
    void restoresFailureAndRetriesIdenticalSnapshot() {
        FakeServer server;
        server.failPost = true;
        QTemporaryDir dir;
        const auto path=dir.filePath("attempts.sqlite");
        QJsonArray strokes{QJsonArray{QJsonObject{{"x",2},{"y",3},{"pressure",0.6},{"ms",10}}}};
        {
            ExerciseClient client(server.url(),path);
            client.start(); QTRY_VERIFY(client.ready());
            client.submit(strokes);
            QTRY_VERIFY(!client.busy());
            QVERIFY(!client.error().isEmpty());
            QVERIFY(!client.editingEnabled());
        }
        ExerciseClient restarted(server.url(),path);
        restarted.start(); QTRY_VERIFY(restarted.ready());
        QCOMPARE(restarted.pendingStrokes(),strokes);
        server.failPost = false;
        restarted.submit(QJsonArray{}); // Retry stored data, never current canvas data.
        QTRY_COMPARE(restarted.feedback(),QString("Great work!"));
        QCOMPARE(server.posts.size(),2);
        QCOMPARE(server.posts[0],server.posts[1]);
    }
    void wrongStaysCorrectAdvancesAndFinalDisablesInput() {
        FakeServer server; QTemporaryDir dir;
        ExerciseClient client(server.url(),dir.filePath("attempts.sqlite"));
        QSignalSpy changes(&client,&ExerciseClient::exerciseChanged);
        client.start(); QTRY_VERIFY(client.ready());
        QJsonArray strokes{QJsonArray{QJsonObject{{"x",2},{"y",3},{"pressure",0.6},{"ms",10}}}};
        server.correctAnswer=false;
        client.submit(strokes); QTRY_VERIFY(!client.busy());
        QCOMPARE(client.prompt(),QString::fromUtf8("7 × 8 = ?"));
        QCOMPARE(server.gets,1); QVERIFY(client.editingEnabled());
        server.correctAnswer=true;
        for (int index=0;index<3;++index) {
            client.submit(strokes);
            QTRY_COMPARE(server.position,index+1);
            QTRY_VERIFY(!client.busy());
        }
        QVERIFY(client.complete()); QVERIFY(!client.editingEnabled()); QVERIFY(!client.checkEnabled());
        QCOMPARE(client.prompt(),QString("All done!"));
        QCOMPARE(changes.size(),4); // Initial load, two next questions, completion.
        client.submit(strokes); QCOMPARE(server.posts.size(),4);
    }
    void lostCorrectResponseReconcilesWithoutSkipping() {
        FakeServer server; server.dropGradedResponse=true; QTemporaryDir dir;
        const auto path=dir.filePath("attempts.sqlite");
        QJsonArray strokes{QJsonArray{QJsonObject{{"x",2},{"y",3},{"pressure",0.6},{"ms",10}}}};
        { ExerciseClient client(server.url(),path); client.start(); QTRY_VERIFY(client.ready());
          client.submit(strokes); QTRY_VERIFY(!client.busy()); QVERIFY(!client.error().isEmpty()); }
        ExerciseClient restarted(server.url(),path); restarted.start(); QTRY_VERIFY(restarted.ready());
        QCOMPARE(restarted.prompt(),QString("Your saved answer"));
        QCOMPARE(restarted.pendingStrokes(),strokes);
        restarted.submit(QJsonArray{});
        QTRY_COMPARE(restarted.prompt(),QString::fromUtf8("6 × 7 = ?"));
        QCOMPARE(server.position,1); QCOMPARE(server.posts[0],server.posts[1]);
    }
    void lostFinalResponseCanRetryBeforeShowingCompletion() {
        FakeServer server; server.position=2; server.dropGradedResponse=true; QTemporaryDir dir;
        const auto path=dir.filePath("attempts.sqlite");
        QJsonArray strokes{QJsonArray{QJsonObject{{"x",2},{"y",3},{"pressure",0.6},{"ms",10}}}};
        { ExerciseClient client(server.url(),path); client.start(); QTRY_VERIFY(client.ready());
          client.submit(strokes); QTRY_VERIFY(!client.busy()); QVERIFY(!client.error().isEmpty()); }
        ExerciseClient restarted(server.url(),path); restarted.start(); QTRY_VERIFY(restarted.ready());
        QVERIFY(!restarted.complete()); QVERIFY(restarted.checkEnabled());
        QCOMPARE(restarted.prompt(),QString("Your saved answer"));
        restarted.submit(QJsonArray{}); QTRY_VERIFY(restarted.complete());
        QCOMPARE(server.position,3); QCOMPARE(server.posts[0],server.posts[1]);
    }
    void failedAdvanceFetchRetriesLoadingNotSubmission() {
        FakeServer server; QTemporaryDir dir;
        ExerciseClient client(server.url(),dir.filePath("attempts.sqlite"));
        client.start(); QTRY_VERIFY(client.ready());
        server.failGet=true;
        QJsonArray strokes{QJsonArray{QJsonObject{{"x",2},{"y",3},{"pressure",0.6},{"ms",10}}}};
        client.submit(strokes); QTRY_VERIFY(!client.busy());
        QVERIFY(!client.ready()); QVERIFY(!client.editingEnabled());
        server.failGet=false; client.submit(strokes);
        QTRY_COMPARE(client.prompt(),QString::fromUtf8("6 × 7 = ?"));
        QCOMPARE(server.posts.size(),1);
    }
    void permanentRejectionPreservesAnswerWithoutLockingCanvas() {
        FakeServer server;
        server.rejectionStatus = 400;
        QTemporaryDir dir;
        const auto path = dir.filePath("attempts.sqlite");
        {
            ExerciseClient client(server.url(),path);
            client.start(); QTRY_VERIFY(client.ready());
            QJsonArray strokes{QJsonArray{QJsonObject{{"x",2},{"y",3},{"pressure",0.6},{"ms",10}}}};
            client.submit(strokes);
            QTRY_VERIFY(!client.busy());
            QVERIFY(!client.error().isEmpty());
            QVERIFY(client.editingEnabled());
            QVERIFY(client.pendingStrokes().isEmpty());
            auto db=QSqlDatabase::addDatabase("QSQLITE","rejection");
            db.setDatabaseName(path); QVERIFY(db.open());
            { QSqlQuery q(db); QVERIFY(q.exec("SELECT state,strokes_json,correct FROM remote_attempts"));
              QVERIFY(q.next()); QCOMPARE(q.value(0).toString(),QString("rejected"));
              QCOMPARE(QJsonDocument::fromJson(q.value(1).toByteArray()).array(),strokes); QVERIFY(q.value(2).isNull()); }
            db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase("rejection");
        }
        ExerciseClient restarted(server.url(),path);
        restarted.start(); QTRY_VERIFY(restarted.ready());
        QVERIFY(restarted.editingEnabled());
    }
};
QTEST_GUILESS_MAIN(ClientTest)
#include "server_client_test.moc"
