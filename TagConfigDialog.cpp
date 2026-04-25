#include "TagConfigDialog.h"
#include "TagConfigMgr.h"
#include "ConfigManager.h"
#include "logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QSplitter>

TagConfigDialog::TagConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("位号配置编辑器"));
    resize(900, 600);
    setStyleSheet(
        "QDialog { background-color: #2d2d2d; }"
        "QLabel { color: #ccc; font-size: 13px; }"
        "QTableWidget { background: #1a1a1a; color: #ddd; gridline-color: #444;"
        "  border: 1px solid #555; }"
        "QTableWidget::item { padding: 4px; }"
        "QTableWidget::item:selected { background: #1f6feb; }"
        "QHeaderView::section { background: #3d3d3d; color: #ccc; border: 1px solid #555; padding: 4px; }"
        "QLineEdit { background: #3d3d3d; color: #fff; border: 1px solid #555; border-radius: 3px; padding: 4px 8px; }"
        "QLineEdit:focus { border-color: #58a6ff; }"
        "QDoubleSpinBox, QSpinBox, QComboBox { background: #3d3d3d; color: #fff; border: 1px solid #555;"
        "  border-radius: 3px; padding: 4px; }"
        "QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus { border-color: #58a6ff; }"
        "QPushButton { background: #3d3d3d; color: #ccc; border: 1px solid #555;"
        "  border-radius: 3px; padding: 6px 16px; font-size: 13px; }"
        "QPushButton:hover { background: #4d4d4d; }"
        "QPushButton#btnSave { background: #1f6feb; color: #fff; border: none; }"
        "QPushButton#btnSave:hover { background: #388bfd; }"
        "QGroupBox { color: #ccc; font-weight: bold; border: 1px solid #555; border-radius: 4px;"
        "  margin-top: 12px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // ---- Left: Tag list ----
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 5, 0);

    auto* listLabel = new QLabel(QStringLiteral("位号列表"), this);
    leftLayout->addWidget(listLabel);

    m_tagTable = new QTableWidget(0, 3, this);
    m_tagTable->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("名称"), QStringLiteral("描述")
    });
    m_tagTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tagTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tagTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tagTable->horizontalHeader()->setStretchLastSection(true);
    m_tagTable->verticalHeader()->setVisible(false);
    m_tagTable->setColumnWidth(0, 60);
    m_tagTable->setColumnWidth(1, 100);
    leftLayout->addWidget(m_tagTable);

    splitter->addWidget(leftWidget);

    // ---- Right: Edit form ----
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(5, 0, 0, 0);

    // Basic info group
    auto* basicGroup = new QGroupBox(QStringLiteral("基本信息"), this);
    auto* basicForm = new QFormLayout(basicGroup);

    m_editName = new QLineEdit(this);
    m_editName->setReadOnly(true);
    basicForm->addRow(QStringLiteral("位号名称:"), m_editName);

    m_editDesc = new QLineEdit(this);
    basicForm->addRow(QStringLiteral("描述:"), m_editDesc);

    m_editUnit = new QLineEdit(this);
    basicForm->addRow(QStringLiteral("单位:"), m_editUnit);

    m_comboType = new QComboBox(this);
    m_comboType->addItems({ "AI", "AO", "DI", "DO", "PID" });
    m_comboType->setEnabled(false);
    basicForm->addRow(QStringLiteral("类型:"), m_comboType);

    rightLayout->addWidget(basicGroup);

    // Range & Alarm limits group
    auto* limitGroup = new QGroupBox(QStringLiteral("量程与报警限值"), this);
    auto* limitForm = new QFormLayout(limitGroup);

    m_spinEngLow = new QDoubleSpinBox(this);
    m_spinEngLow->setRange(-99999, 99999);
    m_spinEngLow->setDecimals(1);
    limitForm->addRow(QStringLiteral("量程下限:"), m_spinEngLow);

    m_spinEngHigh = new QDoubleSpinBox(this);
    m_spinEngHigh->setRange(-99999, 99999);
    m_spinEngHigh->setDecimals(1);
    limitForm->addRow(QStringLiteral("量程上限:"), m_spinEngHigh);

    m_spinHH = new QDoubleSpinBox(this);
    m_spinHH->setRange(-99999, 99999);
    m_spinHH->setDecimals(1);
    m_spinHH->setStyleSheet("color: #ff4444;");
    limitForm->addRow(QStringLiteral("高高报(HH):"), m_spinHH);

    m_spinH = new QDoubleSpinBox(this);
    m_spinH->setRange(-99999, 99999);
    m_spinH->setDecimals(1);
    m_spinH->setStyleSheet("color: #ff8800;");
    limitForm->addRow(QStringLiteral("高报(H):"), m_spinH);

    m_spinL = new QDoubleSpinBox(this);
    m_spinL->setRange(-99999, 99999);
    m_spinL->setDecimals(1);
    m_spinL->setStyleSheet("color: #ff8800;");
    limitForm->addRow(QStringLiteral("低报(L):"), m_spinL);

    m_spinLL = new QDoubleSpinBox(this);
    m_spinLL->setRange(-99999, 99999);
    m_spinLL->setDecimals(1);
    m_spinLL->setStyleSheet("color: #ff4444;");
    limitForm->addRow(QStringLiteral("低低报(LL):"), m_spinLL);

    m_spinDeadband = new QDoubleSpinBox(this);
    m_spinDeadband->setRange(0, 999);
    m_spinDeadband->setDecimals(1);
    m_spinDeadband->setSingleStep(0.1);
    limitForm->addRow(QStringLiteral("回差(死区):"), m_spinDeadband);

    rightLayout->addWidget(limitGroup);

    // Modbus mapping group
    auto* modbusGroup = new QGroupBox(QStringLiteral("Modbus 映射"), this);
    auto* modbusForm = new QFormLayout(modbusGroup);

    m_spinServerAddr = new QSpinBox(this);
    m_spinServerAddr->setRange(1, 255);
    modbusForm->addRow(QStringLiteral("服务器地址:"), m_spinServerAddr);

    m_spinRegAddr = new QSpinBox(this);
    m_spinRegAddr->setRange(0, 65535);
    modbusForm->addRow(QStringLiteral("寄存器地址:"), m_spinRegAddr);

    m_spinRegCount = new QSpinBox(this);
    m_spinRegCount->setRange(1, 10);
    modbusForm->addRow(QStringLiteral("寄存器数量:"), m_spinRegCount);

    rightLayout->addWidget(modbusGroup);

    // Buttons
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    m_btnApply = new QPushButton(QStringLiteral("应用到选中位号"), this);
    m_btnSave = new QPushButton(QStringLiteral("保存到文件"), this);
    m_btnSave->setObjectName("btnSave");
    auto* btnClose = new QPushButton(QStringLiteral("关闭"), this);
    btnLayout->addWidget(m_btnApply);
    btnLayout->addWidget(m_btnSave);
    btnLayout->addWidget(btnClose);
    rightLayout->addLayout(btnLayout);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter);

    // Connections
    connect(m_tagTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0) {
            auto* idItem = m_tagTable->item(row, 0);
            if (idItem) {
                quint32 tagId = idItem->data(Qt::UserRole).toUInt();
                auto tag = TagConfigMgr::instance().getTag(tagId);
                if (tag.tagId != 0) populateForm(tag);
            }
        }
    });
    connect(m_btnApply, &QPushButton::clicked, this, &TagConfigDialog::onApply);
    connect(m_btnSave, &QPushButton::clicked, this, &TagConfigDialog::onSaveAll);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::close);

    loadTags();
}

void TagConfigDialog::loadTags()
{
    m_tagTable->setRowCount(0);
    auto tags = TagConfigMgr::instance().getAllTags();
    m_tagTable->setRowCount(tags.size());

    m_originalTags.clear();
    int row = 0;
    for (const auto& tag : tags) {
        auto* idItem = new QTableWidgetItem(QString::number(tag.tagId));
        idItem->setData(Qt::UserRole, tag.tagId);
        m_tagTable->setItem(row, 0, idItem);
        m_tagTable->setItem(row, 1, new QTableWidgetItem(tag.tagName));
        m_tagTable->setItem(row, 2, new QTableWidgetItem(tag.description));
        m_originalTags.insert(tag.tagId, tag);
        row++;
    }
}

void TagConfigDialog::populateForm(const TagInfo& tag)
{
    m_editName->setText(tag.tagName);
    m_editDesc->setText(tag.description);
    m_editUnit->setText(tag.unit);
    m_comboType->setCurrentIndex(static_cast<int>(tag.tagType));
    m_spinEngLow->setValue(tag.engLow);
    m_spinEngHigh->setValue(tag.engHigh);
    m_spinHH->setValue(tag.highHighLimit);
    m_spinH->setValue(tag.highLimit);
    m_spinL->setValue(tag.lowLimit);
    m_spinLL->setValue(tag.lowLowLimit);
    m_spinDeadband->setValue(tag.deadband);
    m_spinServerAddr->setValue(tag.modbusServerAddr);
    m_spinRegAddr->setValue(tag.modbusRegAddr);
    m_spinRegCount->setValue(tag.modbusRegCount);
}

bool TagConfigDialog::validateForm()
{
    double engLow = m_spinEngLow->value();
    double engHigh = m_spinEngHigh->value();
    if (engLow >= engHigh) {
        QMessageBox::warning(this, QStringLiteral("输入错误"),
            QStringLiteral("量程下限必须小于量程上限！"));
        return false;
    }
    double hh = m_spinHH->value();
    double h = m_spinH->value();
    double l = m_spinL->value();
    double ll = m_spinLL->value();
    if (hh <= h) {
        QMessageBox::warning(this, QStringLiteral("输入错误"),
            QStringLiteral("高高报限值必须大于高报限值！"));
        return false;
    }
    if (ll >= l) {
        QMessageBox::warning(this, QStringLiteral("输入错误"),
            QStringLiteral("低低报限值必须小于低报限值！"));
        return false;
    }
    return true;
}

void TagConfigDialog::updateTagFromForm(quint32 tagId)
{
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (tag.tagId == 0) return;

    tag.description = m_editDesc->text();
    tag.unit = m_editUnit->text();
    tag.engLow = static_cast<float>(m_spinEngLow->value());
    tag.engHigh = static_cast<float>(m_spinEngHigh->value());
    tag.highHighLimit = static_cast<float>(m_spinHH->value());
    tag.highLimit = static_cast<float>(m_spinH->value());
    tag.lowLimit = static_cast<float>(m_spinL->value());
    tag.lowLowLimit = static_cast<float>(m_spinLL->value());
    tag.deadband = static_cast<float>(m_spinDeadband->value());
    tag.modbusServerAddr = m_spinServerAddr->value();
    tag.modbusRegAddr = m_spinRegAddr->value();
    tag.modbusRegCount = m_spinRegCount->value();

    // Update TagConfigMgr in-memory
    TagConfigMgr::instance().addTag(tag);
    LOG_INFO("TagConfig", QString("位号配置已更新: %1 (ID=%2)").arg(tag.tagName).arg(tag.tagId));
}

void TagConfigDialog::onApply()
{
    if (!validateForm()) return;

    auto* idItem = m_tagTable->item(m_tagTable->currentRow(), 0);
    if (!idItem) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个位号"));
        return;
    }

    quint32 tagId = idItem->data(Qt::UserRole).toUInt();
    updateTagFromForm(tagId);

    // Update table display
    auto tag = TagConfigMgr::instance().getTag(tagId);
    if (m_tagTable->item(m_tagTable->currentRow(), 2)) {
        m_tagTable->item(m_tagTable->currentRow(), 2)->setText(tag.description);
    }

    QMessageBox::information(this, QStringLiteral("成功"),
        QStringLiteral("位号 %1 配置已更新").arg(tag.tagName));
}

void TagConfigDialog::onSaveAll()
{
    // Save current form changes first
    auto* idItem = m_tagTable->item(m_tagTable->currentRow(), 0);
    if (idItem) {
        quint32 tagId = idItem->data(Qt::UserRole).toUInt();
        updateTagFromForm(tagId);
    }

    // Save all tags to file
    if (ConfigManager::instance().saveTags(ConfigManager::instance().defaultTagsPath())) {
        QMessageBox::information(this, QStringLiteral("成功"),
            QStringLiteral("所有位号配置已保存到文件"));
    } else {
        QMessageBox::warning(this, QStringLiteral("错误"),
            QStringLiteral("保存位号配置失败"));
    }
}
