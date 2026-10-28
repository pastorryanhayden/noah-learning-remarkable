#include "ExerciseClient.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QTimer>

ExerciseClient::ExerciseClient(QUrl baseUrl, QString databasePath, QObject *parent)
    : QObject(parent), m_base(baseUrl), m_path(databasePath) {}

bool ExerciseClient::database(const std::function<bool(QSqlDatabase &)> &operation) {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE",name);
        db.setDatabaseName(m_path);
        if (db.open()) {
            QSqlQuery schema(db);
            if (schema.exec("CREATE TABLE IF NOT EXISTS remote_attempts (attempt_id TEXT PRIMARY KEY, server_url TEXT NOT NULL, exercise_id TEXT NOT NULL, strokes_json TEXT NOT NULL, state TEXT NOT NULL, recognized TEXT, correct INTEGER, feedback TEXT, created_at TEXT NOT NULL)"))
                ok = operation(db);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    if (!ok) m_error = "Could not save or read your answer on this device.";
    return ok;
}

bool ExerciseClient::restorePending() {
    return database([this](QSqlDatabase &db) {
        QSqlQuery query(db);
        query.prepare("SELECT attempt_id,exercise_id,strokes_json FROM remote_attempts WHERE state='pending' AND server_url=? ORDER BY created_at LIMIT 1");
        query.addBindValue(m_base.toString());
        if (!query.exec()) return false;
        if (query.next()) {
            QJsonParseError error;
            auto data = QJsonDocument::fromJson(query.value(2).toByteArray(), &error);
            if (error.error != QJsonParseError::NoError || !data.isArray() || data.array().isEmpty()) return false;
            m_pending = {{"attempt_id",query.value(0).toString()}, {"exercise_id",query.value(1).toString()}, {"strokes",data.array()}};
        }
        return true;
    });
}

void ExerciseClient::start() {
    if (m_busy) return;
    m_error.clear();
    if (!restorePending()) { emit changed(); return; }
    m_ready = false;
    m_busy = true;
    emit changed();
    QNetworkRequest request(m_base.resolved(QUrl("/v1/exercise")));
    request.setTransferTimeout(5000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    auto *reply = m_network.get(request);
    connect(reply,&QNetworkReply::finished,this,[this,reply] {
        m_busy = false;
        const auto data = QJsonDocument::fromJson(reply->readAll()).object();
        const bool done = data.value("complete").toBool();
        const int position = data.value("position").toInt(-1), total = data.value("total").toInt(-1);
        const QString id = data.value("exercise_id").toString();
        const bool validId = data.value("exercise_id").isString() && (done ? id.isEmpty()
            : QRegularExpression("^[a-z0-9][a-z0-9-]{0,95}$").match(id).hasMatch());
        if (reply->error()!=QNetworkReply::NoError || !validId || !data.value("complete").isBool()
            || position<1 || total<1 || position>total || total>100 || (done && position!=total)
            || !data.value("prompt").isString() || data.value("prompt").toString().isEmpty()
            || data.value("prompt").toString().size()>128 || !data.value("feedback").isString()
            || data.value("feedback").toString().size()>256) {
            m_error = "Can't load the exercise. Tap Check to retry.";
        } else {
            const QString previousId = m_exerciseId;
            const bool recovering = !m_pending.isEmpty();
            const bool previousQuestion = recovering && m_pending.value("exercise_id").toString()!=id;
            m_exerciseId = id;
            m_complete = done && !recovering;
            m_prompt = previousQuestion ? QString("Your saved answer") : data.value("prompt").toString();
            m_progress = previousQuestion ? QString() : m_complete
                ? QString("%1 of %2 complete").arg(position).arg(total)
                : QString("Question %1 of %2").arg(position).arg(total);
            m_ready = true;
            m_feedback = recovering ? QString("Your saved answer is ready. Tap Check to retry.") : data.value("feedback").toString();
            if (!recovering && (m_clearAfterCorrect || previousId!=id || m_complete)) {
                m_clearAfterCorrect = false;
                emit exerciseChanged();
            }
        }
        reply->deleteLater();
        emit changed();
    });
}

bool ExerciseClient::savePending() {
    return database([this](QSqlDatabase &db) {
        QSqlQuery query(db);
        query.prepare("INSERT INTO remote_attempts (attempt_id,server_url,exercise_id,strokes_json,state,created_at) VALUES (?,?,?,?,'pending',?)");
        query.addBindValue(m_pending.value("attempt_id").toString());
        query.addBindValue(m_base.toString());
        query.addBindValue(m_pending.value("exercise_id").toString());
        query.addBindValue(QString::fromUtf8(QJsonDocument(pendingStrokes()).toJson(QJsonDocument::Compact)));
        query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        return query.exec();
    });
}

bool ExerciseClient::saveResult(const QJsonObject &result) {
    return database([this,&result](QSqlDatabase &db) {
        QSqlQuery query(db);
        query.prepare("UPDATE remote_attempts SET state='graded',recognized=?,correct=?,feedback=? WHERE attempt_id=? AND state='pending'");
        query.addBindValue(result.value("recognized").toString());
        query.addBindValue(result.value("correct").toBool()?1:0);
        query.addBindValue(result.value("feedback").toString());
        query.addBindValue(m_pending.value("attempt_id").toString());
        return query.exec() && query.numRowsAffected()==1;
    });
}

void ExerciseClient::submit(const QJsonArray &strokes) {
    if (m_busy || m_complete) return;
    if (!m_ready) { start(); return; }
    m_error.clear();
    if (m_pending.isEmpty()) {
        if (strokes.isEmpty()) { m_feedback="Write your answer first."; emit changed(); return; }
        m_pending = {{"attempt_id",QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"exercise_id",m_exerciseId},{"strokes",strokes}};
        if (!savePending()) { m_pending={}; emit changed(); return; }
    }
    postPending();
}

bool ExerciseClient::rejectPending() {
    return database([this](QSqlDatabase &db) {
        QSqlQuery query(db);
        query.prepare("UPDATE remote_attempts SET state='rejected' WHERE attempt_id=? AND state='pending'");
        query.addBindValue(m_pending.value("attempt_id").toString());
        return query.exec() && query.numRowsAffected()==1;
    });
}

void ExerciseClient::postPending() {
    m_busy = true;
    m_feedback = "Checking your answer…";
    emit changed();
    QNetworkRequest request(m_base.resolved(QUrl("/v1/attempts")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(60000);
    auto *reply = m_network.post(request,QJsonDocument(m_pending).toJson(QJsonDocument::Compact));
    connect(reply,&QNetworkReply::finished,this,[this,reply] {
        m_busy = false;
        const auto result = QJsonDocument::fromJson(reply->readAll()).object();
        const auto recognized = result.value("recognized");
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool rejected = status>=400 && status<500 && status!=408 && status!=429;
        const bool valid = reply->error()==QNetworkReply::NoError
            && result.value("attempt_id")==m_pending.value("attempt_id")
            && result.value("exercise_id")==m_pending.value("exercise_id")
            && recognized.isString() && QRegularExpression("^[0-9]{0,16}$").match(recognized.toString()).hasMatch()
            && result.value("correct").isBool() && result.value("feedback").isString()
            && !result.value("feedback").toString().isEmpty() && result.value("feedback").toString().size()<=256;
        if (rejected) {
            m_feedback.clear();
            if (rejectPending()) {
                m_pending = {};
                m_error = "The server could not accept this answer. It is saved. Tap Try again to rewrite it.";
            }
        } else if (!valid) {
            m_feedback.clear();
            m_error = "Couldn't check your answer. It is saved. Tap Check to retry.";
        } else if (saveResult(result)) {
            m_pending = {};
            m_feedback = result.value("feedback").toString();
            m_error.clear();
            if (result.value("correct").toBool()) {
                m_clearAfterCorrect = true;
                m_ready = false;
                m_busy = true;
                QTimer::singleShot(1000,this,[this] { m_busy=false; start(); });
            }
        }
        reply->deleteLater();
        emit changed();
    });
}

void ExerciseClient::clearFeedback() {
    if (!editingEnabled()) return;
    m_feedback.clear(); m_error.clear(); emit changed();
}
