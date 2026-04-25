#include "TrendWidget.h"
#include "qcustomplot.h"
#include "logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDateTime>
#include <QPen>
#include <QBrush>
#include <QSharedPointer>
#include <cmath>

TrendWidget::TrendWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- 顶部工具栏 ---
    auto* toolbar = new QHBoxLayout;

    m_btn1h = new QPushButton(QStringLiteral("1小时"), this);
    m_btn8h = new QPushButton(QStringLiteral("8小时"), this);
    m_btn24h = new QPushButton(QStringLiteral("24小时"), this);
    m_btnClear = new QPushButton(QStringLiteral("清除"), this);

    m_btn1h->setCheckable(true);
    m_btn8h->setCheckable(true);
    m_btn24h->setCheckable(true);
    m_btn1h->setChecked(true);

    toolbar->addWidget(m_btn1h);
    toolbar->addWidget(m_btn8h);
    toolbar->addWidget(m_btn24h);
    toolbar->addStretch();
    toolbar->addWidget(m_btnClear);

    mainLayout->addLayout(toolbar);

    // --- QCustomPlot ---
    m_plot = new QCustomPlot(this);
    setupPlot();
    mainLayout->addWidget(m_plot, 1);

    // --- 连接 ---
    connect(m_btn1h, &QPushButton::clicked, this, &TrendWidget::onTimeRange1h);
    connect(m_btn8h, &QPushButton::clicked, this, &TrendWidget::onTimeRange8h);
    connect(m_btn24h, &QPushButton::clicked, this, &TrendWidget::onTimeRange24h);
    connect(m_btnClear, &QPushButton::clicked, this, &TrendWidget::clearAll);
}

TrendWidget::~TrendWidget() = default;

void TrendWidget::setupPlot()
{
    m_plot->setBackground(QColor(20, 25, 30));
    m_plot->axisRect()->setBackground(QColor(25, 30, 35));

    // X轴：时间
    auto* xAxis = m_plot->xAxis;
    xAxis->setLabelColor(QColor(180, 180, 180));
    xAxis->setTickLabelColor(QColor(180, 180, 180));
    xAxis->setBasePen(QPen(QColor(100, 100, 100), 1));
    xAxis->setTickPen(QPen(QColor(100, 100, 100), 1));
    xAxis->setSubTickPen(QPen(QColor(80, 80, 80), 1));
    xAxis->setLabel("时间");
    xAxis->setTickLabelRotation(30);

    // Y轴：值
    auto* yAxis = m_plot->yAxis;
    yAxis->setLabelColor(QColor(180, 180, 180));
    yAxis->setTickLabelColor(QColor(180, 180, 180));
    yAxis->setBasePen(QPen(QColor(100, 100, 100), 1));
    yAxis->setTickPen(QPen(QColor(100, 100, 100), 1));
    yAxis->setSubTickPen(QPen(QColor(80, 80, 80), 1));
    yAxis->setLabel(QStringLiteral("数值"));

    // 网格
    m_plot->xAxis->grid()->setPen(QPen(QColor(60, 60, 60), 0.5));
    m_plot->yAxis->grid()->setPen(QPen(QColor(60, 60, 60), 0.5));
    m_plot->xAxis->grid()->setSubGridPen(QPen(QColor(40, 40, 40), 0.5));
    m_plot->yAxis->grid()->setSubGridPen(QPen(QColor(40, 40, 40), 0.5));
    m_plot->xAxis->grid()->setSubGridVisible(true);
    m_plot->yAxis->grid()->setSubGridVisible(true);

    // 日期格式 — 使用 QCPAxisTickerDateTime
    m_dateTicker.reset(new QCPAxisTickerDateTime);
    m_dateTicker->setDateTimeFormat("HH:mm:ss");
    m_dateTicker->setDateTimeSpec(Qt::LocalTime);
    m_plot->xAxis->setTicker(m_dateTicker);

    // 图例
    m_plot->legend->setVisible(true);
    m_plot->legend->setBrush(QBrush(QColor(30, 35, 40)));
    m_plot->legend->setTextColor(QColor(200, 200, 200));
    m_plot->legend->setBorderPen(QPen(QColor(80, 80, 80)));
}

QColor TrendWidget::tagColor(int index) const
{
    static const QColor colors[] = {
        QColor(88, 166, 255),   // 蓝色
        QColor(255, 180, 100),  // 橙色
        QColor(80, 200, 120),   // 绿色
        QColor(255, 100, 100),  // 红色
        QColor(180, 140, 255),  // 紫色
        QColor(255, 220, 80),   // 黄色
        QColor(100, 200, 200),  // 青色
        QColor(255, 150, 200),  // 粉色
    };
    return colors[index % 8];
}

void TrendWidget::appendData(quint32 tagId, const QString& tagName, double value)
{
    auto it = m_series.find(tagId);
    if (it == m_series.end()) {
        // 新的位号，创建趋势线
        TrendSeries series;
        series.tagName = tagName;
        int colorIdx = m_series.size();
        series.graph = m_plot->addGraph();
        series.graph->setPen(QPen(tagColor(colorIdx), 1.5));
        series.graph->setName(tagName);
        it = m_series.insert(tagId, series);
    }

    double now = QDateTime::currentSecsSinceEpoch();
    it->timestamps.append(now);
    it->values.append(value);

    // 限制数据点数量
    while (it->timestamps.size() > MAX_POINTS) {
        it->timestamps.removeFirst();
        it->values.removeFirst();
    }

    // 延迟重绘，每200ms只重绘一次
    it->graph->setData(it->timestamps, it->values);
    it->graph->rescaleValueAxis(false);
}

void TrendWidget::clearAll()
{
    for (auto& series : m_series) {
        series.timestamps.clear();
        series.values.clear();
        series.graph->data()->clear();
    }
    if (m_plot) {
        m_plot->rescaleAxes();
        m_plot->replot();
    }
}

void TrendWidget::replot()
{
    if (!m_plot) return;

    double now = QDateTime::currentSecsSinceEpoch();
    double rangeStart = now - m_timeRangeSec;

    m_plot->xAxis->setRange(rangeStart, now);

    // 只显示时间段内的数据
    bool hasData = false;
    double yMin = 1e18, yMax = -1e18;
    for (auto& series : m_series) {
        auto* graph = series.graph;
        if (!graph || graph->data()->isEmpty()) continue;

        // 裁剪到时间范围内
        QVector<double> x, y;
        for (int i = 0; i < series.timestamps.size(); ++i) {
            if (series.timestamps[i] >= rangeStart) {
                x.append(series.timestamps[i]);
                y.append(series.values[i]);
                yMin = qMin(yMin, series.values[i]);
                yMax = qMax(yMax, series.values[i]);
            }
        }
        graph->setData(x, y);
        if (!x.isEmpty()) hasData = true;
    }

    if (hasData) {
        double padding = (yMax - yMin) * 0.1;
        if (padding < 0.01) padding = 1.0;
        m_plot->yAxis->setRange(yMin - padding, yMax + padding);
    } else {
        m_plot->yAxis->setRange(-10, 110);
    }

    // 时间轴格式自适应
    if (m_dateTicker) {
        if (m_timeRangeSec <= 3600) {
            m_dateTicker->setDateTimeFormat("HH:mm:ss");
        } else if (m_timeRangeSec <= 28800) {
            m_dateTicker->setDateTimeFormat("HH:mm");
        } else {
            m_dateTicker->setDateTimeFormat("MM-dd HH:mm");
        }
    }

    m_plot->replot();
}

void TrendWidget::onTimeRange1h()
{
    m_timeRangeSec = 3600;
    m_btn1h->setChecked(true);
    m_btn8h->setChecked(false);
    m_btn24h->setChecked(false);
    replot();
}

void TrendWidget::onTimeRange8h()
{
    m_timeRangeSec = 28800;
    m_btn1h->setChecked(false);
    m_btn8h->setChecked(true);
    m_btn24h->setChecked(false);
    replot();
}

void TrendWidget::onTimeRange24h()
{
    m_timeRangeSec = 86400;
    m_btn1h->setChecked(false);
    m_btn8h->setChecked(false);
    m_btn24h->setChecked(true);
    replot();
}
//第二优先（2 - 3个月）：
//├ QCustomPlot → Qt Charts / 或保留但补历史查询
//├ OPC UA 集成（用open62541或免费SDK）
//├ 数据库迁移 + DataBackupManager实现
//├ 报警风暴抑制（deadband已有框架，补rate limit）
//└ 用户管理UI（改密、增删用户、LDAP）
//
//第三优先（3 - 6个月）：
//├ 双机热备架构设计
//├ ISA - 101 HMI改造 + 多语言
//├ CI / CD 流水线
//├ 安装包（WiX / InnoSetup）
//└ 工程态界面（PID面板、配置导入导出）
