#pragma once

#include <QJsonArray>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QVector>
#include <QUrl>

class ExerciseClient;
class PracticeClient;

struct InkPoint { QPointF position; double pressure; qint64 ms; };

class InkCanvas : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString feedback READ feedback NOTIFY feedbackChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString prompt READ prompt NOTIFY serverStateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY serverStateChanged)
    Q_PROPERTY(bool editingEnabled READ editingEnabled NOTIFY serverStateChanged)
    Q_PROPERTY(bool checkEnabled READ checkEnabled NOTIFY serverStateChanged)
    Q_PROPERTY(bool complete READ complete NOTIFY serverStateChanged)
    Q_PROPERTY(QString progress READ progress NOTIFY serverStateChanged)
    Q_PROPERTY(bool timedMode READ timedMode NOTIFY serverStateChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY serverStateChanged)
    Q_PROPERTY(QString timerText READ timerText NOTIFY serverStateChanged)
public:
    explicit InkCanvas(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    void beginStroke(QPointF point, double pressure, qint64 ms);
    void appendPoint(QPointF point, double pressure, qint64 ms);
    void endStroke();
    QJsonArray strokesJson() const;
    static QString feedbackFor(const QString &recognized);
    void setDatabasePath(const QString &path);
    bool saveAttempt(const QString &recognized);
    void connectServer(const QUrl &url);
    void connectPractice(const QUrl &url, const QString &token);
    bool timedMode() const { return m_timedMode; }
    bool canStart() const;
    QString timerText() const;
    Q_INVOKABLE void startPractice();
    Q_INVOKABLE void stopPractice();
    Q_INVOKABLE void exitPractice();
    QString prompt() const;
    bool busy() const;
    bool editingEnabled() const;
    bool checkEnabled() const;
    bool complete() const;
    QString progress() const;
    QString feedback() const { return m_feedback; }
    QString error() const { return m_error; }
    void setExternalError(const QString &message) { setError(message); }
    Q_INVOKABLE void submit();
    Q_INVOKABLE void clearInk();
signals:
    void feedbackChanged();
    void errorChanged();
    void serverStateChanged();
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
private:
    QImage recognitionImage() const;
    QString recognize(const QImage &image);
    void setFeedback(const QString &message);
    void setError(const QString &message);
    QVector<QVector<InkPoint>> m_strokes;
    QString m_dbPath;
    QString m_feedback;
    QString m_error;
    ExerciseClient *m_client = nullptr;
    PracticeClient *m_practice = nullptr;
    bool m_timedMode = false;
};
