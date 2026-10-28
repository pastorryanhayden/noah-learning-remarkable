#pragma once
#include <QObject>
#include <QPointF>

class InkCanvas;
class QSocketNotifier;

class PenInput : public QObject {
    Q_OBJECT
public:
    explicit PenInput(InkCanvas *canvas, QObject *parent = nullptr);
    ~PenInput() override;
    bool start();
    void setRanges(int minX, int maxX, int minY, int maxY);
    void processEvent(unsigned short type, unsigned short code, int value, qint64 ms);
private:
    void readEvents();
    QPointF mappedPoint() const;
    InkCanvas *m_canvas;
    QSocketNotifier *m_notifier = nullptr;
    int m_fd = -1;
    int m_minX = 0, m_maxX = 1, m_minY = 0, m_maxY = 1;
    int m_x = 0, m_y = 0, m_pressure = 0, m_maxPressure = 4096;
    bool m_down = false, m_wasDown = false;
    bool m_capturing = false;
    qint64 m_ms = 0;
};
