#include "TrendWidget.h"
#include "3pair/qcustomplot/qcustomplot.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDateTime>

TrendWidget::TrendWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* btnLayout = new QHBoxLayout();
    m_btn1h = new QPushButton("1H", this);
    m_btn8h = new QPushButton("8H", this);
    m_btn24h = new QPushButton("24H", this);
    m_btnClear = new QPushButton("Clear", this);

    for (auto* btn : {m_btn1h, m_btn8h, m_btn24h, m_btnClear}) {
        btn->setFixedWidth(50);
        btn->setStyleSheet("QPushButton { background-color: #16213e; color: #e0e0e0; padding: 4px; }"
                           "QPushButton:hover { background-color: #0f3460; }");
        btnLayout->addWidget(btn);
    }
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    m_plot = new QCustomPlot(this);
    layout->addWidget(m_plot);
    setupPlot();

    connect(m_btn1h, &QPushButton::clicked, this, &TrendWidget::onTimeRange1h);
    connect(m_btn8h, &QPushButton::clicked, this, &TrendWidget::onTimeRange8h);
    connect(m_btn24h, &QPushButton::clicked, this, &TrendWidget::onTimeRange24h);
    connect(m_btnClear, &QPushButton::clicked, this, &TrendWidget::clearAll);
}

TrendWidget::~TrendWidget() {}

void TrendWidget::setupPlot() {
    m_plot->setBackground(QColor(26, 26, 46));
    m_plot->xAxis->setBasePen(QPen(QColor(100, 100, 140)));
    m_plot->xAxis->setTickPen(QPen(QColor(100, 100, 140)));
    m_plot->xAxis->setSubTickPen(QPen(QColor(60, 60, 80)));
    m_plot->xAxis->setTickLabelColor(QColor(180, 180, 200));
    m_plot->xAxis->grid()->setPen(QPen(QColor(40, 40, 60), 1, Qt::DotLine));
    m_plot->xAxis->grid()->setSubGridPen(QPen(QColor(30, 30, 50), 1, Qt::DotLine));

    m_plot->yAxis->setBasePen(QPen(QColor(100, 100, 140)));
    m_plot->yAxis->setTickPen(QPen(QColor(100, 100, 140)));
    m_plot->yAxis->setSubTickPen(QPen(QColor(60, 60, 80)));
    m_plot->yAxis->setTickLabelColor(QColor(180, 180, 200));
    m_plot->yAxis->grid()->setPen(QPen(QColor(40, 40, 60), 1, Qt::DotLine));

    m_dateTicker.reset(new QCPAxisTickerDateTime());
    m_dateTicker->setDateTimeFormat("HH:mm:ss");
    m_plot->xAxis->setTicker(m_dateTicker);

    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->replot();
}

void TrendWidget::appendData(quint32 tagId, const QString& tagName, double value) {
    double now = QDateTime::currentSecsSinceEpoch();

    if (!m_series.contains(tagId)) {
        TrendSeries ts;
        ts.tagName = tagName;
        ts.graph = m_plot->addGraph();
        ts.graph->setPen(QPen(tagColor(m_series.size())));
        ts.graph->setName(tagName);
        m_series[tagId] = ts;
    }

    auto& ts = m_series[tagId];
    ts.timestamps.append(now);
    ts.values.append(value);

    while (ts.timestamps.size() > MAX_POINTS) {
        ts.timestamps.removeFirst();
        ts.values.removeFirst();
    }

    double cutoff = now - m_timeRangeSec;
    ts.graph->setData(ts.timestamps, ts.values, true);
    ts.graph->data()->removeBefore(cutoff);

    replot();
}

void TrendWidget::clearAll() {
    m_plot->clearGraphs();
    m_series.clear();
    m_plot->replot();
}

void TrendWidget::replot() {
    double now = QDateTime::currentSecsSinceEpoch();
    m_plot->xAxis->setRange(now - m_timeRangeSec, now);
    m_plot->yAxis->rescale();
    m_plot->replot();
}

void TrendWidget::onTimeRange1h() {
    m_timeRangeSec = 3600;
    m_dateTicker->setDateTimeFormat("HH:mm:ss");
    replot();
}

void TrendWidget::onTimeRange8h() {
    m_timeRangeSec = 28800;
    m_dateTicker->setDateTimeFormat("HH:mm");
    replot();
}

void TrendWidget::onTimeRange24h() {
    m_timeRangeSec = 86400;
    m_dateTicker->setDateTimeFormat("MM-dd HH:mm");
    replot();
}

QColor TrendWidget::tagColor(int index) const {
    static const QColor palette[] = {
        QColor(60, 180, 75), QColor(255, 100, 100), QColor(100, 180, 255),
        QColor(255, 200, 50), QColor(200, 100, 255), QColor(50, 220, 200),
        QColor(255, 150, 50), QColor(150, 200, 50)
    };
    return palette[index % 8];
}
