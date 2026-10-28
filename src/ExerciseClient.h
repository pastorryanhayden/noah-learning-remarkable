#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <functional>

class ExerciseClient : public QObject {
    Q_OBJECT
public:
    ExerciseClient(QUrl baseUrl, QString databasePath, QObject *parent = nullptr);
    void start();
    void submit(const QJsonArray &strokes);
    QString prompt() const { return m_prompt; }
    QString feedback() const { return m_feedback; }
    QString error() const { return m_error; }
    bool busy() const { return m_busy; }
    bool ready() const { return m_ready; }
    bool complete() const { return m_complete; }
    bool checkEnabled() const { return !m_busy && !m_complete; }
    QString progressText() const { return m_progress; }
    bool editingEnabled() const { return m_ready && !m_busy && !m_complete && m_pending.isEmpty(); }
    QJsonArray pendingStrokes() const { return m_pending.value("strokes").toArray(); }
    void clearFeedback();
signals:
    void changed();
    void exerciseChanged();
private:
    void postPending();
    bool restorePending();
    bool savePending();
    bool saveResult(const QJsonObject &result);
    bool rejectPending();
    bool database(const std::function<bool(class QSqlDatabase &)> &operation);
    QUrl m_base;
    QString m_path, m_prompt, m_exerciseId, m_feedback, m_error;
    QJsonObject m_pending;
    QNetworkAccessManager m_network;
    bool m_busy = false, m_ready = false;
    bool m_complete = false, m_clearAfterCorrect = false;
    QString m_progress;
};
