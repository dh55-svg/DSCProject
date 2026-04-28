#include "MainWindow.h"
#include "presentation/PidScene.h"
#include "presentation/TrendWidget.h"
#include "presentation/TagConfigDialog.h"
#include "domain/tag/TagManager.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QGraphicsView>
#include <QAction>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>

MainWindow::MainWindow(DataController& dataCtrl, AlarmController& alarmCtrl,
                        AuthController& authCtrl, ILogger* logger)
    : QMainWindow(nullptr), m_dataCtrl(dataCtrl), m_alarmCtrl(alarmCtrl), m_authCtrl(authCtrl), m_logger(logger)
{
    setWindowTitle("MYDSC - Distributed Control System v2.0");
    resize(1400, 900);

    setupMenuBar();
    setupToolBar();
    setupCentralWidget();
    setupDockWidgets();
    setupStatusBar();
    setupConnections();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(200);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    m_refreshTimer->start();

    if (m_logger) m_logger->info("主窗口初始化完成");
}

MainWindow::~MainWindow() {}

void MainWindow::setupMenuBar() {
    QMenuBar* mb = menuBar();

    QMenu* fileMenu = mb->addMenu("&File");
    fileMenu->addAction("Open Project...");
    fileMenu->addAction("Save Project...");
    fileMenu->addSeparator();
    fileMenu->addAction("Export Data...", this, &MainWindow::onExportData);
    fileMenu->addSeparator();
    fileMenu->addAction("Quit", qApp, &QApplication::quit, QKeySequence::Quit);

    QMenu* viewMenu = mb->addMenu("&View");
    viewMenu->addAction("Toggle Navigation Panel", this, [this]() {
        if (m_deviceDock) m_deviceDock->setVisible(!m_deviceDock->isVisible());
    }, QKeySequence("Ctrl+1"));
    viewMenu->addAction("Toggle Alarm Panel", this, [this]() {
        if (m_alarmDock) m_alarmDock->setVisible(!m_alarmDock->isVisible());
    }, QKeySequence("Ctrl+2"));

    QMenu* toolsMenu = mb->addMenu("&Tools");
    toolsMenu->addAction("Connect All", this, &MainWindow::onConnectAll, QKeySequence("Ctrl+L"));
    toolsMenu->addAction("Disconnect All", this, &MainWindow::onDisconnectAll, QKeySequence("Ctrl+D"));
    toolsMenu->addAction("Acknowledge All Alarms", this, &MainWindow::onAckAll, QKeySequence("Ctrl+K"));
    toolsMenu->addSeparator();
    toolsMenu->addAction("Performance Monitor...", this, &MainWindow::onShowPerformanceMonitor);
    toolsMenu->addAction("Tag Configuration...", this, &MainWindow::onShowTagConfig);

    QMenu* helpMenu = mb->addMenu("&Help");
    helpMenu->addAction("About");
}

void MainWindow::setupToolBar() {
    QToolBar* tb = addToolBar("Main");
    tb->addAction("Connect", this, &MainWindow::onConnectAll);
    tb->addAction("Disconnect", this, &MainWindow::onDisconnectAll);
    tb->addSeparator();
    tb->addAction("AckAll", this, &MainWindow::onAckAll);
    tb->addAction("Sound", this, &MainWindow::onToggleSound);
    tb->addSeparator();
    tb->addAction("Login", this, &MainWindow::onLogin);
    tb->addAction("Logout", this, &MainWindow::onLogout);
}

void MainWindow::setupCentralWidget() {
    m_tabWidget = new QTabWidget(this);

    m_pidScene = new PidScene(this);
    auto* pidView = new QGraphicsView(m_pidScene, this);
    pidView->setDragMode(QGraphicsView::ScrollHandDrag);
    pidView->setRenderHint(QPainter::Antialiasing);
    m_tabWidget->addTab(pidView, "P&ID");

    m_dataTable = new QTableWidget(0, 7, this);
    m_dataTable->setHorizontalHeaderLabels({"ID", "Name", "Description", "Value", "Unit", "Quality", "Alarm"});
    m_dataTable->horizontalHeader()->setStretchLastSection(true);
    m_tabWidget->addTab(m_dataTable, "Data Table");

    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet("background-color: #0a0a1a; color: #00ff00; font-family: monospace;");
    m_tabWidget->addTab(m_logView, "System Log");

    m_trendWidget = new TrendWidget(this);
    m_tabWidget->addTab(m_trendWidget, "Trends");

    setCentralWidget(m_tabWidget);
}

void MainWindow::setupDockWidgets() {
    m_deviceDock = new QDockWidget("Device Navigation", this);
    m_deviceTree = new QTreeWidget(m_deviceDock);
    m_deviceTree->setHeaderLabels({"Device/Name", "Status"});
    m_deviceDock->setWidget(m_deviceTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_deviceDock);

    m_alarmDock = new QDockWidget("Alarm Summary (ISA-18.2)", this);
    m_alarmTable = new QTableWidget(0, 10, m_alarmDock);
    m_alarmTable->setHorizontalHeaderLabels({"Alarm ID", "Tag", "Limit", "Priority", "Class", "State", "Value", "Threshold", "Area", "Time"});
    m_alarmDock->setWidget(m_alarmTable);
    addDockWidget(Qt::RightDockWidgetArea, m_alarmDock);
}

void MainWindow::setupStatusBar() {
    QStatusBar* sb = statusBar();
    m_statusDevices = new QLabel("Devices: 0/0", sb);
    m_statusTags = new QLabel("Tags: 0", sb);
    m_statusAlarms = new QLabel("Alarms: 0", sb);
    m_statusUser = new QLabel("User: --", sb);
    sb->addWidget(m_statusDevices);
    sb->addWidget(m_statusTags);
    sb->addWidget(m_statusAlarms);
    sb->addPermanentWidget(m_statusUser);
}

void MainWindow::setupConnections() {
    connect(&m_dataCtrl, &DataController::dataUpdated, this, &MainWindow::onDataUpdated);
    connect(&m_alarmCtrl, &AlarmController::alarmCountChanged, this, &MainWindow::onAlarmCountChanged);

    if (m_logger) {
        m_logger->setLogCallback([this](const QString& msg) {
            QMetaObject::invokeMethod(this, [this, msg]() { m_logView->append(msg); }, Qt::QueuedConnection);
        });
    }
}

void MainWindow::onRefreshTimer() {
    auto snap = m_dataCtrl.pipeline().doubleBuffer()->readAll();
    if (!snap) return;

    int tagCount = m_dataCtrl.tagManager().tagCount();
    m_statusTags->setText(QString("Tags: %1").arg(tagCount));

    auto& fieldbus = m_dataCtrl.pipeline();
    // Update device status via fieldbus interface
    m_statusDevices->setText(QString("Devices: %1/%2")
        .arg(m_dataCtrl.pipeline().fieldbus() ? m_dataCtrl.pipeline().fieldbus()->onlineDeviceCount() : 0)
        .arg(m_dataCtrl.pipeline().fieldbus() ? m_dataCtrl.pipeline().fieldbus()->totalDeviceCount() : 0));
}

void MainWindow::onDataUpdated() {
    // Handle data refresh
}

void MainWindow::onAlarmCountChanged(int active, int unack) {
    m_statusAlarms->setText(QString("Alarms: %1 (%2 unack)").arg(active).arg(unack));
}

void MainWindow::onConnectAll() { m_dataCtrl.connectAll(); }
void MainWindow::onDisconnectAll() { m_dataCtrl.disconnectAll(); }

void MainWindow::onAckAll() {
    if (m_authCtrl.isLoggedIn())
        m_alarmCtrl.acknowledgeAll(m_authCtrl.currentUser());
    else
        m_alarmCtrl.acknowledgeAll();
}

void MainWindow::onToggleSound() {
    // Toggle sound enabled in alarm engine
}

void MainWindow::onLogin() {
    bool ok;
    QString user = QInputDialog::getText(this, "Login", "Username:", QLineEdit::Normal, "", &ok);
    if (!ok || user.isEmpty()) return;
    QString pass = QInputDialog::getText(this, "Login", "Password:", QLineEdit::Password, "", &ok);
    if (!ok) return;
    if (!m_authCtrl.login(user, pass))
        QMessageBox::warning(this, "Login Failed", "Invalid username or password.");
    else
        m_statusUser->setText(QString("User: %1").arg(m_authCtrl.currentUser()));
}

void MainWindow::onLogout() {
    m_authCtrl.logout();
    m_statusUser->setText("User: --");
}

void MainWindow::onExportData() {
    // Export functionality
    QMessageBox::information(this, "Export", "Export functionality - to be implemented.");
}

void MainWindow::onShowTagConfig() {
    if (!m_authCtrl.canConfigure()) {
        QMessageBox::warning(this, "Access Denied", "Engineer level or above required.");
        return;
    }
    TagConfigDialog dlg(m_dataCtrl.tagManager(), this);
    dlg.exec();
}

void MainWindow::onShowPerformanceMonitor() {
    QMessageBox::information(this, "Performance", "Performance monitor - to be implemented.");
}
