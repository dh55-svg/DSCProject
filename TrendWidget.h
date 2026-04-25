#pragma once
#include "core_global.h"
#include "TagDef.h"
#include "DoubleBuffer.h"
#include <QWidget>
#include <QHash>
#include <QVector>
#include <QPair>
#include <QElapsedTimer>

class QCustomPlot;
class QCPGraph;
class QCPAxisTickerDateTime;
class QPushButton;
class QComboBox;
class QListWidget;

class CORE_EXPORT TrendWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrendWidget(QWidget* parent = nullptr);
    ~TrendWidget() override;

    /// 追加一个数据点（由主窗口刷新定时器调用）
    void appendData(quint32 tagId, const QString& tagName, double value);

    /// 清除所有历史数据
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

    // 趋势数据存储：tagId -> (timestamp_sec, value)
    struct TrendSeries {
        QString tagName;
        QVector<double> timestamps;  // seconds since epoch
        QVector<double> values;
        QCPGraph* graph = nullptr;
    };
    QHash<quint32, TrendSeries> m_series;

    int m_timeRangeSec = 3600;      // 1h default
    static constexpr int MAX_POINTS = 20000;
};
