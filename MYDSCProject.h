#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_MYDSCProject.h"

#include <QTreeWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QLabel>
#include <QTabWidget>
#include <QDockWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QTextEdit>
#include <QScrollBar>
#include <QSlider>
#include <QComboBox>
#include <QProgressBar>
#include <QSpinBox>
#include <QGroupBox>
#include <QGridLayout>

#include "logger.h"
#include "DataEngine.h"
#include "AlarmEngine.h"
#include "AuthManager.h"
#include "PidScene.h"
#include "PidView.h"
#include "PerformanceMonitor.h"
#include "TrendWidget.h"

class MYDSCProject : public QMainWindow
{
    Q_OBJECT

public:
    MYDSCProject(QWidget *parent = nullptr);
    ~MYDSCProject();

private slots:
    // 文件菜单
    void onOpenProject();
    void onSaveProject();
    void onExportData();

    // 视图菜单
    void onToggleFullScreen();
    void onToggleLeftDock();
    void onToggleRightDock();

    // 工具菜单
    void onConnectAll();
    void onDisconnectAll();
    void onAcknowledgeAllAlarms();
    void onToggleSound();
    void onShowPerformanceMonitor();
    void onShowTagConfig();
    void onGenerateReport();

    // 帮助菜单
    void onAbout();

    // 数据更新
    void onDataUpdated();
    void onAlarmTriggered(quint32 tagId, AlarmLimit state, float value, float limit);
    void onAlarmCleared(quint32 tagId);
    void onAlarmCountChanged(int count);

    // 设备状态
    void onDeviceStatusChanged(int deviceId, bool connected);
    void onCommStatusChanged(bool connected);

    // 用户登录
    void onUserLoggedIn(const QString& username, UserLevel level);
    void onUserLoggedOut();
    void onLoginAction();

    // 定时刷新
    void onRefreshTimer();

    // 报警确认
    void onAcknowledgeAlarm();

    // 上下文菜单
    void onAlarmTableContextMenu(const QPoint& pos);
    void onDeviceTreeContextMenu(const QPoint& pos);
    void onPidViewContextMenu(const QPoint& pos);

    // 演示模式
    void onToggleDemo();

    // P&ID 缩放
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomSliderChanged(int value);

    // === ISA-18.2 报警操作槽 ===

    /// 报警过滤条件变更
    void onAlarmFilterChanged();

    /// 屏蔽报警（ISA-18.2 Shelve）
    void onShelveAlarm(quint32 tagId);

    /// 取消屏蔽
    void onUnshelveAlarm(quint32 tagId);

    /// 设计抑制（ISA-18.2 Suppression-by-Design）
    void onSuppressAlarm(quint32 tagId);

    /// 取消抑制
    void onUnsuppressAlarm(quint32 tagId);

    /// 设备停用（ISA-18.2 Out-of-Service）
    void onSetOutOfService(quint32 tagId);

    /// 恢复服务
    void onReturnToService(quint32 tagId);

    /// 添加操作员注释（ISA-18.2 Operator Annotation）
    void onAnnotateAlarm(const QString& alarmId);

    /// 确认恢复报警（RTN Unack → Normal）
    void onAcknowledgeReturnToNormal(const QString& alarmId);

    /// KPI仪表盘刷新
    void onRefreshKpiDashboard();

private:
    void setupMenuBar();
    void setupToolBar();
    void setupCentralWidget();
    void setupDockWidgets();
    void setupStatusBar();
    void setupConnections();
    void initEngines();
    void loadDemoData();

    void updateAlarmBar();
    void updateStatusBar();
    void updateDeviceTree();
    void updateDataTable();
    void updatePidItems();
    void appendLog(Log_Level level, const QString& model, const QString& message);
    void rebuildDataTable();
    void rebuildDeviceTree();
    void updateDeviceValues();

    // === ISA-18.2 UI 方法 ===

    /// 构建报警过滤工具栏
    void setupAlarmFilterBar();

    /// 构建KPI仪表盘页面
    QWidget* createKpiDashboardPage();

    /// 构建变更日志查看页面
    QWidget* createChangeLogPage();

    /// 刷新报警汇总列表（按过滤条件）
    void refreshAlarmSummary();

    /// 刷新变更日志列表
    void refreshChangeLog();

    /// 获取报警状态显示文本
    QString alarmStateText(AlarmState state) const;

    /// 获取优先级显示文本
    QString priorityText(AlarmPriority priority) const;

    /// 获取报警限值显示文本
    QString limitText(AlarmLimit limit) const;

    /// 获取报警分类显示文本
    QString classificationText(AlarmClassification cls) const;

    /// 获取报警状态背景色
    QColor alarmStateColor(AlarmState state) const;

    Ui::MYDSCProjectClass ui;

    // ==== 核心引擎 ====
    DataEngine* m_dataEngine = nullptr;

    // ==== 中央区域 ====
    QTabWidget* m_centralTabs = nullptr;
    PidScene* m_pidScene = nullptr;
    QGraphicsView* m_pidView = nullptr;
    QTableWidget* m_dataTable = nullptr;
    QTextEdit* m_logView = nullptr;
    TrendWidget* m_trendWidget = nullptr;
    QWidget* m_pidContainer = nullptr;
    QSlider* m_zoomSlider = nullptr;
    QLabel* m_zoomLabel = nullptr;

    // ==== ISA-18.2 KPI 仪表盘 ====
    QWidget* m_kpiPage = nullptr;
    QLabel* m_kpiHealthScore = nullptr;
    QProgressBar* m_kpiHealthBar = nullptr;
    QLabel* m_kpiHealthGrade = nullptr;
    QLabel* m_kpiAlarmRate10min = nullptr;
    QLabel* m_kpiAvgPerHour = nullptr;
    QLabel* m_kpiPeakRate = nullptr;
    QLabel* m_kpiStalePercent = nullptr;
    QLabel* m_kpiFloodCount = nullptr;
    QLabel* m_kpiChatteringCount = nullptr;
    QLabel* m_kpiTop5Frequent = nullptr;
    QLabel* m_kpiCriticalCount = nullptr;
    QLabel* m_kpiMajorCount = nullptr;
    QLabel* m_kpiMinorCount = nullptr;
    QLabel* m_kpiAdvisoryCount = nullptr;
    QLabel* m_kpiShelvedCount = nullptr;
    QLabel* m_kpiSuppressedCount = nullptr;

    // ==== ISA-18.2 变更日志 ====
    QWidget* m_changeLogPage = nullptr;
    QTableWidget* m_changeLogTable = nullptr;

    // ==== 左侧面板 ====
    QDockWidget* m_leftDock = nullptr;
    QTreeWidget* m_deviceTree = nullptr;

    // ==== 右侧面板（ISA-18.2 增强） ====
    QDockWidget* m_rightDock = nullptr;
    QTableWidget* m_alarmTable = nullptr;
    QWidget* m_alarmFilterBar = nullptr;
    QComboBox* m_filterPriority = nullptr;
    QComboBox* m_filterState = nullptr;
    QComboBox* m_filterClassification = nullptr;
    QComboBox* m_filterArea = nullptr;
    QComboBox* m_filterLimit = nullptr;

    // ==== 底部报警条 ====
    QLabel* m_alarmBar = nullptr;

    // ==== 工具栏Action ====
    QAction* m_actConnect = nullptr;
    QAction* m_actDisconnect = nullptr;
    QAction* m_actAckAll = nullptr;
    QAction* m_actSound = nullptr;
    QAction* m_actLogin = nullptr;

    // ==== 状态栏 ====
    QLabel* m_statusDevices = nullptr;
    QLabel* m_statusTags = nullptr;
    QLabel* m_statusAlarms = nullptr;
    QLabel* m_statusUser = nullptr;
    QLabel* m_statusTime = nullptr;

    // ==== 定时器 ====
    QTimer* m_refreshTimer = nullptr;
    QTimer* m_demoTimer = nullptr;
    QTimer* m_kpiRefreshTimer = nullptr;

    // ==== 状态 ====
    bool m_soundEnabled = true;
    bool m_isFullScreen = false;
    bool m_demoMode = false;
    int m_totalAlarms = 0;
    QAction* m_actDemo = nullptr;
};
