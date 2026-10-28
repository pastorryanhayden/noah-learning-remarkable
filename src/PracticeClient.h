#pragma once
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>
class QSqlDatabase;
class PracticeClient : public QObject {
  Q_OBJECT
public:
  PracticeClient(QUrl base, QString path, QString token,
                 QObject *parent = nullptr);
  void start();
  void startPractice();
  void stopPractice();
  void submit(const QJsonArray &);
  void noteWriting();
  void clearFeedback();
  QString prompt() const;
  QString feedback() const { return m_feedback; }
  QString error() const { return m_error; }
  bool busy() const { return m_busy; }
  bool editingEnabled() const;
  bool checkEnabled() const;
  bool complete() const { return phase() == "complete" && m_pending.isEmpty(); }
  QString progressText() const;
  QString timerText() const;
  QString phase() const {
    return !m_start.isEmpty() || !m_stop.isEmpty()
               ? QString("stopped")
               : m_state["phase"].toString("stopped");
  }
  QJsonArray pendingStrokes() const { return m_pending["strokes"].toArray(); }
signals:
  void changed();
  void exerciseChanged();

private:
  void request(QString path, QJsonObject body, bool post,
               std::function<void(QJsonObject, int)> done, int timeout = 5000);
  bool database(const std::function<bool(QSqlDatabase &)> &);
  bool restore();
  bool store(QString kind, QJsonObject payload);
  bool finish(QString id, QString status, QJsonObject result = {});
  void fetch();
  bool apply(QJsonObject, bool full);
  void activity(QString kind);
  void postPending();
  void postStop();
  QUrl m_base;
  QString m_path, m_token, m_feedback, m_error;
  QJsonObject m_state, m_pending, m_start, m_stop;
  QNetworkAccessManager m_network;
  QTimer m_heartbeat, m_display;
  QElapsedTimer m_received, m_lastPen, m_question;
  qint64 m_seq = 0;
  bool m_busy = false, m_loaded = false, m_activityBusy = false,
       m_dirty = false, m_restoreDone = false, m_holding = false;
  int m_generation = 0;
};
