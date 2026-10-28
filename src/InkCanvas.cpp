#include "InkCanvas.h"
#include "ExerciseClient.h"
#include "PracticeClient.h"
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUuid>

InkCanvas::InkCanvas(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    m_dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/attempts.sqlite";
}

void InkCanvas::beginStroke(QPointF p, double pressure, qint64 ms) {
    if (!editingEnabled()) return;
    m_strokes.append({{p, pressure, ms}});
    if (m_practice) m_practice->noteWriting();
    setFeedback("");
    update(QRectF(p - QPointF(4, 4), QSizeF(8, 8)).toAlignedRect());
}

void InkCanvas::appendPoint(QPointF p, double pressure, qint64 ms) {
    if (!editingEnabled()) return;
    if (m_strokes.isEmpty()) return;
    const QPointF previous = m_strokes.last().last().position;
    m_strokes.last().append({p, pressure, ms});
    if (m_practice) m_practice->noteWriting();
    // Include round caps and antialiasing, without invalidating the whole canvas.
    update(QRectF(previous, p).normalized().adjusted(-4, -4, 4, 4).toAlignedRect());
}

void InkCanvas::endStroke() {}

QJsonArray InkCanvas::strokesJson() const {
    QJsonArray strokes;
    for (const auto &stroke : m_strokes) {
        QJsonArray points;
        for (const auto &point : stroke) {
            QJsonObject p;
            p["x"] = point.position.x(); p["y"] = point.position.y();
            p["pressure"] = point.pressure; p["ms"] = double(point.ms);
            points.append(p);
        }
        strokes.append(points);
    }
    return strokes;
}

QString InkCanvas::feedbackFor(const QString &recognized) {
    if (recognized.isEmpty()) return "I couldn't read that. Try writing it again.";
    return recognized == "56" ? "Great work!" : "Not quite. Try again!";
}

void InkCanvas::setDatabasePath(const QString &path) { m_dbPath = path; }

bool InkCanvas::saveAttempt(const QString &recognized) {
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());
    const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool ok = false;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(m_dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("CREATE TABLE IF NOT EXISTS attempts (id INTEGER PRIMARY KEY, question TEXT NOT NULL, recognized TEXT NOT NULL, correct INTEGER NOT NULL, strokes_json TEXT NOT NULL, created_at TEXT NOT NULL)");
            if (ok) {
                query.prepare("INSERT INTO attempts (question, recognized, correct, strokes_json, created_at) VALUES (?, ?, ?, ?, ?)");
                query.addBindValue("7 x 8");
                query.addBindValue(recognized.isNull() ? QString("") : recognized);
                query.addBindValue(recognized == "56" ? 1 : 0);
                query.addBindValue(QString::fromUtf8(QJsonDocument(strokesJson()).toJson(QJsonDocument::Compact)));
                query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                ok = query.exec();
            }
            if (!ok) setError(query.lastError().text());
            db.close();
        } else setError(db.lastError().text());
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

void InkCanvas::paint(QPainter *painter) {
    painter->fillRect(boundingRect(), Qt::white);
    painter->setRenderHint(QPainter::Antialiasing, !qEnvironmentVariableIsSet("RM_MONO_INK"));
    QPen pen(Qt::black, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    for (const auto &stroke : m_strokes) {
        if (stroke.size() == 1) painter->drawPoint(stroke[0].position);
        for (int i = 1; i < stroke.size(); ++i)
            painter->drawLine(stroke[i-1].position, stroke[i].position);
    }
}

QImage InkCanvas::recognitionImage() const {
    QImage image(600, 240, QImage::Format_RGB32);
    image.fill(QColor(Qt::white));
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool first = true;
    for (const auto &stroke : m_strokes) for (const auto &point : stroke) {
        const auto p = point.position;
        if (first) { minX = maxX = p.x(); minY = maxY = p.y(); first = false; }
        else { minX = qMin(minX, p.x()); maxX = qMax(maxX, p.x()); minY = qMin(minY, p.y()); maxY = qMax(maxY, p.y()); }
    }
    if (first) return image;
    const QRectF bounds(QPointF(minX, minY), QPointF(maxX, maxY));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double scale = qMin(500.0 / qMax(1.0, bounds.width()), 160.0 / qMax(1.0, bounds.height()));
    painter.translate(50, 40);
    painter.scale(scale, scale);
    painter.translate(-bounds.topLeft());
    painter.setPen(QPen(Qt::black, qMax(3.0, 7.0 / scale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const auto &stroke : m_strokes) {
        if (stroke.size() == 1) painter.drawPoint(stroke[0].position);
        for (int i = 1; i < stroke.size(); ++i)
            painter.drawLine(stroke[i-1].position, stroke[i].position);
    }
    return image;
}

QString InkCanvas::recognize(const QImage &image) {
    const QString debugPath = qEnvironmentVariable("RM_DEBUG_OCR_IMAGE");
    if (!debugPath.isEmpty()) image.save(debugPath);
    // Preserve the raw-line view, but also try a tight line view: excessive
    // white margins can make Tesseract collapse two handwritten digits into one.
    QRect inkBounds;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 250)
                inkBounds |= QRect(x, y, 1, 1);
    if (inkBounds.isEmpty()) return QString("");
    QImage line(inkBounds.size() + QSize(40, 40), QImage::Format_RGB32);
    line.fill(Qt::white);
    { QPainter painter(&line); painter.drawImage(QPoint(20, 20), image.copy(inkBounds)); }

    QString best("");
    double bestConfidence = -1;
    QElapsedTimer timer;
    timer.start();
    const QVector<QPair<QImage, QString>> views = {{image, "13"}, {line, "7"}};
    for (const auto &view : views) {
        QTemporaryFile file(QDir::tempPath() + "/remarkable-ink-XXXXXX.png");
        if (!file.open() || !view.first.save(&file, "PNG")) {
            setError("Could not prepare handwriting for recognition."); return {};
        }
        file.flush();
        QProcess process;
        process.start("tesseract", {file.fileName(), "stdout", "--psm", view.second,
            "-c", "tessedit_char_whitelist=0123456789", "-c", "tessedit_create_tsv=1"});
        auto remaining = [&] { return qMax(0, 10000 - int(timer.elapsed())); };
        if (!process.waitForStarted(remaining()) || !process.waitForFinished(remaining())
            || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            process.kill();
            process.waitForFinished(1000);
            setError("Handwriting recognition is unavailable. Check Tesseract installation.");
            return {};
        }
        QString digits;
        double weightedConfidence = 0;
        for (const auto &row : process.readAllStandardOutput().split('\n')) {
            const auto fields = row.split('\t');
            if (fields.size() != 12 || fields[0] != "5") continue;
            const QString text = QString::fromUtf8(fields[11]).trimmed();
            bool confidenceValid = false;
            const double confidence = fields[10].toDouble(&confidenceValid);
            if (!confidenceValid || confidence < 0 || text.isEmpty()
                || text.contains(QRegularExpression("[^0-9]"))) continue;
            digits += text;
            weightedConfidence += confidence * text.size();
        }
        const double confidence = digits.isEmpty() ? -1 : weightedConfidence / digits.size();
        if (confidence > bestConfidence) {
            bestConfidence = confidence;
            best = digits;
        }
    }
    return best;
}

void InkCanvas::submit() {
    if (m_timedMode) { if (m_practice) m_practice->submit(strokesJson()); return; }
    if (m_client) { m_client->submit(strokesJson()); return; }
    if (m_strokes.isEmpty()) { setFeedback("Write your answer first."); return; }
    setError("");
    const QString result = recognize(recognitionImage());
    if (!saveAttempt(result)) return;
    if (m_error.isEmpty()) setFeedback(feedbackFor(result));
}

void InkCanvas::clearInk() {
    if (!editingEnabled()) return;
    m_strokes.clear();
    if (m_client) m_client->clearFeedback();
    if (m_practice) m_practice->clearFeedback();
    setFeedback(""); setError(""); update();
}

QString InkCanvas::prompt() const {
    if (m_timedMode) return m_practice ? m_practice->prompt() : QString("Ready to practice");
    if (!m_client) return QString::fromUtf8("7 × 8 = ?");
    return m_client->prompt().isEmpty() ? QString("Loading exercise…") : m_client->prompt();
}
bool InkCanvas::busy() const { return m_timedMode ? (m_practice && m_practice->busy()) : (m_client && m_client->busy()); }
bool InkCanvas::editingEnabled() const { return m_timedMode ? (m_practice && m_practice->editingEnabled()) : (!m_client || m_client->editingEnabled()); }
bool InkCanvas::checkEnabled() const { return m_timedMode ? (m_practice && m_practice->checkEnabled()) : (!m_client || m_client->checkEnabled()); }
bool InkCanvas::complete() const { return m_timedMode ? (m_practice && m_practice->complete()) : (m_client && m_client->complete()); }
QString InkCanvas::progress() const { return m_timedMode ? (m_practice ? m_practice->progressText() : QString()) : (m_client ? m_client->progressText() : QString()); }

void InkCanvas::connectServer(const QUrl &url) {
    if (m_client) return;
    m_client = new ExerciseClient(url,m_dbPath,this);
    connect(m_client,&ExerciseClient::exerciseChanged,this,[this] {
        m_strokes.clear();
        update();
    });
    connect(m_client,&ExerciseClient::changed,this,[this] {
        setFeedback(m_client->feedback());
        setError(m_client->error());
        emit serverStateChanged();
    });
    m_client->start();
    // A failed submission is restored exactly, ready for an idempotent retry.
    m_strokes.clear();
    for (const auto &stroke : m_client->pendingStrokes()) {
        QVector<InkPoint> points;
        for (const auto &entry : stroke.toArray()) {
            const auto p = entry.toObject();
            points.append({QPointF(p["x"].toDouble(),p["y"].toDouble()),p["pressure"].toDouble(),qint64(p["ms"].toDouble())});
        }
        if (!points.isEmpty()) m_strokes.append(points);
    }
    update();
}
bool InkCanvas::canStart() const {
    return m_practice && !m_practice->busy()
        && (m_practice->phase() == "stopped" || m_practice->phase() == "complete");
}
QString InkCanvas::timerText() const { return m_practice ? m_practice->timerText() : QString(); }
void InkCanvas::startPractice() { if (canStart()) m_practice->startPractice(); }
void InkCanvas::stopPractice() { if (m_practice) m_practice->stopPractice(); }
void InkCanvas::exitPractice() {
    stopPractice();
    // Give the asynchronous Stop a bounded opportunity to reach the server.
    // If offline, server liveness expiry resets the unfinished block.
    QTimer::singleShot(750, QCoreApplication::instance(), &QCoreApplication::quit);
}
void InkCanvas::connectPractice(const QUrl &url, const QString &token) {
    if (m_practice || m_client) return;
    m_timedMode = true;
    emit serverStateChanged();
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty()
        || !url.userInfo().isEmpty() || token.trimmed().isEmpty()) {
        setError("Practice needs a secure server address and device credential.");
        return;
    }
    m_practice = new PracticeClient(url, m_dbPath, token, this);
    connect(m_practice, &PracticeClient::exerciseChanged, this, [this] {
        m_strokes.clear();
        update();
    });
    connect(m_practice, &PracticeClient::changed, this, [this] {
        setFeedback(m_practice->feedback());
        setError(m_practice->error());
        emit serverStateChanged();
    });
    m_practice->start();
    m_strokes.clear();
    for (const auto &stroke : m_practice->pendingStrokes()) {
        QVector<InkPoint> points;
        for (const auto &entry : stroke.toArray()) {
            const auto point = entry.toObject();
            points.append({QPointF(point["x"].toDouble(), point["y"].toDouble()),
                point["pressure"].toDouble(), qint64(point["ms"].toDouble())});
        }
        if (!points.isEmpty()) m_strokes.append(points);
    }
    update();
}
void InkCanvas::setFeedback(const QString &s) { if (m_feedback != s) { m_feedback = s; emit feedbackChanged(); } }
void InkCanvas::setError(const QString &s) { if (m_error != s) { m_error = s; emit errorChanged(); } }

void InkCanvas::mousePressEvent(QMouseEvent *event) { beginStroke(event->position(), 1, QDateTime::currentMSecsSinceEpoch()); event->accept(); }
void InkCanvas::mouseMoveEvent(QMouseEvent *event) { appendPoint(event->position(), 1, QDateTime::currentMSecsSinceEpoch()); event->accept(); }
void InkCanvas::mouseReleaseEvent(QMouseEvent *event) { endStroke(); event->accept(); }
