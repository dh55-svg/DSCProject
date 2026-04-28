#pragma once
#include <QWidget>
#include <QHash>
#include <QVector>
#include <QElapsedTimer>
#include <QSharedPointer>

class QCustomPlot;
class QCPGraph;
class QCPAxisTickerDateTime;
class QPushButton;
class QComboBox;

class TrendWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrendWidget(QWidget* parent = nullptr);
    ~TrendWidget() override;

    void appendData(quint32 tagId, const QString& tagName, double value);
    void clearAll();

private slots:
    void onTimeRange1h();
    void onTimeRange8h();
    void onTimeRange24h();

private:
    void replot();
    void setupPlot();
    QColor tagColor(int index) const;

    QCustomPlot* m_plot = nullptr;
    QSharedPointer<QCPAxisTickerDateTime> m_dateTicker;
    QPushButton* m_btn1h = nullptr;
    QPushButton* m_btn8h = nullptr;
    QPushButton* m_btn24h = nullptr;
    QPushButton* m_btnClear = nullptr;

    struct TrendSeries {
        QString tagName;
        QVector<double> timestamps;
        QVector<double> values;
        QCPGraph* graph = nullptr;
    };
    QHash<quint32, TrendSeries> m_series;

    int m_timeRangeSec = 3600;
    static constexpr int MAX_POINTS = 20000;
};
