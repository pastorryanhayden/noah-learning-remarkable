#include "PracticeClient.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <cmath>

PracticeClient::PracticeClient(QUrl base, QString path, QString token,
                               QObject *parent)
    : QObject(parent), m_base(base), m_path(path), m_token(token) {
  m_heartbeat.setInterval(2000);
  m_display.setInterval(1000);
  connect(&m_display, &QTimer::timeout, this, &PracticeClient::changed);
  connect(&m_heartbeat, &QTimer::timeout, this, [this] {
    if (!m_stop.isEmpty()) {
      if (!m_busy)
        postStop();
      return;
    }
    if (!m_loaded || phase() == "stopped" || phase() == "complete")
      return;
    if (m_dirty && m_lastPen.isValid() && m_lastPen.elapsed() <= 5000)
      activity("write");
    else {
      m_dirty = false;
      activity("heartbeat");
    }
  });
}
bool PracticeClient::database(const std::function<bool(QSqlDatabase &)> &op) {
  QDir().mkpath(QFileInfo(m_path).absolutePath());
  auto name = QUuid::createUuid().toString();
  bool ok = false;
  {
    auto db = QSqlDatabase::addDatabase("QSQLITE", name);
    db.setDatabaseName(m_path);
    if (db.open()) {
      QSqlQuery q(db);
      if (q.exec("CREATE TABLE IF NOT EXISTS timed_outbox (id TEXT PRIMARY "
                 "KEY,server_url TEXT NOT NULL,kind TEXT NOT NULL,payload TEXT "
                 "NOT NULL,state TEXT NOT NULL,result TEXT,created_at TEXT NOT "
                 "NULL)"))
        ok = op(db);
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(name);
  if (!ok)
    m_error = "Could not save or read your answer on this device.";
  return ok;
}
bool PracticeClient::restore() {
  return database([this](QSqlDatabase &db) {
    QSqlQuery q(db);
    q.prepare("SELECT kind,payload FROM timed_outbox WHERE server_url=? AND "
              "state='pending' ORDER BY created_at");
    q.addBindValue(m_base.toString());
    if (!q.exec())
      return false;
    while (q.next()) {
      QJsonParseError e;
      auto p = QJsonDocument::fromJson(q.value(1).toByteArray(), &e).object();
      if (e.error != QJsonParseError::NoError || p.isEmpty())
        return false;
      if (q.value(0) == "start")
        m_start = p;
      else if (q.value(0) == "stop")
        m_stop = p;
      else
        m_pending = p;
    }
    return true;
  });
}
bool PracticeClient::store(QString kind, QJsonObject p) {
  return database([&](QSqlDatabase &db) {
    QSqlQuery q(db);
    q.prepare("INSERT INTO timed_outbox "
              "(id,server_url,kind,payload,state,created_at) VALUES "
              "(?,?,?,?,'pending',?)");
    q.addBindValue(
        p[kind == "attempt" ? "attempt_id" : "request_id"].toString());
    q.addBindValue(m_base.toString());
    q.addBindValue(kind);
    q.addBindValue(
        QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Compact)));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return q.exec();
  });
}
bool PracticeClient::finish(QString id, QString status, QJsonObject result) {
  return database([&](QSqlDatabase &db) {
    QSqlQuery q(db);
    q.prepare("UPDATE timed_outbox SET state=?,result=? WHERE id=? AND "
              "state='pending'");
    q.addBindValue(status);
    q.addBindValue(QString::fromUtf8(
        QJsonDocument(result).toJson(QJsonDocument::Compact)));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
  });
}
void PracticeClient::request(QString path, QJsonObject body, bool post,
                             std::function<void(QJsonObject, int)> done,
                             int timeout) {
  const bool local = m_base.host() == "127.0.0.1" ||
                     m_base.host() == "localhost" || m_base.host() == "::1";
  if (m_token.isEmpty() || (!local && m_base.scheme() != "https") ||
      !m_base.userInfo().isEmpty()) {
    QTimer::singleShot(0, this, [done] { done({}, 0); });
    return;
  }
  QNetworkRequest req(m_base.resolved(QUrl(path)));
  req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setTransferTimeout(timeout);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::ManualRedirectPolicy);
  auto *reply =
      post ? m_network.post(req,
                            QJsonDocument(body).toJson(QJsonDocument::Compact))
           : m_network.get(req);
  connect(reply, &QNetworkReply::finished, this, [reply, done] {
    int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && status < 400)
      status = 0;
    auto data = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    done(data, status);
  });
}
bool PracticeClient::apply(QJsonObject s, bool full) {
  const QString p = s["phase"].toString();
  const double remain = s["remaining_ms"].toDouble(-1);
  if (!QStringList{"ready", "active", "waiting", "complete", "stopped"}
           .contains(p) ||
      !s["run_id"].isString() || !s["question_id"].isString() ||
      !s["prompt"].isString() || s["prompt"].toString().size() > 128 ||
      !std::isfinite(remain) || remain < 0 || remain > 600000 ||
      !s["timer_running"].isBool() || !s["today_complete"].isBool() ||
      s["week_days"].toInt(-1) < 0 || s["week_days"].toInt() > 7 ||
      s["last_event_seq"].toDouble(-1) < 0)
    return false;
  const auto oldId = m_state["question_id"];
  const auto oldRun = m_state["run_id"];
  m_seq = qMax(m_seq, qint64(s["last_event_seq"].toDouble()));
  if (full) {
    m_state = s;
    m_loaded = true;
    if (oldId != s["question_id"] || oldRun != s["run_id"]) {
      m_question.start();
      emit exerciseChanged();
    }
  } else
    for (auto key : {"remaining_ms", "target_ms", "timer_running",
                     "today_complete", "week_days", "weekly_goal", "stats"})
      m_state[key] = s[key];
  m_received.start();
  return true;
}
void PracticeClient::start() {
  if (m_busy)
    return;
  m_error.clear();
  if (!m_restoreDone) {
    if (!restore()) {
      emit changed();
      return;
    }
    m_restoreDone = true;
  }
  if (!m_stop.isEmpty())
    postStop();
  else
    fetch();
  m_heartbeat.start();
  m_display.start();
}
void PracticeClient::fetch() {
  m_busy = true;
  emit changed();
  const int generation = m_generation;
  request(
      "/v2/practice", {}, false, [this, generation](QJsonObject s, int status) {
        if (generation != m_generation)
          return;
        m_busy = false;
        const bool pending = !m_pending.isEmpty() || !m_start.isEmpty();
        if (status != 200 || !apply(s, !pending)) {
          m_error =
              "Can't reach the practice server. Tap Check or Start to retry.";
        } else {
          if (pending) {
            m_loaded = true;
            m_state = s;
            m_received.start();
            m_feedback = m_pending.isEmpty()
                             ? "Tap Start to recover your practice run."
                             : "Your answer is saved. Tap Check to retry.";
          } else {
            m_error.clear();
            m_feedback = s["feedback"].toString();
            if (phase() == "waiting" && (s["wait_reason"] == "question" ||
                                         s["wait_reason"] == "connection"))
              activity("ready");
          }
        }
        emit changed();
      });
}
void PracticeClient::startPractice() {
  if (m_busy)
    return;
  if (!m_stop.isEmpty()) {
    postStop();
    return;
  }
  if (m_start.isEmpty() && m_loaded && phase() != "stopped" &&
      phase() != "complete")
    return;
  m_error.clear();
  if (m_start.isEmpty()) {
    m_start = {
        {"request_id", QUuid::createUuid().toString(QUuid::WithoutBraces)}};
    if (!store("start", m_start)) {
      m_start = {};
      emit changed();
      return;
    }
  }
  ++m_generation;
  m_activityBusy = false;
  m_dirty = false;
  m_lastPen.invalidate();
  m_busy = true;
  emit changed();
  const int generation = m_generation;
  request("/v2/runs", m_start, true,
          [this, generation](QJsonObject s, int status) {
            if (generation != m_generation)
              return;
            m_busy = false;
            if (status != 200 || !apply(s, true))
              m_error = "Couldn't start practice. Tap Start to retry.";
            else if (finish(m_start["request_id"].toString(), "done", s)) {
              m_start = {};
              m_feedback = s["feedback"].toString();
              m_error.clear();
              m_question.start();
              m_heartbeat.start();
              m_display.start();
            }
            emit changed();
          });
}
void PracticeClient::noteWriting() {
  if (!editingEnabled())
    return;
  m_feedback.clear();
  const bool first = !m_lastPen.isValid() || m_lastPen.elapsed() > 2000;
  m_lastPen.start();
  m_dirty = true;
  if (first)
    activity("write");
}
void PracticeClient::activity(QString kind) {
  if (m_activityBusy || m_state["run_id"].toString().isEmpty())
    return;
  if (kind == "write" && (!m_lastPen.isValid() || m_lastPen.elapsed() > 5000))
    return;
  QJsonObject p{
      {"run_id", m_state["run_id"]}, {"seq", double(++m_seq)}, {"kind", kind}};
  if (kind == "write") {
    p["age_ms"] = int(m_lastPen.elapsed());
    m_dirty = false;
  }
  m_activityBusy = true;
  const int generation = m_generation;
  request(
      "/v2/activity", p, true,
      [this, generation, kind](QJsonObject s, int status) {
        m_activityBusy = false;
        if (generation != m_generation)
          return;
        bool full =
            !m_busy && !m_holding && m_pending.isEmpty() && m_start.isEmpty();
        if (status != 200 || !apply(s, full)) {
          m_error =
              m_pending.isEmpty()
                  ? "Connection lost. Practice is paused; your time is saved."
                  : "Connection lost. Your answer is saved. Tap Check to "
                    "retry.";
          m_state["timer_running"] = false;
        } else {
          if (m_pending.isEmpty() && m_start.isEmpty())
            m_error.clear();
          if (kind == "ready")
            m_question.start();
          if (full && phase() == "waiting" &&
              (s["wait_reason"] == "question" ||
               s["wait_reason"] == "connection") &&
              kind != "ready")
            QTimer::singleShot(0, this, [this] { activity("ready"); });
        }
        emit changed();
      });
}
void PracticeClient::submit(const QJsonArray &ink) {
  if (m_busy)
    return;
  if (!m_loaded) {
    start();
    return;
  }
  if (!m_start.isEmpty()) {
    startPractice();
    return;
  }
  m_error.clear();
  if (m_pending.isEmpty()) {
    if (!editingEnabled() || ink.isEmpty()) {
      m_feedback = "Write your answer first.";
      emit changed();
      return;
    }
    m_pending = {
        {"attempt_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"run_id", m_state["run_id"]},
        {"question_id", m_state["question_id"]},
        {"strokes", ink},
        {"response_ms",
         int(qMin(qint64(600000),
                  m_question.isValid() ? m_question.elapsed() : qint64(0)))}};
    if (!store("attempt", m_pending)) {
      m_pending = {};
      emit changed();
      return;
    }
  }
  postPending();
}
void PracticeClient::postPending() {
  m_busy = true;
  m_feedback = "Checking your answer…";
  m_state["timer_running"] = false;
  emit changed();
  const int generation = m_generation;
  const auto payload = m_pending;
  request(
      "/v2/attempts", payload, true,
      [this, generation, payload](QJsonObject r, int status) {
        if (generation != m_generation)
          return;
        m_busy = false;
        const bool valid =
            status == 200 && r["attempt_id"] == payload["attempt_id"] &&
            r["run_id"] == payload["run_id"] &&
            r["question_id"] == payload["question_id"] &&
            r["correct"].isBool() && r["recognized"].isString() &&
            QRegularExpression("^[0-9]{0,16}$")
                .match(r["recognized"].toString())
                .hasMatch() &&
            r["feedback"].isString() &&
            r["feedback"].toString().size() <= 256 && r["practice"].isObject();
        if (!valid) {
          m_feedback.clear();
          if ((status == 400 || status == 409) &&
              finish(payload["attempt_id"].toString(), "rejected", r)) {
            m_pending = {};
            m_error = "This saved answer no longer matches the question. Tap "
                      "Check to reload.";
            m_loaded = false;
          } else
            m_error =
                "Couldn't check your answer. It is saved. Tap Check to retry.";
        } else if (finish(payload["attempt_id"].toString(), "graded", r)) {
          m_pending = {};
          m_feedback = r["feedback"].toString();
          m_error.clear();
          m_busy = true;
          m_holding = true;
          const bool correct = r["correct"].toBool();
          QTimer::singleShot(1000, this, [this, generation, correct] {
            if (generation != m_generation)
              return;
            m_busy = false;
            m_holding = false;
            if (correct)
              emit exerciseChanged();
            fetch();
          });
        }
        emit changed();
      },
      60000);
}
void PracticeClient::stopPractice() {
  if (m_state["run_id"].toString().isEmpty())
    return;
  ++m_generation;
  m_busy = false;
  m_holding = false;
  m_dirty = false;
  m_lastPen.invalidate();
  m_activityBusy = false;
  // Pending snapshots remain in history, but a stopped run cannot be
  // resubmitted.
  if (!m_pending.isEmpty()) {
    if (!finish(m_pending["attempt_id"].toString(), "stopped"))
      return;
    m_pending = {};
  }
  if (!m_start.isEmpty()) {
    if (!finish(m_start["request_id"].toString(), "stopped"))
      return;
    m_start = {};
  }
  if (m_stop.isEmpty()) {
    m_stop = {
        {"request_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"run_id", m_state["run_id"]},
        {"seq", double(++m_seq)},
        {"kind", "stop"}};
    if (!store("stop", m_stop)) {
      m_stop = {};
      emit changed();
      return;
    }
  }
  postStop();
}
void PracticeClient::postStop() {
  if (m_busy || m_stop.isEmpty())
    return;
  m_busy = true;
  const auto saved = m_stop;
  auto p = saved;
  p.remove("request_id");
  request("/v2/activity", p, true, [this](QJsonObject s, int status) {
    m_busy = false;
    if (status == 200 && apply(s, true)) {
      if (finish(m_stop["request_id"].toString(), "done", s)) {
        m_stop = {};
        m_error.clear();
        m_feedback = s["feedback"].toString();
        emit exerciseChanged();
      }
    } else if (status == 400 || status == 409) {
      if (finish(m_stop["request_id"].toString(), "superseded", s)) {
        m_stop = {};
        fetch();
      }
    } else {
      m_state["phase"] = "stopped";
      m_state["timer_running"] = false;
      m_error = "Stop is saved here and will sync when the connection returns.";
    }
    emit changed();
  });
  emit changed();
}
void PracticeClient::clearFeedback() {
  if (!editingEnabled())
    return;
  m_feedback.clear();
  m_error.clear();
  emit changed();
}
QString PracticeClient::prompt() const {
  if (complete())
    return "Great work today!";
  if (!m_pending.isEmpty() &&
      m_pending["question_id"] != m_state["question_id"])
    return "Your saved answer";
  return m_state["prompt"].toString("Loading practice…");
}
bool PracticeClient::editingEnabled() const {
  return m_loaded && !m_busy && m_pending.isEmpty() && m_start.isEmpty() &&
         (phase() == "ready" || phase() == "active");
}
bool PracticeClient::checkEnabled() const {
  return !m_busy && (!m_pending.isEmpty() || !m_loaded || editingEnabled());
}
QString PracticeClient::progressText() const {
  if (complete()) {
    auto s = m_state["stats"].toObject();
    return QString("%1 / 5 days · %2 / %3 correct · %4 s average")
        .arg(m_state["week_days"].toInt())
        .arg(s["correct"].toInt())
        .arg(s["attempts"].toInt())
        .arg(s["mean_response_ms"].toDouble() / 1000.0, 0, 'f', 1);
  }
  return QString("%1 / 5 days this week%2")
      .arg(m_state["week_days"].toInt())
      .arg(m_state["today_complete"].toBool() ? " · Today earned" : "");
}
QString PracticeClient::timerText() const {
  qint64 ms = phase() == "stopped" ? m_state["next_target_ms"].toInt(240000)
                                   : m_state["remaining_ms"].toInt(240000);
  if (m_state["timer_running"].toBool() && m_received.isValid())
    ms -= qMin(qint64(4000), m_received.elapsed());
  int seconds = int(qMax(qint64(0), ms) + 999) / 1000;
  return QString("%1:%2")
      .arg(seconds / 60)
      .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
