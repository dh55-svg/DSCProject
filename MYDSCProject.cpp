#include "MYDSCProject.h"
#include "logger.h"
#include "DatabaseManager.h"
#include "HistoryArchiveThread.h"
#include "ConfigManager.h"
#include "TagConfigMgr.h"
#include "DoubleBuffer.h"

#include <QApplication>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QTextDocument>
#include <QPrinter>
#include <QPrintPreviewDialog>
#include <QDateTime>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextCursor>
#include <QTextDocument>
#include <QPushButton>
#include <QMenu>
#include <QtMath>
#include "TagConfigDialog.h"
// ============================================================
// 构造/析构
// ============================================================
MYDSCProject::MYDSCProject(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
    setWindowTitle(QStringLiteral("MY DCS - 分布式控制系统 v1.0"));
    resize(1400, 900);

    // 初始化各引擎
    initEngines();

    // 构建UI
    setupMenuBar();
    setupToolBar();
    setupCentralWidget();
    setupDockWidgets();
    setupStatusBar();
    setupConnections();

    // 通过统一配置管理器加载JSON配置（场景创建完成后）
    ConfigManager::instance().setBasePath("./config");
    ConfigManager::instance().initialize(m_dataEngine, m_pidScene);

    // 启动刷新定时器（100ms刷新一次数据和报警）
    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MYDSCProject::onRefreshTimer);
    m_refreshTimer->start(200);

    // ISA-18.2: KPI仪表盘定时刷新（每30秒）
    m_kpiRefreshTimer = new QTimer(this);
    connect(m_kpiRefreshTimer, &QTimer::timeout, this, &MYDSCProject::onRefreshKpiDashboard);
    m_kpiRefreshTimer->start(30000);

    LOG_INFO("MainWindow", "DCS主窗口初始化完成");
}

MYDSCProject::~MYDSCProject()
{
    if (m_dataEngine) {
        m_dataEngine->stop();
    }
    LOG_INFO("MainWindow", "DCS主窗口已销毁");
}

// ============================================================
// 引擎初始化
// ============================================================
void MYDSCProject::initEngines()
{
    // 初始化性能监视器
    PerformanceMonitor::instance();

    // 初始化日志
    Logger::instance().setLogDir("./logs");
    Logger::instance().setLogLevel(Log_Level::Info);

    // 初始化权限管理
    AuthManager::instance().initialize();

    // 初始化报警引擎
    AlarmEngine::instance().initialize();

    // 初始化数据引擎
    m_dataEngine = new DataEngine(this);
    m_dataEngine->initialize();

    // 初始化数据库（MySQL + SQLite回退）
    DatabaseManager::instance().initializeWithFallback();
    LOG_INFO("MainWindow", QString("数据库后端: %1").arg(DatabaseManager::instance().backendType()));

    // 启动历史归档线程
    HistoryArchiveThread::instance().setDoubleBuffer(m_dataEngine->doubleBuffer());
    HistoryArchiveThread::instance().start();

    LOG_INFO("MainWindow", "所有引擎初始化完成");
}

// ============================================================
// 菜单栏
// ============================================================
void MYDSCProject::setupMenuBar()
{
    // ---- 文件菜单 ----
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(QStringLiteral("打开项目(&O)..."), QKeySequence::Open, this, &MYDSCProject::onOpenProject);
    fileMenu->addAction(QStringLiteral("保存项目(&S)..."), QKeySequence::Save, this, &MYDSCProject::onSaveProject);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("导出数据(&E)..."), this, &MYDSCProject::onExportData);
    fileMenu->addAction(QStringLiteral("生成报告(&R)..."), this, &MYDSCProject::onGenerateReport);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("退出(&Q)"), QKeySequence::Quit, qApp, &QApplication::quit);

    // ---- 视图菜单 ----
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(QStringLiteral("全屏(&F)"), QKeySequence("F11"), this, &MYDSCProject::onToggleFullScreen);
    viewMenu->addAction(QStringLiteral("导航面板"), QKeySequence("Ctrl+1"), this, &MYDSCProject::onToggleLeftDock);
    viewMenu->addAction(QStringLiteral("报警面板"), QKeySequence("Ctrl+2"), this, &MYDSCProject::onToggleRightDock);

    // ---- 工具菜单 ----
    QMenu* toolMenu = menuBar()->addMenu(QStringLiteral("工具(&T)"));
    toolMenu->addAction(QStringLiteral("连接所有设备"), QKeySequence("Ctrl+L"), this, &MYDSCProject::onConnectAll);
    toolMenu->addAction(QStringLiteral("断开所有设备"), QKeySequence("Ctrl+D"), this, &MYDSCProject::onDisconnectAll);
    toolMenu->addSeparator();
    m_actAckAll = toolMenu->addAction(QStringLiteral("确认全部报警"), QKeySequence("Ctrl+K"), this, &MYDSCProject::onAcknowledgeAllAlarms);
    toolMenu->addSeparator();
    toolMenu->addAction(QStringLiteral("性能监视器"), this, &MYDSCProject::onShowPerformanceMonitor);
    toolMenu->addSeparator();
    toolMenu->addAction(QStringLiteral("位号配置编辑器..."), this, &MYDSCProject::onShowTagConfig);

    // ---- 帮助菜单 ----
    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(QStringLiteral("关于(&A)..."), this, &MYDSCProject::onAbout);
}

// ============================================================
// 工具栏
// ============================================================
void MYDSCProject::setupToolBar()
{
    QToolBar* toolbar = addToolBar(QStringLiteral("主工具栏"));
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* style = qApp->style();

    m_actConnect = toolbar->addAction(
        style->standardIcon(QStyle::SP_ComputerIcon),
        QStringLiteral(" 连接 "), this, &MYDSCProject::onConnectAll);
    m_actDisconnect = toolbar->addAction(
        style->standardIcon(QStyle::SP_DriveNetIcon),
        QStringLiteral(" 断开 "), this, &MYDSCProject::onDisconnectAll);
    toolbar->addSeparator();
    m_actAckAll->setIcon(style->standardIcon(QStyle::SP_DialogApplyButton));
    toolbar->addAction(m_actAckAll);
    m_actSound = toolbar->addAction(
        style->standardIcon(QStyle::SP_DialogCloseButton),
        QStringLiteral(" 静音 "), this, &MYDSCProject::onToggleSound);
    toolbar->addSeparator();
    m_actLogin = toolbar->addAction(
        style->standardIcon(QStyle::SP_FileDialogDetailedView),
        QStringLiteral(" 登录 "), this, &MYDSCProject::onLoginAction);
    toolbar->addSeparator();
    m_actDemo = toolbar->addAction(
        style->standardIcon(QStyle::SP_MediaPlay),
        QStringLiteral(" ▶ 演示 "), this, &MYDSCProject::onToggleDemo);
}

// ============================================================
// 中央区域: P&ID + 数据表 分页
// ============================================================
void MYDSCProject::setupCentralWidget()
{
    m_centralTabs = new QTabWidget(this);
    m_centralTabs->setDocumentMode(true);

    // ---- Tab1: P&ID 工艺流程图（带缩放控制） ----
    m_pidScene = new PidScene(this);
    m_pidView = new QGraphicsView(m_pidScene, this);
    m_pidView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_pidView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_pidView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_pidView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    m_pidView->setBackgroundBrush(QColor(25, 30, 35));
    m_pidView->setContextMenuPolicy(Qt::CustomContextMenu);

    // 缩放控制条
    m_pidContainer = new QWidget(this);
    auto* pidLayout = new QVBoxLayout(m_pidContainer);
    pidLayout->setContentsMargins(0, 0, 0, 0);
    pidLayout->setSpacing(0);

    auto* zoomBar = new QToolBar(QStringLiteral("缩放"), this);
    zoomBar->setIconSize(QSize(16, 16));
    zoomBar->setStyleSheet(
        "QToolBar { background: #2d2d2d; border: none; padding: 2px; spacing: 4px; }"
        "QToolButton { color: #ccc; background: #3d3d3d; border: 1px solid #555;"
        "  border-radius: 3px; padding: 2px 8px; font-size: 12px; }"
        "QToolButton:hover { background: #4d4d4d; }");

    auto* style = qApp->style();

    zoomBar->addAction(
        style->standardIcon(QStyle::SP_ArrowUp),
        QStringLiteral("放大 +"), this, &MYDSCProject::onZoomIn);
    zoomBar->addAction(
        style->standardIcon(QStyle::SP_ArrowDown),
        QStringLiteral("缩小 -"), this, &MYDSCProject::onZoomOut);
    zoomBar->addAction(
        style->standardIcon(QStyle::SP_FileDialogContentsView),
        QStringLiteral("适应"), this, &MYDSCProject::onZoomFit);
    zoomBar->addSeparator();

    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(10, 400);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(150);
    m_zoomSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #555; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #888; width: 14px; margin: -5px 0;"
        "  border-radius: 7px; }"
        "QSlider::sub-page:horizontal { background: #58a6ff; border-radius: 2px; }");
    connect(m_zoomSlider, &QSlider::valueChanged, this, &MYDSCProject::onZoomSliderChanged);
    zoomBar->addWidget(m_zoomSlider);

    m_zoomLabel = new QLabel(" 100% ", this);
    m_zoomLabel->setStyleSheet("color: #ccc; font-size: 12px; padding: 0 6px;");
    zoomBar->addWidget(m_zoomLabel);

    pidLayout->addWidget(zoomBar);
    pidLayout->addWidget(m_pidView, 1);
    m_centralTabs->addTab(m_pidContainer, QStringLiteral("📊 工艺流程图"));

    // ---- Tab2: 数据总表 ----
    QWidget* tablePage = new QWidget(this);
    QVBoxLayout* tableLayout = new QVBoxLayout(tablePage);
    tableLayout->setContentsMargins(0, 0, 0, 0);

    m_dataTable = new QTableWidget(0, 7, tablePage);
    m_dataTable->setHorizontalHeaderLabels({
        QStringLiteral("位号ID"), QStringLiteral("位号名称"), QStringLiteral("描述"),
        QStringLiteral("当前值"), QStringLiteral("单位"), QStringLiteral("质量"),
        QStringLiteral("报警状态")
    });
    m_dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dataTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_dataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dataTable->setAlternatingRowColors(true);
    m_dataTable->horizontalHeader()->setStretchLastSection(true);
    m_dataTable->verticalHeader()->setVisible(false);
    m_dataTable->setColumnWidth(0, 80);
    m_dataTable->setColumnWidth(1, 120);
    m_dataTable->setColumnWidth(2, 180);
    m_dataTable->setColumnWidth(3, 100);
    m_dataTable->setColumnWidth(4, 60);
    m_dataTable->setColumnWidth(5, 80);

    tableLayout->addWidget(m_dataTable);
    m_centralTabs->addTab(tablePage, QStringLiteral("📋 位号数据表"));

    // ---- Tab3: 日志输出 ----
    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet("background-color: #1a1a1a; color: #00ff00; font-family: Consolas, monospace;");
    m_centralTabs->addTab(m_logView, QStringLiteral("📝 系统日志"));

    // ---- Tab4: 趋势曲线 ----
    m_trendWidget = new TrendWidget(this);
    m_centralTabs->addTab(m_trendWidget, QStringLiteral("📈 趋势曲线"));

    // ---- Tab5: ISA-18.2 KPI 仪表盘 ----
    m_kpiPage = createKpiDashboardPage();
    m_centralTabs->addTab(m_kpiPage, QStringLiteral("📊 KPI仪表盘"));

    // ---- Tab6: ISA-18.2 变更日志 ----
    m_changeLogPage = createChangeLogPage();
    m_centralTabs->addTab(m_changeLogPage, QStringLiteral("📋 变更日志"));

    setCentralWidget(m_centralTabs);
}

// ============================================================
// 停靠面板
// ============================================================
void MYDSCProject::setupDockWidgets()
{
    // ---- 左侧：设备导航树 ----
    m_leftDock = new QDockWidget(QStringLiteral("设备导航"), this);
    m_leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_deviceTree = new QTreeWidget(m_leftDock);
    m_deviceTree->setHeaderLabels({ QStringLiteral("设备名称"), QStringLiteral("状态") });
    m_deviceTree->setRootIsDecorated(true);
    m_deviceTree->setAlternatingRowColors(true);
    m_deviceTree->setColumnWidth(0, 150);
    m_deviceTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_leftDock->setWidget(m_deviceTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

    // ---- 右侧：ISA-18.2 报警汇总面板 ----
    m_rightDock = new QDockWidget(QStringLiteral("报警汇总 (ISA-18.2)"), this);
    m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // 报警面板容器（过滤栏 + 报警表格）
    QWidget* alarmPanel = new QWidget(m_rightDock);
    QVBoxLayout* alarmLayout = new QVBoxLayout(alarmPanel);
    alarmLayout->setContentsMargins(0, 0, 0, 0);
    alarmLayout->setSpacing(0);

    // ISA-18.2 报警过滤工具栏
    setupAlarmFilterBar();
    alarmLayout->addWidget(m_alarmFilterBar);

    // ISA-18.2 增强报警表格（10列完整信息）
    m_alarmTable = new QTableWidget(0, 10, alarmPanel);
    m_alarmTable->setHorizontalHeaderLabels({
        QStringLiteral("报警ID"), QStringLiteral("位号"), QStringLiteral("限值"),
        QStringLiteral("优先级"), QStringLiteral("分类"), QStringLiteral("状态"),
        QStringLiteral("当前值"), QStringLiteral("限值"), QStringLiteral("区域"),
        QStringLiteral("触发时间")
    });
    m_alarmTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_alarmTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_alarmTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_alarmTable->setAlternatingRowColors(true);
    m_alarmTable->horizontalHeader()->setStretchLastSection(true);
    m_alarmTable->verticalHeader()->setVisible(false);
    m_alarmTable->setColumnWidth(0, 140);
    m_alarmTable->setColumnWidth(1, 70);
    m_alarmTable->setColumnWidth(2, 50);
    m_alarmTable->setColumnWidth(3, 55);
    m_alarmTable->setColumnWidth(4, 55);
    m_alarmTable->setColumnWidth(5, 65);
    m_alarmTable->setColumnWidth(6, 60);
    m_alarmTable->setColumnWidth(7, 60);
    m_alarmTable->setColumnWidth(8, 65);
    m_alarmTable->setContextMenuPolicy(Qt::CustomContextMenu);
    alarmLayout->addWidget(m_alarmTable);

    m_rightDock->setWidget(alarmPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);
}

// ============================================================
// 状态栏
// ============================================================
void MYDSCProject::setupStatusBar()
{
    m_alarmBar = new QLabel(QStringLiteral(" 系统正常 "), this);
    m_alarmBar->setFrameStyle(QFrame::StyledPanel);
    m_alarmBar->setStyleSheet("background-color: #2d2d2d; color: #00cc00; font-weight: bold; padding: 2px 10px;");
    statusBar()->addWidget(m_alarmBar, 1);

    m_statusDevices = new QLabel(QStringLiteral(" 设备: 0/0 "), this);
    statusBar()->addPermanentWidget(m_statusDevices);

    m_statusTags = new QLabel(QStringLiteral(" 位号: 0 "), this);
    statusBar()->addPermanentWidget(m_statusTags);

    m_statusAlarms = new QLabel(QStringLiteral(" 报警: 0 "), this);
    statusBar()->addPermanentWidget(m_statusAlarms);

    m_statusUser = new QLabel(QStringLiteral(" 用户: observer "), this);
    statusBar()->addPermanentWidget(m_statusUser);

    m_statusTime = new QLabel(this);
    m_statusTime->setText(QDateTime::currentDateTime().toString(" yyyy-MM-dd HH:mm:ss "));
    statusBar()->addPermanentWidget(m_statusTime);

    // 时间更新定时器（每秒）
    QTimer* timeTimer = new QTimer(this);
    connect(timeTimer, &QTimer::timeout, this, [this]() {
        m_statusTime->setText(QDateTime::currentDateTime().toString(" yyyy-MM-dd HH:mm:ss "));
    });
    timeTimer->start(1000);
}

// ============================================================
// 信号连接
// ============================================================
void MYDSCProject::setupConnections()
{
    // 数据引擎信号
    if (m_dataEngine) {
        connect(m_dataEngine, &DataEngine::dataUpdated,
            this, &MYDSCProject::onDataUpdated);
        connect(m_dataEngine, &DataEngine::alarmTriggered,
            this, &MYDSCProject::onAlarmTriggered);
        connect(m_dataEngine, &DataEngine::alarmCleared,
            this, &MYDSCProject::onAlarmCleared);
        connect(m_dataEngine, &DataEngine::deviceStatusChanged,
            this, &MYDSCProject::onDeviceStatusChanged);
        connect(m_dataEngine, &DataEngine::commStatusChanged,
            this, &MYDSCProject::onCommStatusChanged);
    }

    // 报警引擎信号
    connect(&AlarmEngine::instance(), &AlarmEngine::alarmCountChanged,
        this, &MYDSCProject::onAlarmCountChanged);

    // 认证信号
    connect(&AuthManager::instance(), &AuthManager::userLoggedIn,
        this, &MYDSCProject::onUserLoggedIn);
    connect(&AuthManager::instance(), &AuthManager::userLoggedOut,
        this, &MYDSCProject::onUserLoggedOut);

    // 报警表格双击 -> 确认报警
    connect(m_alarmTable, &QTableWidget::doubleClicked,
        this, &MYDSCProject::onAcknowledgeAlarm);

    // 上下文菜单
    connect(m_alarmTable, &QTableWidget::customContextMenuRequested,
        this, &MYDSCProject::onAlarmTableContextMenu);
    connect(m_deviceTree, &QTreeWidget::customContextMenuRequested,
        this, &MYDSCProject::onDeviceTreeContextMenu);
    connect(m_pidView, &QGraphicsView::customContextMenuRequested,
        this, &MYDSCProject::onPidViewContextMenu);

    // 日志回调：将Logger输出重定向到UI日志面板
    Logger::setLogCallback([this](Log_Level level, const QString& model, const QString& message) {
        QMetaObject::invokeMethod(this, [this, level, model, message]() {
            appendLog(level, model, message);
        }, Qt::QueuedConnection);
    });
}

// ============================================================
// 定时刷新
// ============================================================
void MYDSCProject::onRefreshTimer()
{
    updateAlarmBar();
    updateStatusBar();
    updateDataTable();
    updateDeviceTree();
    if (m_demoMode) {
        updatePidItems();
    }

    // ISA-18.2: 定期刷新报警汇总（过滤后的活跃报警列表）
    static int refreshCounter = 0;
    if (++refreshCounter >= 5) {
        refreshCounter = 0;
        refreshAlarmSummary();
    }
}

// ============================================================
// 报警条更新
// ============================================================
void MYDSCProject::updateAlarmBar()
{
    int activeCount = AlarmEngine::instance().activeAlarmCount();
    if (activeCount > 0) {
        auto alarms = AlarmEngine::instance().activeAlarms();
        QString text;
        int shown = 0;
        for (const auto& alarm : alarms) {
            if (shown >= 2) {
                text += QStringLiteral(" ... 还有 %1 条报警 ").arg(activeCount - 2);
                break;
            }
            QString severityStr;
            QString color;
            switch (alarm.limit) {
            case AlarmLimit::HighHigh: severityStr = "HH"; color = "#ff0000"; break;
            case AlarmLimit::High:     severityStr = "H";  color = "#ff6600"; break;
            case AlarmLimit::LowLow:   severityStr = "LL"; color = "#ff0000"; break;
            case AlarmLimit::Low:      severityStr = "L";  color = "#ff6600"; break;
            default:                   severityStr = "?";  color = "#ffffff"; break;
            }
            text += QStringLiteral("<span style='color:%1; font-weight:bold;'> [%2] %3 %4</span>")
                .arg(color).arg(severityStr).arg(alarm.tagName).arg(alarm.description);
            shown++;
        }
        m_alarmBar->setTextFormat(Qt::RichText);
        m_alarmBar->setText(text);
        m_alarmBar->setStyleSheet("background-color: #3a0000; color: #ff4444; font-weight: bold; padding: 2px 10px;");
    }
    else {
        m_alarmBar->setText(QStringLiteral(" 系统正常 "));
        m_alarmBar->setStyleSheet("background-color: #002a00; color: #00cc00; font-weight: bold; padding: 2px 10px;");
    }
}

// ============================================================
// 状态栏更新
// ============================================================
void MYDSCProject::updateStatusBar()
{
    m_statusAlarms->setText(QStringLiteral(" 报警: %1 ").arg(m_totalAlarms));

    if (m_dataEngine) {
        int totalTags = TagConfigMgr::instance().tagCount();
        m_statusTags->setText(QStringLiteral(" 位号: %1 ").arg(totalTags));
    }

    // 检查用户级别并切换颜色
    if (AuthManager::instance().isLoggedIn()) {
        m_statusUser->setStyleSheet("color: #00cc00;");
    }
    else {
        m_statusUser->setStyleSheet("color: #ff6600;");
    }
}

// ============================================================
// 设备树更新
// ============================================================
void MYDSCProject::rebuildDeviceTree()
{
    if (!m_deviceTree) return;
    m_deviceTree->clear();

    if (!m_demoMode && (!m_dataEngine || m_dataEngine->modbusManager()->totalDeviceCount() == 0)) {
        // 未连接设备，显示提示
        auto* tip = new QTreeWidgetItem(m_deviceTree);
        tip->setText(0, QStringLiteral("未连接设备"));
        tip->setForeground(0, QColor(128, 128, 128));
        return;
    }

    // 按modbusServerAddr分组显示设备
    QMap<int, QVector<quint32>> devices;
    for (const auto& tag : TagConfigMgr::instance().getAllTags()) {
        devices[tag.modbusServerAddr].append(tag.tagId);
    }

    QStringList devNames = {
        QStringLiteral("1号反应釜工段"),
        QStringLiteral("2号反应釜工段")
    };

    int devIdx = 0;
    for (auto it = devices.begin(); it != devices.end(); ++it, ++devIdx) {
        auto* devItem = new QTreeWidgetItem(m_deviceTree);
        QString name = (devIdx < devNames.size())
            ? devNames[devIdx]
            : QStringLiteral("设备 %1 (Server: %2)").arg(devIdx + 1).arg(it.key());
        devItem->setText(0, name);
        // 先设为离线，后续由 updateDeviceValues 更新状态
        devItem->setText(1, QStringLiteral("--"));
        devItem->setData(0, Qt::UserRole, it.key());
        devItem->setForeground(1, QColor(128, 128, 128));

        for (quint32 tagId : it.value()) {
            auto tag = TagConfigMgr::instance().getTag(tagId);
            auto* child = new QTreeWidgetItem(devItem);
            child->setText(0, tag.tagName);
            child->setText(1, "--");
            child->setData(0, Qt::UserRole, tagId);
            child->setForeground(1, QColor(128, 128, 128));
        }
        devItem->setExpanded(true);
    }
}

void MYDSCProject::updateDeviceValues()
{
    if (!m_deviceTree || m_deviceTree->topLevelItemCount() == 0) return;

    bool online = m_demoMode;
    int onlineCount = 0, totalCount = 0;

    for (int i = 0; i < m_deviceTree->topLevelItemCount(); ++i) {
        auto* devItem = m_deviceTree->topLevelItem(i);
        int serverAddr = devItem->data(0, Qt::UserRole).toInt();

        // 检查设备在线状态
        bool devOnline = m_demoMode;
        if (!m_demoMode && m_dataEngine) {
            auto* mm = m_dataEngine->modbusManager();
            devOnline = mm->isDeviceConnected(serverAddr);
        }

        devItem->setText(1, devOnline ? QStringLiteral("在线") : QStringLiteral("离线"));
        devItem->setForeground(1, devOnline ? QColor(0, 200, 0) : QColor(255, 0, 0));

        if (devOnline) onlineCount++;
        totalCount++;

        // 更新子节点（位号）的实时值
        for (int j = 0; j < devItem->childCount(); ++j) {
            auto* child = devItem->child(j);
            quint32 tagId = child->data(0, Qt::UserRole).toUInt();
            if (tagId == 0) continue;

            auto snap = m_dataEngine ? m_dataEngine->doubleBuffer()->readTag(tagId) : DoubleBuffer::TagSnapshot{};
            if (snap.tagId != 0) {
                child->setText(1, QString::number(snap.currentValue, 'f', 1));
                child->setForeground(1, QColor(0, 200, 0));
            }
        }
    }

    // 更新状态栏
    if (m_demoMode) {
        m_statusDevices->setText(QStringLiteral(" 设备: 演示 "));
    } else {
        m_statusDevices->setText(QStringLiteral(" 设备: %1/%2 ").arg(onlineCount).arg(totalCount));
    }
}

void MYDSCProject::updateDeviceTree()
{
    // 延迟初始化：首次调用时重建
    if (m_deviceTree->topLevelItemCount() == 0) {
        rebuildDeviceTree();
    }
    // 更新设备状态和位号值
    updateDeviceValues();
}

// ============================================================
// 数据更新槽
// ============================================================
void MYDSCProject::onDataUpdated()
{
    // UI数据更新由DoubleBuffer驱动，这里可以用作触发重绘
    if (m_pidScene) {
        m_pidScene->update();
    }
}

void MYDSCProject::onAlarmTriggered(quint32 tagId, AlarmLimit state, float value, float limit)
{
    m_totalAlarms = AlarmEngine::instance().activeAlarmCount();

    // 查询完整的报警事件（从AlarmEngine获取ISA-18.2全字段）
    AlarmEvent alarmEvent = AlarmEngine::instance().alarmByTagId(tagId);

    // 添加到报警表格顶部（10列ISA-18.2完整信息）
    m_alarmTable->insertRow(0);

    // 查询位号名称
    QString tagName = QString::number(tagId);
    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    if (tagInfo.tagId != 0) {
        tagName = tagInfo.tagName;
    }

    // 根据报警状态设置行背景色
    QColor bgColor = alarmStateColor(alarmEvent.state);

    // 列0: 报警ID
    auto* idItem = new QTableWidgetItem(alarmEvent.alarmId.isEmpty()
        ? QString::number(tagId) : alarmEvent.alarmId);
    idItem->setData(Qt::UserRole, tagId);
    idItem->setData(Qt::UserRole + 1, alarmEvent.alarmId);
    idItem->setBackground(bgColor);
    m_alarmTable->setItem(0, 0, idItem);

    // 列1: 位号名
    auto* tagItem = new QTableWidgetItem(tagName);
    tagItem->setBackground(bgColor);
    m_alarmTable->setItem(0, 1, tagItem);

    // 列2: 限值类型
    auto* limitItem = new QTableWidgetItem(limitText(alarmEvent.limit));
    limitItem->setBackground(bgColor);
    m_alarmTable->setItem(0, 2, limitItem);

    // 列3: 优先级
    auto* priItem = new QTableWidgetItem(priorityText(alarmEvent.priority));
    priItem->setBackground(bgColor);
    m_alarmTable->setItem(0, 3, priItem);

    // 列4: 分类
    auto* clsItem = new QTableWidgetItem(classificationText(alarmEvent.classification));
    clsItem->setBackground(bgColor);
    m_alarmTable->setItem(0, 4, clsItem);

    // 列5: 状态
    auto* stateItem = new QTableWidgetItem(alarmStateText(alarmEvent.state));
    stateItem->setBackground(bgColor);
    m_alarmTable->setItem(0, 5, stateItem);

    // 列6: 当前值
    m_alarmTable->setItem(0, 6, new QTableWidgetItem(
        QString::number(value, 'f', 1)));

    // 列7: 限值
    m_alarmTable->setItem(0, 7, new QTableWidgetItem(
        QString::number(limit, 'f', 1)));

    // 列8: 区域
    m_alarmTable->setItem(0, 8, new QTableWidgetItem(alarmEvent.area));

    // 列9: 触发时间
    m_alarmTable->setItem(0, 9, new QTableWidgetItem(
        QDateTime::currentDateTime().toString("HH:mm:ss")));

    // 限制表格行数
    while (m_alarmTable->rowCount() > 500) {
        m_alarmTable->removeRow(m_alarmTable->rowCount() - 1);
    }

    LOG_WARN("MainWindow", QString("报警: tagId=%1, 级别=%2, 值=%3")
        .arg(tagId).arg(limitText(alarmEvent.limit)).arg(value));
}

void MYDSCProject::onAlarmCleared(quint32 tagId)
{
    // 在报警表格中标记已恢复（按UserRole存储的tagId匹配）
    for (int row = 0; row < m_alarmTable->rowCount(); ++row) {
        if (m_alarmTable->item(row, 0) && m_alarmTable->item(row, 0)->data(Qt::UserRole).toUInt() == tagId) {
            QColor greenBg(200, 255, 200);
            m_alarmTable->item(row, 0)->setBackground(greenBg);
            m_alarmTable->item(row, 1)->setBackground(greenBg);
            m_alarmTable->item(row, 2)->setBackground(greenBg);
            break;
        }
    }
    m_totalAlarms = AlarmEngine::instance().activeAlarmCount();
}

void MYDSCProject::onAlarmCountChanged(int count)
{
    m_totalAlarms = count;
}

void MYDSCProject::onDeviceStatusChanged(int deviceId, bool connected)
{
    LOG_INFO("MainWindow", QString("设备状态变化: ID=%1, 状态=%2")
        .arg(deviceId).arg(connected ? "在线" : "离线"));

    // 更新设备树
    for (int i = 0; i < m_deviceTree->topLevelItemCount(); ++i) {
        auto* item = m_deviceTree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toInt() == deviceId) {
            item->setText(1, connected ? "在线" : "离线");
            item->setForeground(1, connected ? QColor(0, 200, 0) : QColor(255, 0, 0));
            break;
        }
    }

    int online = m_dataEngine ? m_dataEngine->modbusManager()->onlineDeviceCount() : 0;
    int total = m_dataEngine ? m_dataEngine->modbusManager()->totalDeviceCount() : 0;
    m_statusDevices->setText(QStringLiteral(" 设备: %1/%2 ").arg(online).arg(total));
}

void MYDSCProject::onCommStatusChanged(bool connected)
{
    LOG_INFO("MainWindow", QString("通讯状态变化: %1").arg(connected ? "正常" : "中断"));
}

// ============================================================
// 用户登录/登出
// ============================================================
void MYDSCProject::onUserLoggedIn(const QString& username, UserLevel level)
{
    m_statusUser->setText(QStringLiteral(" 用户: %1 ").arg(username));
    m_actLogin->setText(QStringLiteral(" 注销 "));
}

void MYDSCProject::onUserLoggedOut()
{
    m_statusUser->setText(QStringLiteral(" 用户: (未登录) "));
    m_actLogin->setText(QStringLiteral(" 登录 "));
}

void MYDSCProject::onLoginAction()
{
    if (AuthManager::instance().isLoggedIn()) {
        AuthManager::instance().logout();
        return;
    }

    // 使用QDialog弹出登录窗口
    QDialog loginDialog(this);
    loginDialog.setWindowTitle(QStringLiteral("用户登录"));
    loginDialog.setFixedSize(320, 220);
    loginDialog.setStyleSheet(
        "QDialog { background-color: #2d2d2d; }"
        "QLabel { color: #ccc; font-size: 13px; }"
        "QLineEdit { background: #3d3d3d; color: #fff; border: 1px solid #555;"
        "  border-radius: 3px; padding: 6px 10px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #58a6ff; }"
        "QPushButton { background: #3d3d3d; color: #ccc; border: 1px solid #555;"
        "  border-radius: 3px; padding: 6px 20px; font-size: 13px; }"
        "QPushButton:hover { background: #4d4d4d; }"
        "QPushButton#btnLogin { background: #1f6feb; color: #fff; border: none; }"
        "QPushButton#btnLogin:hover { background: #388bfd; }");

    auto* layout = new QVBoxLayout(&loginDialog);
    layout->setSpacing(10);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* titleLabel = new QLabel(QStringLiteral("MY DCS 系统登录"), &loginDialog);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #58a6ff;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    auto* userCombo = new QComboBox(&loginDialog);
    userCombo->setEditable(true);
    userCombo->setStyleSheet(
        "QComboBox { background: #3d3d3d; color: #fff; border: 1px solid #555;"
        "  border-radius: 3px; padding: 6px 10px; font-size: 13px; }"
        "QComboBox:hover { border-color: #58a6ff; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background: #3d3d3d; color: #fff;"
        "  selection-background-color: #1f6feb; }");
    userCombo->addItems({ "operator", "engineer", "admin", "observer" });
    userCombo->setCurrentText("operator");
    layout->addWidget(userCombo);

    auto* passEdit = new QLineEdit(&loginDialog);
    passEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    passEdit->setEchoMode(QLineEdit::Password);
    layout->addWidget(passEdit);

    auto* errorLabel = new QLabel(&loginDialog);
    errorLabel->setStyleSheet("color: #f85149; font-size: 12px;");
    errorLabel->setVisible(false);
    layout->addWidget(errorLabel);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    auto* btnCancel = new QPushButton(QStringLiteral("取消"), &loginDialog);
    auto* btnLogin = new QPushButton(QStringLiteral("登录"), &loginDialog);
    btnLogin->setObjectName("btnLogin");
    btnLogin->setDefault(true);
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnLogin);
    layout->addLayout(btnLayout);

    connect(btnLogin, &QPushButton::clicked, &loginDialog, [&]() {
        QString username = userCombo->currentText().trimmed();
        QString password = passEdit->text();
        if (username.isEmpty()) {
            errorLabel->setText(QStringLiteral("请输入用户名"));
            errorLabel->setVisible(true);
            return;
        }
        if (password.isEmpty()) {
            errorLabel->setText(QStringLiteral("请输入密码"));
            errorLabel->setVisible(true);
            return;
        }
        if (AuthManager::instance().login(username, password)) {
            loginDialog.accept();
        } else {
            errorLabel->setText(QStringLiteral("用户名或密码错误"));
            errorLabel->setVisible(true);
            passEdit->clear();
            passEdit->setFocus();
        }
    });
    connect(btnCancel, &QPushButton::clicked, &loginDialog, &QDialog::reject);
    connect(passEdit, &QLineEdit::returnPressed, btnLogin, &QPushButton::click);

    loginDialog.exec();
}

// ============================================================
// 上下文菜单
// ============================================================
void MYDSCProject::onAlarmTableContextMenu(const QPoint& pos)
{
    int row = m_alarmTable->rowAt(pos.y());
    if (row < 0) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #2d2d2d; color: #ccc; border: 1px solid #555; padding: 4px; }"
        "QMenu::item { padding: 6px 20px; border-radius: 3px; }"
        "QMenu::item:selected { background: #1f6feb; }");

    quint32 tagId = m_alarmTable->item(row, 0)->data(Qt::UserRole).toUInt();
    if (tagId == 0) tagId = m_alarmTable->item(row, 0)->text().toUInt();
    QString alarmId = m_alarmTable->item(row, 0)->data(Qt::UserRole + 1).toString();

    // 查询当前报警状态
    AlarmEvent alarmEvent = AlarmEngine::instance().alarmByTagId(tagId);

    // === 基础操作 ===
    menu.addAction(QStringLiteral("确认报警"), this, [this, row, tagId]() {
        m_alarmTable->selectRow(row);
        onAcknowledgeAlarm();
    });

    if (alarmEvent.state == AlarmState::ReturnToNormalUnacknowledged) {
        menu.addAction(QStringLiteral("确认恢复"), this, [this, alarmId]() {
            onAcknowledgeReturnToNormal(alarmId);
        });
    }

    menu.addSeparator();

    // === ISA-18.2 操作员注释 ===
    menu.addAction(QStringLiteral("📝 添加注释..."), this, [this, alarmId]() {
        onAnnotateAlarm(alarmId);
    });

    menu.addSeparator();

    // === ISA-18.2 Shelving（操作员临时屏蔽） ===
    if (alarmEvent.isShelved()) {
        menu.addAction(QStringLiteral("🔓 取消屏蔽"), this, [this, tagId]() {
            onUnshelveAlarm(tagId);
        });
    } else if (alarmEvent.isActive() || alarmEvent.state == AlarmState::ReturnToNormalUnacknowledged) {
        menu.addAction(QStringLiteral("🔇 屏蔽报警..."), this, [this, tagId]() {
            onShelveAlarm(tagId);
        });
    }

    // === ISA-18.2 Suppression-by-Design（设计抑制） ===
    if (alarmEvent.isSuppressed()) {
        menu.addAction(QStringLiteral("🔓 取消抑制"), this, [this, tagId]() {
            onUnsuppressAlarm(tagId);
        });
    } else if (AuthManager::instance().hasPermission(UserLevel::Engineer)) {
        menu.addAction(QStringLiteral("⛔ 设计抑制..."), this, [this, tagId]() {
            onSuppressAlarm(tagId);
        });
    }

    // === ISA-18.2 Out-of-Service（设备停用） ===
    if (alarmEvent.state == AlarmState::OutOfService) {
        menu.addAction(QStringLiteral("🔧 恢复服务"), this, [this, tagId]() {
            onReturnToService(tagId);
        });
    } else if (AuthManager::instance().hasPermission(UserLevel::Engineer)) {
        menu.addAction(QStringLiteral("🔧 设备停用..."), this, [this, tagId]() {
            onSetOutOfService(tagId);
        });
    }

    menu.addSeparator();

    // === 查看趋势 ===
    menu.addAction(QStringLiteral("📈 查看趋势"), this, [this, tagId]() {
        if (m_trendWidget && tagId != 0) {
            for (int i = 0; i < m_centralTabs->count(); ++i) {
                if (m_centralTabs->tabText(i).contains(QStringLiteral("趋势"))) {
                    m_centralTabs->setCurrentIndex(i);
                    break;
                }
            }
        }
    });

    // === 查看KPI ===
    menu.addAction(QStringLiteral("📊 查看KPI仪表盘"), this, [this]() {
        for (int i = 0; i < m_centralTabs->count(); ++i) {
            if (m_centralTabs->tabText(i).contains(QStringLiteral("KPI"))) {
                m_centralTabs->setCurrentIndex(i);
                break;
            }
        }
        onRefreshKpiDashboard();
    });

    menu.exec(m_alarmTable->viewport()->mapToGlobal(pos));
}

void MYDSCProject::onDeviceTreeContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_deviceTree->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #2d2d2d; color: #ccc; border: 1px solid #555; padding: 4px; }"
        "QMenu::item { padding: 6px 20px; border-radius: 3px; }"
        "QMenu::item:selected { background: #1f6feb; }");

    bool isDevice = (item->parent() == nullptr);
    int serverAddr = item->data(0, Qt::UserRole).toInt();

    if (isDevice) {
        menu.addAction(QStringLiteral("连接设备"), this, [this, serverAddr]() {
            if (m_dataEngine && serverAddr > 0) {
                m_dataEngine->start();
                LOG_INFO("MainWindow", QString("连接设备 Server=%1").arg(serverAddr));
            }
        });
        menu.addAction(QStringLiteral("断开设备"), this, [this, serverAddr]() {
            if (m_dataEngine) {
                m_dataEngine->stop();
                LOG_INFO("MainWindow", QString("断开设备 Server=%1").arg(serverAddr));
            }
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("属性"), this, [this, item]() {
            QMessageBox::information(this, QStringLiteral("设备属性"),
                QStringLiteral("设备: %1\n地址: %2").arg(item->text(0)).arg(item->data(0, Qt::UserRole).toInt()));
        });
    } else {
        quint32 tagId = item->data(0, Qt::UserRole).toUInt();
        auto tag = TagConfigMgr::instance().getTag(tagId);
        if (tag.tagId != 0) {
            menu.addAction(QStringLiteral("查看属性"), this, [this, tag]() {
                QMessageBox::information(this, QStringLiteral("位号属性"),
                    QStringLiteral("名称: %1\n描述: %2\n值: %3\n单位: %4\n量程: %5 - %6")
                        .arg(tag.tagName).arg(tag.description)
                        .arg(tag.currentValue, 0, 'f', 2).arg(tag.unit)
                        .arg(tag.engLow).arg(tag.engHigh));
            });
        }
    }

    menu.exec(m_deviceTree->viewport()->mapToGlobal(pos));
}

void MYDSCProject::onPidViewContextMenu(const QPoint& pos)
{
    if (!m_pidScene || !m_pidView) return;

    QGraphicsItem* clickedItem = m_pidView->itemAt(pos);
    if (!clickedItem) {
        QMenu menu(this);
        menu.setStyleSheet(
            "QMenu { background: #2d2d2d; color: #ccc; border: 1px solid #555; padding: 4px; }"
            "QMenu::item { padding: 6px 20px; border-radius: 3px; }"
            "QMenu::item:selected { background: #1f6feb; }");
        menu.addAction(QStringLiteral("适应窗口"), this, &MYDSCProject::onZoomFit);
        menu.addAction(QStringLiteral("重置缩放"), this, [this]() {
            m_pidView->resetTransform();
            m_zoomSlider->setValue(100);
            m_zoomLabel->setText(" 100% ");
        });
        menu.exec(m_pidView->viewport()->mapToGlobal(pos));
        return;
    }

    auto* baseItem = dynamic_cast<BaseGraphicsItem*>(clickedItem);
    if (!baseItem) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #2d2d2d; color: #ccc; border: 1px solid #555; padding: 4px; }"
        "QMenu::item { padding: 6px 20px; border-radius: 3px; }"
        "QMenu::item:selected { background: #1f6feb; }");

    menu.addAction(QStringLiteral("类型: %1").arg(baseItem->itemTypeName()))->setEnabled(false);

    QString tagName = baseItem->boundTagName();
    if (!tagName.isEmpty()) {
        menu.addAction(QStringLiteral("绑定位号: %1").arg(tagName))->setEnabled(false);
        menu.addAction(QStringLiteral("查看位号属性"), this, [this, baseItem]() {
            auto tag = TagConfigMgr::instance().getTag(baseItem->boundTagId());
            if (tag.tagId != 0) {
                QMessageBox::information(this, QStringLiteral("位号属性"),
                    QStringLiteral("名称: %1\n值: %2 %3\n量程: %4 - %5")
                        .arg(tag.tagName).arg(tag.currentValue, 0, 'f', 2).arg(tag.unit)
                        .arg(tag.engLow).arg(tag.engHigh));
            }
        });
    }

    menu.addSeparator();
    menu.addAction(QStringLiteral("适应窗口"), this, &MYDSCProject::onZoomFit);

    menu.exec(m_pidView->viewport()->mapToGlobal(pos));
}

void MYDSCProject::onAcknowledgeAlarm()
{
    int row = m_alarmTable->currentRow();
    if (row < 0) return;
    // tagId存储在列0的UserRole中
    quint32 tagId = m_alarmTable->item(row, 0)->data(Qt::UserRole).toUInt();
    if (tagId == 0) {
        tagId = m_alarmTable->item(row, 0)->text().toUInt();
    }
    AlarmEngine::instance().acknowledgeAlarmByTagId(tagId);
}

// ============================================================
// 菜单Action
// ============================================================
void MYDSCProject::onOpenProject()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("选择项目配置目录"),
        QString("./config"));
    if (dir.isEmpty()) return;

    // Load tags.json from selected directory
    QString tagsPath = dir + "/tags.json";
    if (QFile::exists(tagsPath)) {
        ConfigManager::instance().loadTags(tagsPath, m_dataEngine);
        rebuildDataTable();
        LOG_INFO("MainWindow", QString("加载位号配置: %1").arg(tagsPath));
    }

    // Load scene.json from selected directory
    QString scenePath = dir + "/scene.json";
    if (QFile::exists(scenePath) && m_pidScene) {
        ConfigManager::instance().loadScene(scenePath, m_pidScene);
        LOG_INFO("MainWindow", QString("加载场景配置: %1").arg(scenePath));
    }
}

void MYDSCProject::onSaveProject()
{
    QString dir = QFileDialog::getExistingDirectory(this,
        QStringLiteral("选择保存目录"));
    if (dir.isEmpty()) return;

    // Save tags.json
    QString tagsPath = dir + "/tags.json";
    if (ConfigManager::instance().saveTags(tagsPath)) {
        LOG_INFO("MainWindow", QString("保存位号配置: %1").arg(tagsPath));
    }

    // Save scene.json
    QString scenePath = dir + "/scene.json";
    if (m_pidScene && ConfigManager::instance().saveScene(scenePath, m_pidScene)) {
        LOG_INFO("MainWindow", QString("保存场景配置: %1").arg(scenePath));
    }
}

void MYDSCProject::onExportData()
{
    QStringList options;
    options << QStringLiteral("位号实时值表") << QStringLiteral("报警历史记录") << QStringLiteral("操作日志");
    bool ok = false;
    QString choice = QInputDialog::getItem(this, QStringLiteral("导出数据"),
        QStringLiteral("选择导出类型:"), options, 0, false, &ok);
    if (!ok || choice.isEmpty()) return;

    QString filePath = QFileDialog::getSaveFileName(this,
        QStringLiteral("保存CSV文件"), QString(), QStringLiteral("CSV文件 (*.csv)"));
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
            QStringLiteral("无法创建文件: %1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    // BOM for Excel UTF-8 compatibility
    out.setEncoding(QStringConverter::Utf8);
    out << QChar(0xFEFF);

    if (choice == options[0]) {
        // 导出位号实时值
        out << "位号ID,位号名称,描述,当前值,单位,质量,报警状态\n";
        auto snapshot = m_dataEngine ? m_dataEngine->doubleBuffer()->readAll() : nullptr;
        for (const auto& tag : TagConfigMgr::instance().getAllTags()) {
            QString valueStr = "--";
            QString qualityStr = "--";
            QString alarmStr = "--";
            if (snapshot) {
                auto it = snapshot->find(tag.tagId);
                if (it != snapshot->end()) {
                    valueStr = QString::number(it->second.currentValue, 'f', 2);
                    switch (it->second.quality) {
                    case DataQuality::Good: qualityStr = "正常"; break;
                    case DataQuality::Uncertain: qualityStr = "可疑"; break;
                    case DataQuality::Bad: qualityStr = "失效"; break;
                    }
                    switch (it->second.alarmstate) {
                    case AlarmLimit::HighHigh: alarmStr = "HH"; break;
                    case AlarmLimit::High: alarmStr = "H"; break;
                    case AlarmLimit::Low: alarmStr = "L"; break;
                    case AlarmLimit::LowLow: alarmStr = "LL"; break;
                    default: alarmStr = "正常"; break;
                    }
                }
            }
            out << tag.tagId << ","
                << "\"" << tag.tagName << "\","
                << "\"" << tag.description << "\","
                << valueStr << ","
                << tag.unit << ","
                << qualityStr << ","
                << alarmStr << "\n";
        }
    } else if (choice == options[1]) {
        // 导出报警历史
        out << "报警ID,位号,级别,描述,触发值,限值,触发时间,确认时间,恢复时间,已确认\n";
        auto alarms = AlarmEngine::instance().alarmHistory();
        for (const auto& a : alarms) {
            QString sevStr;
            switch (a.limit) {
            case AlarmLimit::HighHigh: sevStr = "HH"; break;
            case AlarmLimit::High: sevStr = "H"; break;
            case AlarmLimit::Low: sevStr = "L"; break;
            case AlarmLimit::LowLow: sevStr = "LL"; break;
            default: sevStr = "Normal"; break;
            }
            out << a.alarmId << ","
                << "\"" << a.tagName << "\","
                << sevStr << ","
                << "\"" << a.description << "\","
                << a.triggerValue << ","
                << a.thresholdValue << ","
                << QDateTime::fromMSecsSinceEpoch(a.triggerTime).toString("yyyy-MM-dd HH:mm:ss") << ","
                << (a.acknowledgeTime > 0 ? QDateTime::fromMSecsSinceEpoch(a.acknowledgeTime).toString("yyyy-MM-dd HH:mm:ss") : "") << ","
                << (a.returnAckTime > 0 ? QDateTime::fromMSecsSinceEpoch(a.returnAckTime).toString("yyyy-MM-dd HH:mm:ss") : "") << ","
                << (a.acknowledged ? "是" : "否") << "\n";
        }
    } else if (choice == options[2]) {
        // 导出操作日志（最近30天）
        out << "用户,操作,详情,时间\n";
        QDateTime startTime = QDateTime::currentDateTime().addDays(-30);
        auto logs = DatabaseManager::instance().queryOperationLog(startTime, QDateTime::currentDateTime(), 5000);
        for (const auto& log : logs) {
            out << "\"" << log["username"].toString() << "\","
                << "\"" << log["action"].toString() << "\","
                << "\"" << log["detail"].toString() << "\","
                << QDateTime::fromMSecsSinceEpoch(log["timestamp"].toLongLong()).toString("yyyy-MM-dd HH:mm:ss") << "\n";
        }
    }

    file.close();
    LOG_INFO("MainWindow", QString("数据导出完成: %1").arg(filePath));
    QMessageBox::information(this, QStringLiteral("导出成功"),
        QStringLiteral("数据已导出到:\n%1").arg(filePath));
}

void MYDSCProject::onGenerateReport()
{
    // 构建HTML报告
    QString html;
    html += "<html><head><meta charset='UTF-8'>";
    html += "<style>"
        "body { font-family: 'Microsoft YaHei', sans-serif; color: #333; padding: 20px; }"
        "h1 { color: #1f6feb; border-bottom: 2px solid #1f6feb; padding-bottom: 8px; }"
        "h2 { color: #444; margin-top: 24px; }"
        "table { width: 100%; border-collapse: collapse; margin: 12px 0; }"
        "th { background: #1f6feb; color: white; padding: 8px; text-align: left; }"
        "td { padding: 6px 8px; border-bottom: 1px solid #ddd; }"
        "tr:nth-child(even) { background: #f5f5f5; }"
        ".alarm-HH { color: red; font-weight: bold; }"
        ".alarm-H { color: darkorange; font-weight: bold; }"
        ".footer { margin-top: 30px; font-size: 12px; color: #888; text-align: center; }"
        "</style></head><body>";

    html += "<h1>MY DCS 系统报告</h1>";
    html += QString("<p>生成时间: %1</p>").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));

    // 系统状态摘要
    int activeAlarms = AlarmEngine::instance().activeAlarmCount();
    int totalTags = TagConfigMgr::instance().tagCount();
    html += "<h2>系统状态</h2>";
    html += QString("<p>活跃报警: %1<br>位号总数: %2<br>数据库后端: %3</p>")
        .arg(activeAlarms).arg(totalTags).arg(DatabaseManager::instance().backendType());

    // 报警摘要
    html += "<h2>活跃报警</h2>";
    auto alarms = AlarmEngine::instance().activeAlarms();
    if (alarms.isEmpty()) {
        html += "<p style='color: green;'>当前无活跃报警</p>";
    } else {
        html += "<table><tr><th>位号</th><th>级别</th><th>描述</th><th>值</th><th>限值</th><th>时间</th></tr>";
        for (const auto& a : alarms) {
            QString sevClass;
            QString sevStr;
            switch (a.limit) {
            case AlarmLimit::HighHigh: sevClass = "alarm-HH"; sevStr = "HH"; break;
            case AlarmLimit::High: sevClass = "alarm-H"; sevStr = "H"; break;
            case AlarmLimit::LowLow: sevClass = "alarm-HH"; sevStr = "LL"; break;
            case AlarmLimit::Low: sevClass = "alarm-H"; sevStr = "L"; break;
            default: sevStr = "-"; break;
            }
            html += QString("<tr><td>%1</td><td class='%2'>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td></tr>")
                .arg(a.tagName).arg(sevClass).arg(sevStr).arg(a.description)
                .arg(a.triggerValue, 0, 'f', 1).arg(a.thresholdValue, 0, 'f', 1)
                .arg(QDateTime::fromMSecsSinceEpoch(a.triggerTime).toString("HH:mm:ss"));
        }
        html += "</table>";
    }

    // 位号统计
    html += "<h2>位号统计</h2>";
    int aiCount = 0, aoCount = 0, diCount = 0, doCount = 0, pidCount = 0;
    for (const auto& tag : TagConfigMgr::instance().getAllTags()) {
        switch (tag.tagType) {
        case TagType::AI: aiCount++; break;
        case TagType::AO: aoCount++; break;
        case TagType::DI: diCount++; break;
        case TagType::DO: doCount++; break;
        case TagType::PID: pidCount++; break;
        }
    }
    html += QString("<p>AI: %1 | AO: %2 | DI: %3 | DO: %4 | PID: %5</p>")
        .arg(aiCount).arg(aoCount).arg(diCount).arg(doCount).arg(pidCount);

    html += "<div class='footer'>MY DCS - 分布式控制系统 报告</div>";
    html += "</body></html>";

    // 打印到PDF
    QTextDocument doc;
    doc.setHtml(html);

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize::A4);
    printer.setOutputFormat(QPrinter::PdfFormat);

    QString pdfPath = QFileDialog::getSaveFileName(this,
        QStringLiteral("保存报告"), QString("./report.pdf"),
        QStringLiteral("PDF文件 (*.pdf)"));
    if (pdfPath.isEmpty()) return;

    printer.setOutputFileName(pdfPath);
    doc.print(&printer);

    LOG_INFO("MainWindow", QString("报告已生成: %1").arg(pdfPath));
    QMessageBox::information(this, QStringLiteral("报告已生成"),
        QStringLiteral("报告已保存到:\n%1").arg(pdfPath));
}

void MYDSCProject::onToggleFullScreen()
{
    m_isFullScreen = !m_isFullScreen;
    if (m_isFullScreen) {
        showFullScreen();
    }
    else {
        showNormal();
    }
}

void MYDSCProject::onToggleLeftDock()
{
    m_leftDock->setVisible(!m_leftDock->isVisible());
}

void MYDSCProject::onToggleRightDock()
{
    m_rightDock->setVisible(!m_rightDock->isVisible());
}

// ============================================================
// P&ID 缩放控制
// ============================================================
void MYDSCProject::onZoomIn()
{
    if (!m_pidView) return;
    double factor = m_pidView->transform().m11() * 1.25;
    if (factor > 4.0) factor = 4.0;
    m_pidView->resetTransform();
    m_pidView->scale(factor, factor);
    if (m_zoomSlider) m_zoomSlider->setValue(qRound(factor * 100));
    if (m_zoomLabel) m_zoomLabel->setText(QString(" %1% ").arg(qRound(factor * 100)));
}

void MYDSCProject::onZoomOut()
{
    if (!m_pidView) return;
    double factor = m_pidView->transform().m11() * 0.8;
    if (factor < 0.1) factor = 0.1;
    m_pidView->resetTransform();
    m_pidView->scale(factor, factor);
    if (m_zoomSlider) m_zoomSlider->setValue(qRound(factor * 100));
    if (m_zoomLabel) m_zoomLabel->setText(QString(" %1% ").arg(qRound(factor * 100)));
}

void MYDSCProject::onZoomFit()
{
    if (!m_pidView || !m_pidScene) return;
    m_pidView->fitInView(m_pidScene->sceneRect(), Qt::KeepAspectRatio);
    double factor = m_pidView->transform().m11();
    if (m_zoomSlider) m_zoomSlider->setValue(qRound(factor * 100));
    if (m_zoomLabel) m_zoomLabel->setText(QString(" %1% ").arg(qRound(factor * 100)));
}

void MYDSCProject::onZoomSliderChanged(int value)
{
    if (!m_pidView) return;
    double factor = value / 100.0;
    m_pidView->resetTransform();
    m_pidView->scale(factor, factor);
    if (m_zoomLabel) m_zoomLabel->setText(QString(" %1% ").arg(value));
}

void MYDSCProject::onConnectAll()
{
    if (!AuthManager::instance().confirmCriticalAction(
            QStringLiteral("连接所有设备"), QStringLiteral("启动所有Modbus通信连接")))
        return;
    auto ret = QMessageBox::question(this, QStringLiteral("确认操作"),
        QStringLiteral("确定要连接所有设备吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    if (m_dataEngine) {
        m_dataEngine->start();
        LOG_INFO("MainWindow", "连接所有设备");
    }
}

void MYDSCProject::onDisconnectAll()
{
    if (!AuthManager::instance().confirmCriticalAction(
            QStringLiteral("断开所有设备"), QStringLiteral("停止所有Modbus通信连接")))
        return;
    auto ret = QMessageBox::question(this, QStringLiteral("确认操作"),
        QStringLiteral("确定要断开所有设备吗？\n这将中断所有数据采集。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    if (m_dataEngine) {
        m_dataEngine->stop();
        LOG_INFO("MainWindow", "断开所有设备");
    }
}

void MYDSCProject::onAcknowledgeAllAlarms()
{
    if (!AuthManager::instance().confirmCriticalAction(
            QStringLiteral("确认全部报警"), QStringLiteral("确认所有当前活跃报警")))
        return;
    auto ret = QMessageBox::question(this, QStringLiteral("确认操作"),
        QStringLiteral("确定要确认全部报警吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    AlarmEngine::instance().acknowledgeAll();
    LOG_INFO("MainWindow", "确认全部报警");
}

void MYDSCProject::onToggleSound()
{
    m_soundEnabled = !m_soundEnabled;
    AlarmEngine::instance().setSoundEnabled(m_soundEnabled);
    m_actSound->setText(m_soundEnabled ? QStringLiteral(" 静音 ") : QStringLiteral(" 取消静音 "));
    LOG_INFO("MainWindow", QStringLiteral("报警音: %1").arg(m_soundEnabled ? "开启" : "关闭"));
}

void MYDSCProject::onShowPerformanceMonitor()
{
    QString report = PerformanceMonitor::instance().generateReport();
    QMessageBox::information(this, QStringLiteral("性能监视器"),
        report.isEmpty() ? QStringLiteral("暂无性能数据") : report);
}

void MYDSCProject::onShowTagConfig()
{
    // 检查权限：仅工程师(Engineer)及以上可编辑位号配置
    if (!AuthManager::instance().hasPermission(UserLevel::Engineer)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("您没有编辑位号配置的权限。\n需要工程师或管理员权限。"));
        AuthManager::instance().confirmCriticalAction(
            QStringLiteral("尝试打开位号配置编辑器"), QStringLiteral("权限不足"));
        return;
    }

    TagConfigDialog dialog(this);
    dialog.exec();

    // 配置可能已修改，刷新数据表
    rebuildDataTable();
}

void MYDSCProject::onAbout()
{
    QMessageBox::about(this, QStringLiteral("关于 MY DCS"),
        QStringLiteral("MY DCS v1.0\n"
            "分布式控制系统\n"
            "Qt 6 + MSVC 构建\n"
            "支持 Modbus RTU/TCP 多设备采集\n"
            "实时数据引擎 | 报警管理 | P&ID 可视化"));
}

// ============================================================
// 数据总表更新（从DoubleBuffer读取实时值）
// ============================================================
void MYDSCProject::updateDataTable()
{
    if (!m_dataTable || !m_dataEngine) return;

    // 首次调用时构建表结构（此时TagConfigMgr已有数据）
    if (m_dataTable->rowCount() == 0) {
        rebuildDataTable();
    }

    // 从DoubleBuffer更新实时值
    auto snapshot = m_dataEngine->doubleBuffer()->readAll();
    for (int row = 0; row < m_dataTable->rowCount(); ++row) {
        QTableWidgetItem* idItem = m_dataTable->item(row, 0);
        if (!idItem) continue;
        quint32 tagId = idItem->data(Qt::UserRole).toUInt();

        auto it = snapshot->find(tagId);
        if (it == snapshot->end()) {
            // 无数据：显示"--"
            if (m_dataTable->item(row, 3)) m_dataTable->item(row, 3)->setText("--");
            if (m_dataTable->item(row, 5)) m_dataTable->item(row, 5)->setText("NoData");
            if (m_dataTable->item(row, 6)) m_dataTable->item(row, 6)->setText("Unknown");
            continue;
        }

        const auto& tag = it->second;
        m_dataTable->item(row, 3)->setText(QString::number(tag.currentValue, 'f', 2));

        // 质量
        QString qStr;
        switch (tag.quality) {
        case DataQuality::Good:      qStr = QStringLiteral("正常"); break;
        case DataQuality::Uncertain: qStr = QStringLiteral("可疑"); break;
        case DataQuality::Bad:       qStr = QStringLiteral("失效"); break;
        default:                     qStr = "?"; break;
        }
        m_dataTable->item(row, 5)->setText(qStr);

        // 报警状态（带颜色）
        QString alarmStr;
        QString alarmBg;
        switch (tag.alarmstate) {
        case AlarmLimit::HighHigh: alarmStr = "HH"; alarmBg = "#ff6464"; break;
        case AlarmLimit::High:     alarmStr = "H";  alarmBg = "#ffb464"; break;
        case AlarmLimit::LowLow:   alarmStr = "LL"; alarmBg = "#ff6464"; break;
        case AlarmLimit::Low:      alarmStr = "L";  alarmBg = "#ffb464"; break;
        default:                   alarmStr = QStringLiteral("正常"); alarmBg = "#c8ffc8"; break;
        }
        m_dataTable->item(row, 6)->setText(alarmStr);
        m_dataTable->item(row, 6)->setBackground(QColor(alarmBg));
    }
}

// ============================================================
// 重建数据表结构（从TagConfigMgr读取）
// ============================================================
void MYDSCProject::rebuildDataTable()
{
    if (!m_dataTable) return;

    auto tags = TagConfigMgr::instance().getAllTags();
    m_dataTable->setRowCount(tags.size());

    int row = 0;
    for (const auto& tag : tags) {
        auto* idItem = new QTableWidgetItem(QString::number(tag.tagId));
        idItem->setData(Qt::UserRole, tag.tagId);
        m_dataTable->setItem(row, 0, idItem);
        m_dataTable->setItem(row, 1, new QTableWidgetItem(tag.tagName));
        m_dataTable->setItem(row, 2, new QTableWidgetItem(tag.description));
        m_dataTable->setItem(row, 3, new QTableWidgetItem("--"));
        m_dataTable->setItem(row, 4, new QTableWidgetItem(tag.unit));
        m_dataTable->setItem(row, 5, new QTableWidgetItem("--"));
        m_dataTable->setItem(row, 6, new QTableWidgetItem("--"));
        row++;
    }
}

// ============================================================
// P&ID图元值更新（从DoubleBuffer推送到图元）
// ============================================================
void MYDSCProject::updatePidItems()
{
    if (!m_pidScene || !m_dataEngine) return;

    auto snapshot = m_dataEngine->doubleBuffer()->readAll();
    for (auto* item : m_pidScene->allGraphicItems()) {
        quint32 tagId = item->boundTagId();
        if (tagId == 0) continue;

        auto it = snapshot->find(tagId);
        if (it == snapshot->end()) continue;

        const auto& tagData = it->second;
        // 通过基类的回调接口更新值（触发updateAppearance和重绘）
        item->onTagValueChanged(tagId, tagData.currentValue);
    }
}

// ============================================================
// 日志追加（按级别着色）
// ============================================================
void MYDSCProject::appendLog(Log_Level level, const QString& model, const QString& message)
{
    if (!m_logView) return;

    QString color;
    switch (level) {
    case Log_Level::Error:   color = "#ff4444"; break;
    case Log_Level::Warning: color = "#ffaa00"; break;
    case Log_Level::Fatal:   color = "#ff0000"; break;
    case Log_Level::Debug:   color = "#888888"; break;
    default:                 color = "#00ff00"; break;
    }

    // 限制行数防止内存溢出（超过上限时清空整个文档）
    if (m_logView->document()->blockCount() > 1000) {
        m_logView->clear();
    }

    // 追加日志（使用insertHtml确保HTML渲染）
    QTextCursor textCursor(m_logView->document());
    textCursor.movePosition(QTextCursor::End);
    textCursor.insertHtml(
        QString("<span style='color:%1'>[%2] %3</span><br>")
            .arg(color)
            .arg(model)
            .arg(message.toHtmlEscaped()));

    // 自动滚动到底部
    auto* scrollBar = m_logView->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

// ============================================================
// 演示模式
// ============================================================
void MYDSCProject::loadDemoData()
{
    if (m_demoTimer) {
        m_demoTimer->stop();
        delete m_demoTimer;
        m_demoTimer = nullptr;
    }

    m_demoTimer = new QTimer(this);
    connect(m_demoTimer, &QTimer::timeout, this, [this]() {
        static quint64 counter = 0;
        counter++;

        auto tags = TagConfigMgr::instance().getAllTags();
        if (tags.isEmpty()) return;

        // 跟踪报警状态变化，避免重复触发
        static QHash<quint32, AlarmLimit> prevAlarmStates;

        for (const auto& tagInfo : tags) {
            // 只给AI类型生成模拟值
            if (tagInfo.tagType != TagType::AI) continue;

            // 正弦波模拟：每位号有不同相位和幅值
            double phase = tagInfo.tagId * 1.7;
            double mid = (tagInfo.engHigh + tagInfo.engLow) / 2.0;
            double amp = (tagInfo.engHigh - tagInfo.engLow) * 0.45;
            float value = static_cast<float>(mid + amp * sin(counter * 0.05 + phase));

            // 报警判断
            AlarmLimit newLimit = AlarmLimit::Normal;
            float threshold = 0;
            if (value >= tagInfo.highHighLimit) {
                newLimit = AlarmLimit::HighHigh;
                threshold = tagInfo.highHighLimit;
            } else if (value >= tagInfo.highLimit) {
                newLimit = AlarmLimit::High;
                threshold = tagInfo.highLimit;
            } else if (value <= tagInfo.lowLowLimit) {
                newLimit = AlarmLimit::LowLow;
                threshold = tagInfo.lowLowLimit;
            } else if (value <= tagInfo.lowLimit) {
                newLimit = AlarmLimit::Low;
                threshold = tagInfo.lowLimit;
            }

            // 写入完整快照到DoubleBuffer（含报警状态）
            {
                DoubleBuffer::TagSnapshot snap;
                snap.tagId = tagInfo.tagId;
                snap.currentValue = value;
                snap.quality = DataQuality::Good;
                snap.alarmstate = newLimit;
                snap.timestamp = QDateTime::currentMSecsSinceEpoch();
                m_dataEngine->doubleBuffer()->write(tagInfo.tagId, snap);
            }

            // 仅在状态切换时触发报警引擎
            auto prevIt = prevAlarmStates.find(tagInfo.tagId);
            AlarmLimit prevLimit = (prevIt != prevAlarmStates.end()) ? prevIt.value() : AlarmLimit::Normal;

            if (newLimit != prevLimit) {
                if (newLimit != AlarmLimit::Normal) {
                    AlarmEngine::instance().triggerAlarm(
                        tagInfo.tagId, newLimit, value, threshold,
                        tagInfo.priority, tagInfo.classification, tagInfo.onDelayMs);
                    onAlarmTriggered(tagInfo.tagId, newLimit, value, threshold);
                } else {
                    AlarmEngine::instance().clearAlarm(tagInfo.tagId, value);
                    onAlarmCleared(tagInfo.tagId);
                }
                prevAlarmStates[tagInfo.tagId] = newLimit;
            }
        }

        // 提交所有写入，让UI线程可见
        m_dataEngine->doubleBuffer()->commit();

        // 更新P&ID场景图元值（triggered by callbacks from updateValue above）
        updatePidItems();

        // 刷新场景显示
        if (m_pidScene) {
            m_pidScene->update();
        }

        // 推送数据到趋势图
        if (m_trendWidget) {
            auto snapshot = m_dataEngine->doubleBuffer()->readAll();
            for (const auto& [tagId, snap] : *snapshot) {
                auto tagInfo = TagConfigMgr::instance().getTag(tagId);
                if (!tagInfo.tagName.isEmpty()) {
                    m_trendWidget->appendData(tagId, tagInfo.tagName, snap.currentValue);
                }
            }
        }
    });
}

void MYDSCProject::onToggleDemo()
{
    m_demoMode = !m_demoMode;

    if (m_demoMode) {
        // 启动演示
        loadDemoData();
        m_demoTimer->start(500);
        m_actDemo->setText(QStringLiteral(" ⏹ 停止演示 "));
        LOG_INFO("MainWindow", "演示模式已启动");
    } else {
        // 停止演示
        if (m_demoTimer) {
            m_demoTimer->stop();
        }
        m_actDemo->setText(QStringLiteral(" ▶ 演示 "));
        LOG_INFO("MainWindow", "演示模式已停止");
    }
}

// ============================================================
// ============================================================
//
//  ISA-18.2 商业化 UI 增强实现
//
//  包含：
//  1. 报警过滤工具栏（按优先级/状态/分类/区域/限值类型）
//  2. KPI 仪表盘（EEMUA 191 标准指标）
//  3. 变更日志查看器（审计追踪）
//  4. 报警屏蔽/抑制/停用操作
//  5. 操作员注释对话框
//  6. 报警状态显示辅助方法
//
// ============================================================
// ============================================================

// ============================================================
// 报警过滤工具栏
//
// ISA-18.2 要求报警汇总支持按多种条件过滤，
// 帮助操作员在报警洪峰中快速定位关键报警。
// 过滤维度：
// - 优先级（Critical/Major/Minor/Advisory）
// - 报警状态（Active/RTN/Shelved/Suppressed/OOS）
// - 分类（Process/Safety/Equipment/Environmental）
// - 区域（按工艺区域划分）
// - 限值类型（HH/H/L/LL/Deviation/ROC）
// ============================================================
void MYDSCProject::setupAlarmFilterBar()
{
    m_alarmFilterBar = new QWidget(this);
    QHBoxLayout* filterLayout = new QHBoxLayout(m_alarmFilterBar);
    filterLayout->setContentsMargins(4, 2, 4, 2);
    filterLayout->setSpacing(6);

    // 优先级过滤
    m_filterPriority = new QComboBox(m_alarmFilterBar);
    m_filterPriority->addItem(QStringLiteral("全部优先级"), -1);
    m_filterPriority->addItem(QStringLiteral("Critical"), static_cast<int>(AlarmPriority::Critical));
    m_filterPriority->addItem(QStringLiteral("Major"), static_cast<int>(AlarmPriority::Major));
    m_filterPriority->addItem(QStringLiteral("Minor"), static_cast<int>(AlarmPriority::Minor));
    m_filterPriority->addItem(QStringLiteral("Advisory"), static_cast<int>(AlarmPriority::Advisory));
    m_filterPriority->setFixedWidth(100);
    filterLayout->addWidget(new QLabel(QStringLiteral("优先级:"), m_alarmFilterBar));
    filterLayout->addWidget(m_filterPriority);

    // 报警状态过滤
    m_filterState = new QComboBox(m_alarmFilterBar);
    m_filterState->addItem(QStringLiteral("全部状态"), -1);
    m_filterState->addItem(QStringLiteral("活跃(未确认)"), static_cast<int>(AlarmState::ActiveUnacknowledged));
    m_filterState->addItem(QStringLiteral("活跃(已确认)"), static_cast<int>(AlarmState::ActiveAcknowledged));
    m_filterState->addItem(QStringLiteral("恢复(未确认)"), static_cast<int>(AlarmState::ReturnToNormalUnacknowledged));
    m_filterState->addItem(QStringLiteral("已屏蔽"), static_cast<int>(AlarmState::Shelved));
    m_filterState->addItem(QStringLiteral("已抑制"), static_cast<int>(AlarmState::SuppressedByDesign));
    m_filterState->addItem(QStringLiteral("停用"), static_cast<int>(AlarmState::OutOfService));
    m_filterState->setFixedWidth(120);
    filterLayout->addWidget(new QLabel(QStringLiteral("状态:"), m_alarmFilterBar));
    filterLayout->addWidget(m_filterState);

    // 分类过滤
    m_filterClassification = new QComboBox(m_alarmFilterBar);
    m_filterClassification->addItem(QStringLiteral("全部分类"), -1);
    m_filterClassification->addItem(QStringLiteral("工艺"), static_cast<int>(AlarmClassification::Process));
    m_filterClassification->addItem(QStringLiteral("安全"), static_cast<int>(AlarmClassification::Safety));
    m_filterClassification->addItem(QStringLiteral("设备"), static_cast<int>(AlarmClassification::Machinery));
    m_filterClassification->addItem(QStringLiteral("环境"), static_cast<int>(AlarmClassification::Environmental));
    m_filterClassification->setFixedWidth(80);
    filterLayout->addWidget(new QLabel(QStringLiteral("分类:"), m_alarmFilterBar));
    filterLayout->addWidget(m_filterClassification);

    // 区域过滤
    m_filterArea = new QComboBox(m_alarmFilterBar);
    m_filterArea->addItem(QStringLiteral("全部区域"), -1);
    // 动态填充区域列表（从TagConfigMgr获取）
    auto tags = TagConfigMgr::instance().getAllTags();
    QSet<QString> areas;
    for (const auto& tag : tags) {
        if (!tag.area.isEmpty()) {
            areas.insert(tag.area);
        }
    }
    for (const auto& area : areas) {
        m_filterArea->addItem(area, area);
    }
    m_filterArea->setFixedWidth(90);
    filterLayout->addWidget(new QLabel(QStringLiteral("区域:"), m_alarmFilterBar));
    filterLayout->addWidget(m_filterArea);

    // 限值类型过滤
    m_filterLimit = new QComboBox(m_alarmFilterBar);
    m_filterLimit->addItem(QStringLiteral("全部类型"), -1);
    m_filterLimit->addItem(QStringLiteral("HH"), static_cast<int>(AlarmLimit::HighHigh));
    m_filterLimit->addItem(QStringLiteral("H"), static_cast<int>(AlarmLimit::High));
    m_filterLimit->addItem(QStringLiteral("L"), static_cast<int>(AlarmLimit::Low));
    m_filterLimit->addItem(QStringLiteral("LL"), static_cast<int>(AlarmLimit::LowLow));
    m_filterLimit->addItem(QStringLiteral("DEV"), static_cast<int>(AlarmLimit::Deviation));
    m_filterLimit->addItem(QStringLiteral("ROC"), static_cast<int>(AlarmLimit::RateOfChange));
    m_filterLimit->setFixedWidth(80);
    filterLayout->addWidget(new QLabel(QStringLiteral("类型:"), m_alarmFilterBar));
    filterLayout->addWidget(m_filterLimit);

    // 刷新按钮
    QPushButton* refreshBtn = new QPushButton(QStringLiteral("🔄 刷新"), m_alarmFilterBar);
    refreshBtn->setFixedWidth(60);
    filterLayout->addWidget(refreshBtn);

    filterLayout->addStretch();

    // 连接过滤信号
    connect(m_filterPriority, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MYDSCProject::onAlarmFilterChanged);
    connect(m_filterState, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MYDSCProject::onAlarmFilterChanged);
    connect(m_filterClassification, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MYDSCProject::onAlarmFilterChanged);
    connect(m_filterArea, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MYDSCProject::onAlarmFilterChanged);
    connect(m_filterLimit, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MYDSCProject::onAlarmFilterChanged);
    connect(refreshBtn, &QPushButton::clicked, this, &MYDSCProject::refreshAlarmSummary);
}

// ============================================================
// 报警过滤条件变更 → 刷新报警汇总
// ============================================================
void MYDSCProject::onAlarmFilterChanged()
{
    refreshAlarmSummary();
}

// ============================================================
// 刷新报警汇总列表（按过滤条件）
//
// 从 AlarmEngine 获取所有活跃报警，按过滤条件筛选后
// 重新填充报警表格。这是 ISA-18.2 报警汇总的核心功能。
// ============================================================
void MYDSCProject::refreshAlarmSummary()
{
    if (!m_alarmTable) return;

    // 获取过滤条件
    int priorityFilter = m_filterPriority ? m_filterPriority->currentData().toInt() : -1;
    int stateFilter = m_filterState ? m_filterState->currentData().toInt() : -1;
    int classFilter = m_filterClassification ? m_filterClassification->currentData().toInt() : -1;
    QString areaFilter = m_filterArea ? m_filterArea->currentData().toString() : QString();
    int limitFilter = m_filterLimit ? m_filterLimit->currentData().toInt() : -1;

    // 获取所有活跃报警
    auto alarms = AlarmEngine::instance().activeAlarms();

    // 清空表格
    m_alarmTable->setRowCount(0);

    for (const auto& alarm : alarms) {
        // 按优先级过滤
        if (priorityFilter >= 0 && static_cast<int>(alarm.priority) != priorityFilter) continue;

        // 按状态过滤
        if (stateFilter >= 0 && static_cast<int>(alarm.state) != stateFilter) continue;

        // 按分类过滤
        if (classFilter >= 0 && static_cast<int>(alarm.classification) != classFilter) continue;

        // 按区域过滤
        if (!areaFilter.isEmpty() && alarm.area != areaFilter) continue;

        // 按限值类型过滤
        if (limitFilter >= 0 && static_cast<int>(alarm.limit) != limitFilter) continue;

        // 添加行
        int row = m_alarmTable->rowCount();
        m_alarmTable->insertRow(row);

        QColor bgColor = alarmStateColor(alarm.state);

        // 列0: 报警ID
        auto* idItem = new QTableWidgetItem(alarm.alarmId);
        idItem->setData(Qt::UserRole, alarm.tagId);
        idItem->setData(Qt::UserRole + 1, alarm.alarmId);
        idItem->setBackground(bgColor);
        m_alarmTable->setItem(row, 0, idItem);

        // 列1: 位号名
        auto* tagItem = new QTableWidgetItem(alarm.tagName);
        tagItem->setBackground(bgColor);
        m_alarmTable->setItem(row, 1, tagItem);

        // 列2: 限值类型
        auto* limitItem = new QTableWidgetItem(limitText(alarm.limit));
        limitItem->setBackground(bgColor);
        m_alarmTable->setItem(row, 2, limitItem);

        // 列3: 优先级
        auto* priItem = new QTableWidgetItem(priorityText(alarm.priority));
        priItem->setBackground(bgColor);
        m_alarmTable->setItem(row, 3, priItem);

        // 列4: 分类
        auto* clsItem = new QTableWidgetItem(classificationText(alarm.classification));
        clsItem->setBackground(bgColor);
        m_alarmTable->setItem(row, 4, clsItem);

        // 列5: 状态
        auto* stateItem = new QTableWidgetItem(alarmStateText(alarm.state));
        stateItem->setBackground(bgColor);
        m_alarmTable->setItem(row, 5, stateItem);

        // 列6: 当前值
        m_alarmTable->setItem(row, 6, new QTableWidgetItem(
            QString::number(alarm.triggerValue, 'f', 1)));

        // 列7: 限值
        m_alarmTable->setItem(row, 7, new QTableWidgetItem(
            QString::number(alarm.thresholdValue, 'f', 1)));

        // 列8: 区域
        m_alarmTable->setItem(row, 8, new QTableWidgetItem(alarm.area));

        // 列9: 触发时间
        m_alarmTable->setItem(row, 9, new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(alarm.triggerTime).toString("HH:mm:ss")));
    }
}

// ============================================================
// KPI 仪表盘页面
//
// 基于 EEMUA 191 标准的报警系统性能指标仪表盘。
// 核心指标：
// - 系统健康评分（0-100）
// - 10分钟报警率（EEMUA 191: ≤10为可管理）
// - 平均每小时报警率
// - 峰值报警率
// - 陈旧报警百分比
// - 报警洪峰次数
// - 颤振报警次数
// - Top5 频发报警
// - 各优先级分布
// - 屏蔽/抑制计数
// ============================================================
QWidget* MYDSCProject::createKpiDashboardPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(page);
    mainLayout->setSpacing(12);

    // ---- 顶部：系统健康评分 ----
    QGroupBox* healthGroup = new QGroupBox(QStringLiteral("系统健康评分"), page);
    QHBoxLayout* healthLayout = new QHBoxLayout(healthGroup);

    m_kpiHealthScore = new QLabel(QStringLiteral("--"), page);
    m_kpiHealthScore->setAlignment(Qt::AlignCenter);
    m_kpiHealthScore->setStyleSheet(
        "font-size: 48px; font-weight: bold; color: #4CAF50;");

    m_kpiHealthBar = new QProgressBar(page);
    m_kpiHealthBar->setRange(0, 100);
    m_kpiHealthBar->setValue(0);
    m_kpiHealthBar->setFormat("%v%");
    m_kpiHealthBar->setFixedHeight(30);
    m_kpiHealthBar->setStyleSheet(
        "QProgressBar { border: 1px solid #555; border-radius: 4px; text-align: center; }"
        "QProgressBar::chunk { background: #4CAF50; border-radius: 3px; }");

    m_kpiHealthGrade = new QLabel(QStringLiteral("等级: --"), page);
    m_kpiHealthGrade->setStyleSheet("font-size: 18px; font-weight: bold;");

    healthLayout->addWidget(m_kpiHealthScore);
    healthLayout->addWidget(m_kpiHealthBar, 1);
    healthLayout->addWidget(m_kpiHealthGrade);
    mainLayout->addWidget(healthGroup);

    // ---- 中部：核心KPI指标 ----
    QGroupBox* metricsGroup = new QGroupBox(QStringLiteral("核心KPI指标 (EEMUA 191)"), page);
    QGridLayout* metricsLayout = new QGridLayout(metricsGroup);

    // 10分钟报警率
    metricsLayout->addWidget(new QLabel(QStringLiteral("10分钟报警率:"), page), 0, 0);
    m_kpiAlarmRate10min = new QLabel(QStringLiteral("-- /10min"), page);
    m_kpiAlarmRate10min->setStyleSheet("font-size: 16px; font-weight: bold;");
    metricsLayout->addWidget(m_kpiAlarmRate10min, 0, 1);

    // 平均每小时报警率
    metricsLayout->addWidget(new QLabel(QStringLiteral("平均报警率:"), page), 0, 2);
    m_kpiAvgPerHour = new QLabel(QStringLiteral("-- /hr"), page);
    m_kpiAvgPerHour->setStyleSheet("font-size: 16px; font-weight: bold;");
    metricsLayout->addWidget(m_kpiAvgPerHour, 0, 3);

    // 峰值报警率
    metricsLayout->addWidget(new QLabel(QStringLiteral("峰值报警率:"), page), 1, 0);
    m_kpiPeakRate = new QLabel(QStringLiteral("-- /10min"), page);
    m_kpiPeakRate->setStyleSheet("font-size: 16px; font-weight: bold;");
    metricsLayout->addWidget(m_kpiPeakRate, 1, 1);

    // 陈旧报警百分比
    metricsLayout->addWidget(new QLabel(QStringLiteral("陈旧报警:"), page), 1, 2);
    m_kpiStalePercent = new QLabel(QStringLiteral("-- %"), page);
    m_kpiStalePercent->setStyleSheet("font-size: 16px; font-weight: bold;");
    metricsLayout->addWidget(m_kpiStalePercent, 1, 3);

    // 报警洪峰次数
    metricsLayout->addWidget(new QLabel(QStringLiteral("报警洪峰次数:"), page), 2, 0);
    m_kpiFloodCount = new QLabel(QStringLiteral("--"), page);
    m_kpiFloodCount->setStyleSheet("font-size: 16px; font-weight: bold;");
    metricsLayout->addWidget(m_kpiFloodCount, 2, 1);

    // 颤振报警次数
    metricsLayout->addWidget(new QLabel(QStringLiteral("颤振报警次数:"), page), 2, 2);
    m_kpiChatteringCount = new QLabel(QStringLiteral("--"), page);
    m_kpiChatteringCount->setStyleSheet("font-size: 16px; font-weight: bold;");
    metricsLayout->addWidget(m_kpiChatteringCount, 2, 3);

    mainLayout->addWidget(metricsGroup);

    // ---- 底部左：优先级分布 ----
    QGroupBox* priorityGroup = new QGroupBox(QStringLiteral("优先级分布"), page);
    QVBoxLayout* priLayout = new QVBoxLayout(priorityGroup);

    m_kpiCriticalCount = new QLabel(QStringLiteral("Critical: --"), page);
    m_kpiCriticalCount->setStyleSheet("font-size: 14px; color: #ff4444; font-weight: bold;");
    m_kpiMajorCount = new QLabel(QStringLiteral("Major: --"), page);
    m_kpiMajorCount->setStyleSheet("font-size: 14px; color: #ff8800; font-weight: bold;");
    m_kpiMinorCount = new QLabel(QStringLiteral("Minor: --"), page);
    m_kpiMinorCount->setStyleSheet("font-size: 14px; color: #ffcc00; font-weight: bold;");
    m_kpiAdvisoryCount = new QLabel(QStringLiteral("Advisory: --"), page);
    m_kpiAdvisoryCount->setStyleSheet("font-size: 14px; color: #4488ff; font-weight: bold;");

    priLayout->addWidget(m_kpiCriticalCount);
    priLayout->addWidget(m_kpiMajorCount);
    priLayout->addWidget(m_kpiMinorCount);
    priLayout->addWidget(m_kpiAdvisoryCount);

    // 屏蔽/抑制统计
    priLayout->addSpacing(10);
    m_kpiShelvedCount = new QLabel(QStringLiteral("已屏蔽: --"), page);
    m_kpiShelvedCount->setStyleSheet("font-size: 14px; color: #888;");
    m_kpiSuppressedCount = new QLabel(QStringLiteral("已抑制: --"), page);
    m_kpiSuppressedCount->setStyleSheet("font-size: 14px; color: #888;");
    priLayout->addWidget(m_kpiShelvedCount);
    priLayout->addWidget(m_kpiSuppressedCount);
    priLayout->addStretch();

    // ---- 底部右：Top5 频发报警 ----
    QGroupBox* top5Group = new QGroupBox(QStringLiteral("Top5 频发报警 (Bad Actor)"), page);
    QVBoxLayout* top5Layout = new QVBoxLayout(top5Group);
    m_kpiTop5Frequent = new QLabel(QStringLiteral("暂无数据"), page);
    m_kpiTop5Frequent->setWordWrap(true);
    m_kpiTop5Frequent->setStyleSheet("font-size: 13px;");
    top5Layout->addWidget(m_kpiTop5Frequent);
    top5Layout->addStretch();

    // 底部布局
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(priorityGroup, 1);
    bottomLayout->addWidget(top5Group, 2);
    mainLayout->addLayout(bottomLayout);

    // 刷新按钮
    QPushButton* refreshKpiBtn = new QPushButton(QStringLiteral("🔄 刷新KPI"), page);
    refreshKpiBtn->setFixedHeight(32);
    connect(refreshKpiBtn, &QPushButton::clicked, this, &MYDSCProject::onRefreshKpiDashboard);
    mainLayout->addWidget(refreshKpiBtn, 0, Qt::AlignRight);

    return page;
}

// ============================================================
// 变更日志查看页面
//
// ISA-18.2 要求所有报警参数变更必须有审计追踪记录。
// 包括：报警限值修改、优先级变更、屏蔽/抑制操作、
// 注释添加、审批流程等。
// ============================================================
QWidget* MYDSCProject::createChangeLogPage()
{
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);

    // 工具栏
    QHBoxLayout* toolbarLayout = new QHBoxLayout();

    QPushButton* refreshBtn = new QPushButton(QStringLiteral("🔄 刷新"), page);
    refreshBtn->setFixedWidth(70);
    connect(refreshBtn, &QPushButton::clicked, this, &MYDSCProject::refreshChangeLog);
    toolbarLayout->addWidget(refreshBtn);

    toolbarLayout->addStretch();

    QLabel* tipLabel = new QLabel(
        QStringLiteral("ISA-18.2 审计追踪：记录所有报警参数变更、操作员操作和审批流程"), page);
    tipLabel->setStyleSheet("color: #888; font-size: 12px;");
    toolbarLayout->addWidget(tipLabel);

    layout->addLayout(toolbarLayout);

    // 变更日志表格
    m_changeLogTable = new QTableWidget(0, 7, page);
    m_changeLogTable->setHorizontalHeaderLabels({
        QStringLiteral("时间"), QStringLiteral("操作人"), QStringLiteral("变更类型"),
        QStringLiteral("报警ID"), QStringLiteral("位号"), QStringLiteral("变更前"),
        QStringLiteral("变更后")
    });
    m_changeLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_changeLogTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_changeLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_changeLogTable->setAlternatingRowColors(true);
    m_changeLogTable->horizontalHeader()->setStretchLastSection(true);
    m_changeLogTable->verticalHeader()->setVisible(false);
    m_changeLogTable->setColumnWidth(0, 140);
    m_changeLogTable->setColumnWidth(1, 80);
    m_changeLogTable->setColumnWidth(2, 100);
    m_changeLogTable->setColumnWidth(3, 140);
    m_changeLogTable->setColumnWidth(4, 80);
    m_changeLogTable->setColumnWidth(5, 120);

    layout->addWidget(m_changeLogTable);

    return page;
}

// ============================================================
// 刷新变更日志列表
// ============================================================
void MYDSCProject::refreshChangeLog()
{
    if (!m_changeLogTable) return;

    // 从数据库读取变更记录（ISA-18.2 Level 4 审计追踪）
    auto records = DatabaseManager::instance().queryChangeRecords(0, 100);

    m_changeLogTable->setRowCount(0);

    for (const auto& rec : records) {
        int row = m_changeLogTable->rowCount();
        m_changeLogTable->insertRow(row);

        // 时间
        m_changeLogTable->setItem(row, 0, new QTableWidgetItem(
            QDateTime::fromMSecsSinceEpoch(rec.changeTime).toString("yyyy-MM-dd HH:mm:ss")));

        // 操作人
        m_changeLogTable->setItem(row, 1, new QTableWidgetItem(rec.operatorName));

        // 变更类型（字段名作为变更类型）
        m_changeLogTable->setItem(row, 2, new QTableWidgetItem(rec.fieldName));

        // 报警ID（用tagId代替）
        m_changeLogTable->setItem(row, 3, new QTableWidgetItem(QString::number(rec.tagId)));

        // 位号名（从TagConfigMgr查询）
        TagInfo tagInfo = TagConfigMgr::instance().getTag(rec.tagId);
        m_changeLogTable->setItem(row, 4, new QTableWidgetItem(tagInfo.tagName));

        // 变更前
        m_changeLogTable->setItem(row, 5, new QTableWidgetItem(rec.oldValue));

        // 变更后
        m_changeLogTable->setItem(row, 6, new QTableWidgetItem(rec.newValue));
    }
}

// ============================================================
// KPI 仪表盘刷新
//
// 从 AlarmKpiMonitor 获取实时KPI数据并更新UI。
// EEMUA 191 标准关键阈值：
// - 10分钟报警率 ≤ 10: 可管理（绿色）
// - 10分钟报警率 10-20: 可容忍（黄色）
// - 10分钟报警率 > 20: 过载（红色）
// ============================================================
void MYDSCProject::onRefreshKpiDashboard()
{
    auto kpi = AlarmEngine::instance().kpiSnapshot();

    // 系统健康评分
    int score = qBound(0, static_cast<int>(kpi.systemHealthScore), 100);
    m_kpiHealthScore->setText(QString::number(score));
    m_kpiHealthBar->setValue(score);

    // 健康评分颜色和等级
    QString scoreColor;
    QString grade;
    if (score >= 80) {
        scoreColor = "#4CAF50";
        grade = QStringLiteral("A - 优秀");
    } else if (score >= 60) {
        scoreColor = "#8BC34A";
        grade = QStringLiteral("B - 良好");
    } else if (score >= 40) {
        scoreColor = "#FFC107";
        grade = QStringLiteral("C - 一般");
    } else if (score >= 20) {
        scoreColor = "#FF9800";
        grade = QStringLiteral("D - 较差");
    } else {
        scoreColor = "#F44336";
        grade = QStringLiteral("F - 危险");
    }

    m_kpiHealthScore->setStyleSheet(
        QString("font-size: 48px; font-weight: bold; color: %1;").arg(scoreColor));
    m_kpiHealthBar->setStyleSheet(
        QString("QProgressBar { border: 1px solid #555; border-radius: 4px; text-align: center; }"
                "QProgressBar::chunk { background: %1; border-radius: 3px; }").arg(scoreColor));
    m_kpiHealthGrade->setText(QStringLiteral("等级: ") + grade);
    m_kpiHealthGrade->setStyleSheet(
        QString("font-size: 18px; font-weight: bold; color: %1;").arg(scoreColor));

    // 10分钟报警率（EEMUA 191 核心指标）
    double rate10min = kpi.alarmCount10min;
    QString rate10Color;
    if (rate10min <= 10.0) {
        rate10Color = "#4CAF50";
    } else if (rate10min <= 20.0) {
        rate10Color = "#FFC107";
    } else {
        rate10Color = "#F44336";
    }
    m_kpiAlarmRate10min->setText(QString("%1 /10min").arg(rate10min, 0, 'f', 1));
    m_kpiAlarmRate10min->setStyleSheet(
        QString("font-size: 16px; font-weight: bold; color: %1;").arg(rate10Color));

    // 平均每小时报警率
    m_kpiAvgPerHour->setText(QString("%1 /hr").arg(kpi.avgPerHour, 0, 'f', 1));

    // 峰值报警率
    m_kpiPeakRate->setText(QString("%1 /10min").arg(kpi.peakCount10min));

    // 陈旧报警百分比
    double stalePct = kpi.staleAlarmPercent;
    QString staleColor = (stalePct > 5.0) ? "#F44336" : "#4CAF50";
    m_kpiStalePercent->setText(QString("%1 %").arg(stalePct, 0, 'f', 1));
    m_kpiStalePercent->setStyleSheet(
        QString("font-size: 16px; font-weight: bold; color: %1;").arg(staleColor));

    // 报警洪峰次数
    m_kpiFloodCount->setText(QString::number(kpi.floodEventCount));

    // 颤振报警次数
    m_kpiChatteringCount->setText(QString::number(kpi.chatteringCount));

    // 优先级分布
    m_kpiCriticalCount->setText(QString("Critical: %1").arg(kpi.criticalCount));
    m_kpiMajorCount->setText(QString("Major: %1").arg(kpi.majorCount));
    m_kpiMinorCount->setText(QString("Minor: %1").arg(kpi.minorCount));
    m_kpiAdvisoryCount->setText(QString("Advisory: %1").arg(kpi.advisoryCount));

    // 屏蔽/抑制统计
    m_kpiShelvedCount->setText(QString("已屏蔽: %1").arg(kpi.shelvedCount));
    m_kpiSuppressedCount->setText(QString("已抑制: %1").arg(kpi.suppressedCount));

    // Top5 频发报警（Bad Actor 分析）
    auto topAlarms = AlarmEngine::instance().topFrequentAlarms(5);
    QString top5Text;
    int rank = 1;
    for (const auto& [tagId, count] : topAlarms) {
        TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
        QString name = tagInfo.tagName.isEmpty() ? QString::number(tagId) : tagInfo.tagName;
        top5Text += QString("%1. %2 — 触发 %3 次\n").arg(rank++).arg(name).arg(count);
    }
    if (top5Text.isEmpty()) {
        top5Text = QStringLiteral("暂无频发报警数据");
    }
    m_kpiTop5Frequent->setText(top5Text.trimmed());
}

// ============================================================
// ISA-18.2 报警屏蔽（Shelve）
//
// 操作员可临时屏蔽一个报警，使其在设定时间内不再显示。
// 屏蔽需要指定持续时间（默认1小时），到期自动解除。
// 屏蔽操作需要操作员权限，且会被审计追踪记录。
// ============================================================
void MYDSCProject::onShelveAlarm(quint32 tagId)
{
    if (!AuthManager::instance().hasPermission(UserLevel::Operator)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("需要操作员权限才能屏蔽报警。"));
        return;
    }

    // 屏蔽时长选择对话框
    QDialog shelveDialog(this);
    shelveDialog.setWindowTitle(QStringLiteral("屏蔽报警 (ISA-18.2 Shelve)"));
    shelveDialog.setMinimumWidth(320);

    QVBoxLayout* layout = new QVBoxLayout(&shelveDialog);

    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    layout->addWidget(new QLabel(
        QStringLiteral("位号: %1 (ID: %2)").arg(tagInfo.tagName).arg(tagId), &shelveDialog));

    layout->addWidget(new QLabel(
        QStringLiteral("选择屏蔽持续时间:"), &shelveDialog));

    QComboBox* durationCombo = new QComboBox(&shelveDialog);
    durationCombo->addItem(QStringLiteral("15 分钟"), 15);
    durationCombo->addItem(QStringLiteral("30 分钟"), 30);
    durationCombo->addItem(QStringLiteral("1 小时"), 60);
    durationCombo->addItem(QStringLiteral("2 小时"), 120);
    durationCombo->addItem(QStringLiteral("4 小时"), 240);
    durationCombo->addItem(QStringLiteral("8 小时（整个班次）"), 480);
    durationCombo->setCurrentIndex(2);
    layout->addWidget(durationCombo);

    // 屏蔽原因
    layout->addWidget(new QLabel(QStringLiteral("屏蔽原因:"), &shelveDialog));
    QTextEdit* reasonEdit = new QTextEdit(&shelveDialog);
    reasonEdit->setMaximumHeight(80);
    reasonEdit->setPlaceholderText(QStringLiteral("输入屏蔽原因（可选）"));
    layout->addWidget(reasonEdit);

    // 确认/取消按钮
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &shelveDialog);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &shelveDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &shelveDialog, &QDialog::reject);

    if (shelveDialog.exec() == QDialog::Accepted) {
        int durationMin = durationCombo->currentData().toInt();
        QString reason = reasonEdit->toPlainText().trimmed();

        // 调用AlarmEngine屏蔽
        AlarmEngine::instance().shelveAlarm(tagId, durationMin);

        LOG_INFO("MainWindow",
            QString("报警屏蔽: tagId=%1, 时长=%2min, 原因=%3")
                .arg(tagId).arg(durationMin).arg(reason));

        // 刷新报警汇总
        refreshAlarmSummary();

        QMessageBox::information(this, QStringLiteral("屏蔽成功"),
            QStringLiteral("报警已屏蔽 %1 分钟。\n到期后将自动解除。").arg(durationMin));
    }
}

// ============================================================
// 取消报警屏蔽
// ============================================================
void MYDSCProject::onUnshelveAlarm(quint32 tagId)
{
    if (!AuthManager::instance().hasPermission(UserLevel::Operator)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("需要操作员权限才能取消屏蔽。"));
        return;
    }

    AlarmEngine::instance().unshelveAlarm(tagId);
    LOG_INFO("MainWindow", QString("取消报警屏蔽: tagId=%1").arg(tagId));
    refreshAlarmSummary();
}

// ============================================================
// ISA-18.2 设计抑制（Suppression-by-Design）
//
// 设计抑制是工程师级别的操作，用于在特定工况下
// 永久性抑制某个报警。例如：
// - 设备检修期间抑制相关报警
// - 工艺变更后抑制不再适用的报警
// 需要工程师权限，且必须记录抑制原因。
// ============================================================
void MYDSCProject::onSuppressAlarm(quint32 tagId)
{
    if (!AuthManager::instance().hasPermission(UserLevel::Engineer)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("需要工程师权限才能进行设计抑制。"));
        return;
    }

    if (!AuthManager::instance().confirmCriticalAction(
            QStringLiteral("设计抑制报警"), QStringLiteral("永久抑制该报警，需工程师审批")))
        return;

    // 抑制原因对话框
    QDialog suppressDialog(this);
    suppressDialog.setWindowTitle(QStringLiteral("设计抑制 (ISA-18.2 Suppression-by-Design)"));
    suppressDialog.setMinimumWidth(360);

    QVBoxLayout* layout = new QVBoxLayout(&suppressDialog);

    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    layout->addWidget(new QLabel(
        QStringLiteral("⚠ 警告：设计抑制将永久禁止该报警触发！"), &suppressDialog));
    layout->addWidget(new QLabel(
        QStringLiteral("位号: %1 (ID: %2)").arg(tagInfo.tagName).arg(tagId), &suppressDialog));

    layout->addWidget(new QLabel(QStringLiteral("抑制原因（必填）:"), &suppressDialog));
    QTextEdit* reasonEdit = new QTextEdit(&suppressDialog);
    reasonEdit->setMaximumHeight(80);
    reasonEdit->setPlaceholderText(QStringLiteral("输入设计抑制原因"));
    layout->addWidget(reasonEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &suppressDialog);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &suppressDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &suppressDialog, &QDialog::reject);

    if (suppressDialog.exec() == QDialog::Accepted) {
        QString reason = reasonEdit->toPlainText().trimmed();
        if (reason.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("原因必填"),
                QStringLiteral("设计抑制必须填写原因。"));
            return;
        }

        AlarmEngine::instance().suppressAlarm(tagId, reason);

        LOG_WARN("MainWindow",
            QString("设计抑制: tagId=%1, 原因=%2").arg(tagId).arg(reason));

        refreshAlarmSummary();

        QMessageBox::information(this, QStringLiteral("抑制成功"),
            QStringLiteral("报警已被设计抑制。\n需要工程师权限才能解除。"));
    }
}

// ============================================================
// 取消设计抑制
// ============================================================
void MYDSCProject::onUnsuppressAlarm(quint32 tagId)
{
    if (!AuthManager::instance().hasPermission(UserLevel::Engineer)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("需要工程师权限才能取消设计抑制。"));
        return;
    }

    if (!AuthManager::instance().confirmCriticalAction(
            QStringLiteral("取消设计抑制"), QStringLiteral("恢复该报警的正常触发")))
        return;

    AlarmEngine::instance().unsuppressAlarm(tagId);
    LOG_INFO("MainWindow", QString("取消设计抑制: tagId=%1").arg(tagId));
    refreshAlarmSummary();
}

// ============================================================
// ISA-18.2 设备停用（Out-of-Service）
//
// 当设备进入停用状态时，相关报警不再触发。
// 典型场景：设备检修、设备报废、临时停机。
// 需要工程师权限，且需要记录停用原因。
// ============================================================
void MYDSCProject::onSetOutOfService(quint32 tagId)
{
    if (!AuthManager::instance().hasPermission(UserLevel::Engineer)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("需要工程师权限才能设置设备停用。"));
        return;
    }

    if (!AuthManager::instance().confirmCriticalAction(
            QStringLiteral("设备停用"), QStringLiteral("将设备设为停用状态，相关报警将不再触发")))
        return;

    // 停用原因对话框
    QDialog oosDialog(this);
    oosDialog.setWindowTitle(QStringLiteral("设备停用 (ISA-18.2 Out-of-Service)"));
    oosDialog.setMinimumWidth(360);

    QVBoxLayout* layout = new QVBoxLayout(&oosDialog);

    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    layout->addWidget(new QLabel(
        QStringLiteral("⚠ 设备停用后，相关报警将不再触发！"), &oosDialog));
    layout->addWidget(new QLabel(
        QStringLiteral("位号: %1 (ID: %2)").arg(tagInfo.tagName).arg(tagId), &oosDialog));

    layout->addWidget(new QLabel(QStringLiteral("停用原因（必填）:"), &oosDialog));
    QTextEdit* reasonEdit = new QTextEdit(&oosDialog);
    reasonEdit->setMaximumHeight(80);
    reasonEdit->setPlaceholderText(QStringLiteral("输入停用原因"));
    layout->addWidget(reasonEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &oosDialog);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &oosDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &oosDialog, &QDialog::reject);

    if (oosDialog.exec() == QDialog::Accepted) {
        QString reason = reasonEdit->toPlainText().trimmed();
        if (reason.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("原因必填"),
                QStringLiteral("设备停用必须填写原因。"));
            return;
        }

        AlarmEngine::instance().setOutOfService(tagId, reason);

        LOG_WARN("MainWindow",
            QString("设备停用: tagId=%1, 原因=%2").arg(tagId).arg(reason));

        refreshAlarmSummary();

        QMessageBox::information(this, QStringLiteral("停用成功"),
            QStringLiteral("设备已设为停用状态。\n需要工程师权限才能恢复。"));
    }
}

// ============================================================
// 恢复服务
// ============================================================
void MYDSCProject::onReturnToService(quint32 tagId)
{
    if (!AuthManager::instance().hasPermission(UserLevel::Engineer)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("需要工程师权限才能恢复设备服务。"));
        return;
    }

    if (!AuthManager::instance().confirmCriticalAction(
            QStringLiteral("恢复服务"), QStringLiteral("恢复设备的正常报警功能")))
        return;

    AlarmEngine::instance().returnToService(tagId);
    LOG_INFO("MainWindow", QString("恢复服务: tagId=%1").arg(tagId));
    refreshAlarmSummary();
}

// ============================================================
// ISA-18.2 操作员注释（Operator Annotation）
//
// 操作员可以为任何报警添加注释，记录操作决策和现场情况。
// 这是 ISA-18.2 审计追踪的重要组成部分。
// 注释内容包括：
// - 操作员对报警的判断
// - 采取的应对措施
// - 现场观察到的异常情况
// ============================================================
void MYDSCProject::onAnnotateAlarm(const QString& alarmId)
{
    if (!AuthManager::instance().hasPermission(UserLevel::Operator)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("需要操作员权限才能添加注释。"));
        return;
    }

    QDialog annotateDialog(this);
    annotateDialog.setWindowTitle(QStringLiteral("添加注释 (ISA-18.2 Annotation)"));
    annotateDialog.setMinimumWidth(400);
    annotateDialog.setMinimumHeight(300);

    QVBoxLayout* layout = new QVBoxLayout(&annotateDialog);

    layout->addWidget(new QLabel(
        QStringLiteral("报警ID: %1").arg(alarmId), &annotateDialog));

    layout->addWidget(new QLabel(QStringLiteral("注释内容:"), &annotateDialog));
    QTextEdit* annotationEdit = new QTextEdit(&annotateDialog);
    annotationEdit->setPlaceholderText(
        QStringLiteral("输入注释内容，例如：\n"
                       "- 已通知现场巡检人员\n"
                       "- 原因：上游装置波动导致\n"
                       "- 已采取：调整PID参数"));
    layout->addWidget(annotationEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &annotateDialog);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &annotateDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &annotateDialog, &QDialog::reject);

    if (annotateDialog.exec() == QDialog::Accepted) {
        QString annotation = annotationEdit->toPlainText().trimmed();
        if (annotation.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("注释为空"),
                QStringLiteral("请输入注释内容。"));
            return;
        }

        AlarmEngine::instance().annotateAlarm(alarmId, annotation);

        LOG_INFO("MainWindow",
            QString("报警注释: alarmId=%1, 内容=%2").arg(alarmId).arg(annotation));
    }
}

// ============================================================
// 确认恢复报警（RTN Unack → Normal）
//
// ISA-18.2 状态机：当报警从 ReturnToNormalUnacknowledged
// 确认后，状态变为 ReturnToNormalAcknowledged，
// 然后自动转为 Normal（从活跃列表移除）。
// ============================================================
void MYDSCProject::onAcknowledgeReturnToNormal(const QString& alarmId)
{
    if (!AuthManager::instance().hasPermission(UserLevel::Operator)) {
        QMessageBox::warning(this, QStringLiteral("权限不足"),
            QStringLiteral("需要操作员权限才能确认报警。"));
        return;
    }

    AlarmEngine::instance().acknowledgeAlarm(alarmId);
    LOG_INFO("MainWindow", QString("确认恢复报警: alarmId=%1").arg(alarmId));
    refreshAlarmSummary();
}

// ============================================================
// 报警状态显示文本
// ============================================================
QString MYDSCProject::alarmStateText(AlarmState state) const
{
    switch (state) {
    case AlarmState::Normal:                        return QStringLiteral("正常");
    case AlarmState::ActiveUnacknowledged:          return QStringLiteral("活跃(未确认)");
    case AlarmState::ActiveAcknowledged:            return QStringLiteral("活跃(已确认)");
    case AlarmState::ReturnToNormalUnacknowledged:  return QStringLiteral("恢复(未确认)");
    case AlarmState::ReturnToNormalAcknowledged:    return QStringLiteral("恢复(已确认)");
    case AlarmState::Shelved:                       return QStringLiteral("已屏蔽");
    case AlarmState::SuppressedByDesign:            return QStringLiteral("设计抑制");
    case AlarmState::OutOfService:                  return QStringLiteral("停用");
    default:                                        return QStringLiteral("未知");
    }
}

// ============================================================
// 优先级显示文本
// ============================================================
QString MYDSCProject::priorityText(AlarmPriority priority) const
{
    switch (priority) {
    case AlarmPriority::Critical: return QStringLiteral("Critical");
    case AlarmPriority::Major:    return QStringLiteral("Major");
    case AlarmPriority::Minor:    return QStringLiteral("Minor");
    case AlarmPriority::Advisory: return QStringLiteral("Advisory");
    default:                      return QStringLiteral("--");
    }
}

// ============================================================
// 报警限值显示文本
// ============================================================
QString MYDSCProject::limitText(AlarmLimit limit) const
{
    switch (limit) {
    case AlarmLimit::HighHigh:     return QStringLiteral("HH");
    case AlarmLimit::High:         return QStringLiteral("H");
    case AlarmLimit::Low:          return QStringLiteral("L");
    case AlarmLimit::LowLow:       return QStringLiteral("LL");
    case AlarmLimit::Deviation:    return QStringLiteral("DEV");
    case AlarmLimit::RateOfChange: return QStringLiteral("ROC");
    default:                       return QStringLiteral("--");
    }
}

// ============================================================
// 报警分类显示文本
// ============================================================
QString MYDSCProject::classificationText(AlarmClassification cls) const
{
    switch (cls) {
    case AlarmClassification::Process:      return QStringLiteral("工艺");
    case AlarmClassification::Safety:       return QStringLiteral("安全");
    case AlarmClassification::Machinery:    return QStringLiteral("设备");
    case AlarmClassification::Environmental: return QStringLiteral("环境");
    default:                                return QStringLiteral("--");
    }
}

// ============================================================
// 报警状态背景色
//
// ISA-18.2 标准颜色方案：
// - 活跃未确认：红色闪烁（最紧急）
// - 活跃已确认：红色稳态
// - 恢复未确认：黄色
// - 恢复已确认：绿色
// - 屏蔽：灰色
// - 抑制：深灰色
// - 停用：深灰色斜线
// ============================================================
QColor MYDSCProject::alarmStateColor(AlarmState state) const
{
    switch (state) {
    case AlarmState::ActiveUnacknowledged:          return QColor(255, 100, 100);
    case AlarmState::ActiveAcknowledged:            return QColor(255, 180, 180);
    case AlarmState::ReturnToNormalUnacknowledged:  return QColor(255, 255, 150);
    case AlarmState::ReturnToNormalAcknowledged:    return QColor(180, 255, 180);
    case AlarmState::Shelved:                       return QColor(200, 200, 200);
    case AlarmState::SuppressedByDesign:            return QColor(180, 180, 180);
    case AlarmState::OutOfService:                  return QColor(160, 160, 160);
    default:                                        return QColor(255, 255, 255);
    }
}
