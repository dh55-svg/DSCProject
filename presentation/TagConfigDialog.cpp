#include "TagConfigDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>

TagConfigDialog::TagConfigDialog(TagManager& tagManager, QWidget* parent)
    : QDialog(parent), m_tagManager(tagManager)
{
    setWindowTitle("Tag Configuration");
    resize(900, 600);

    auto* mainLayout = new QHBoxLayout(this);
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left: tag table
    auto* leftWidget = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_tagTable = new QTableWidget(0, 4);
    m_tagTable->setHorizontalHeaderLabels({"ID", "Name", "Type", "Description"});
    m_tagTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tagTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tagTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_tagTable, &QTableWidget::cellClicked, this, &TagConfigDialog::onTagSelected);
    leftLayout->addWidget(m_tagTable);
    splitter->addWidget(leftWidget);

    // Right: edit form
    auto* rightWidget = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightWidget);

    auto* formGroup = new QGroupBox("Tag Properties");
    auto* formLayout = new QFormLayout(formGroup);

    m_editName = new QLineEdit();
    m_editDesc = new QLineEdit();
    m_editUnit = new QLineEdit();
    m_comboType = new QComboBox();
    m_comboType->addItems({"AI", "AO", "DI", "DO", "PID"});

    m_spinEngLow = new QDoubleSpinBox(); m_spinEngLow->setRange(-99999, 99999);
    m_spinEngHigh = new QDoubleSpinBox(); m_spinEngHigh->setRange(-99999, 99999);
    m_spinHH = new QDoubleSpinBox(); m_spinHH->setRange(-99999, 99999);
    m_spinH = new QDoubleSpinBox(); m_spinH->setRange(-99999, 99999);
    m_spinL = new QDoubleSpinBox(); m_spinL->setRange(-99999, 99999);
    m_spinLL = new QDoubleSpinBox(); m_spinLL->setRange(-99999, 99999);
    m_spinDeadband = new QDoubleSpinBox(); m_spinDeadband->setRange(0, 9999);
    m_spinServerAddr = new QSpinBox(); m_spinServerAddr->setRange(1, 255);
    m_spinRegAddr = new QSpinBox(); m_spinRegAddr->setRange(0, 65535);
    m_spinRegCount = new QSpinBox(); m_spinRegCount->setRange(1, 64);

    for (auto* sp : {m_spinEngLow, m_spinEngHigh, m_spinHH, m_spinH, m_spinL, m_spinLL}) {
        sp->setDecimals(2);
        sp->setFixedWidth(120);
    }

    formLayout->addRow("Name:", m_editName);
    formLayout->addRow("Description:", m_editDesc);
    formLayout->addRow("Unit:", m_editUnit);
    formLayout->addRow("Type:", m_comboType);
    formLayout->addRow("Eng Low:", m_spinEngLow);
    formLayout->addRow("Eng High:", m_spinEngHigh);
    formLayout->addRow("HH Limit:", m_spinHH);
    formLayout->addRow("H Limit:", m_spinH);
    formLayout->addRow("L Limit:", m_spinL);
    formLayout->addRow("LL Limit:", m_spinLL);
    formLayout->addRow("Deadband:", m_spinDeadband);
    formLayout->addRow("Modbus Addr:", m_spinServerAddr);
    formLayout->addRow("Reg Addr:", m_spinRegAddr);
    formLayout->addRow("Reg Count:", m_spinRegCount);

    rightLayout->addWidget(formGroup);

    auto* btnLayout = new QHBoxLayout();
    m_btnApply = new QPushButton("Apply");
    m_btnSave = new QPushButton("Save All");
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnApply);
    btnLayout->addWidget(m_btnSave);
    rightLayout->addLayout(btnLayout);

    connect(m_btnApply, &QPushButton::clicked, this, &TagConfigDialog::onApply);
    connect(m_btnSave, &QPushButton::clicked, this, &TagConfigDialog::onSaveAll);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter);

    loadTags();
}

void TagConfigDialog::loadTags() {
    m_tagTable->setRowCount(0);
    m_originalTags.clear();
    QList<TagInfo> tags = m_tagManager.getAllTags();
    for (const auto& tag : tags) {
        m_originalTags[tag.tagId] = tag;
        int row = m_tagTable->rowCount();
        m_tagTable->insertRow(row);
        m_tagTable->setItem(row, 0, new QTableWidgetItem(QString::number(tag.tagId)));
        m_tagTable->setItem(row, 1, new QTableWidgetItem(tag.tagName));
        m_tagTable->setItem(row, 2, new QTableWidgetItem(
            tag.tagType == TagType::PID ? "PID" :
            tag.tagType == TagType::AO ? "AO" :
            tag.tagType == TagType::DI ? "DI" :
            tag.tagType == TagType::DO ? "DO" : "AI"));
        m_tagTable->setItem(row, 3, new QTableWidgetItem(tag.description));
    }
}

void TagConfigDialog::onTagSelected(int row, int) {
    QTableWidgetItem* idItem = m_tagTable->item(row, 0);
    if (!idItem) return;
    quint32 tagId = idItem->text().toUInt();
    if (m_originalTags.contains(tagId))
        populateForm(m_originalTags[tagId]);
}

void TagConfigDialog::populateForm(const TagInfo& tag) {
    m_editName->setText(tag.tagName);
    m_editDesc->setText(tag.description);
    m_editUnit->setText(tag.unit);

    switch (tag.tagType) {
    case TagType::PID: m_comboType->setCurrentText("PID"); break;
    case TagType::AO:  m_comboType->setCurrentText("AO"); break;
    case TagType::DI:  m_comboType->setCurrentText("DI"); break;
    case TagType::DO:  m_comboType->setCurrentText("DO"); break;
    default:           m_comboType->setCurrentText("AI"); break;
    }

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

bool TagConfigDialog::validateForm() {
    if (m_editName->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Tag name cannot be empty.");
        return false;
    }
    if (m_spinHH->value() <= m_spinH->value()) {
        QMessageBox::warning(this, "Validation Error", "HH limit must be > H limit.");
        return false;
    }
    if (m_spinL->value() <= m_spinLL->value()) {
        QMessageBox::warning(this, "Validation Error", "L limit must be > LL limit.");
        return false;
    }
    if (m_spinEngHigh->value() <= m_spinEngLow->value()) {
        QMessageBox::warning(this, "Validation Error", "Eng High must be > Eng Low.");
        return false;
    }
    return true;
}

void TagConfigDialog::onApply() {
    int row = m_tagTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "No Selection", "Please select a tag in the table first.");
        return;
    }

    if (!validateForm()) return;

    quint32 tagId = m_tagTable->item(row, 0)->text().toUInt();
    updateTagFromForm(tagId);
    loadTags();
    m_tagTable->selectRow(row);
}

void TagConfigDialog::onSaveAll() {
    m_tagManager.saveToJson("config/tags.json");
    QMessageBox::information(this, "Saved", "Tag configuration saved.");
}

void TagConfigDialog::updateTagFromForm(quint32 tagId) {
    if (!m_originalTags.contains(tagId)) return;
    TagInfo tag = m_originalTags[tagId];

    tag.tagName = m_editName->text().trimmed();
    tag.description = m_editDesc->text();
    tag.unit = m_editUnit->text();

    QString typeStr = m_comboType->currentText();
    if (typeStr == "PID") tag.tagType = TagType::PID;
    else if (typeStr == "AO") tag.tagType = TagType::AO;
    else if (typeStr == "DI") tag.tagType = TagType::DI;
    else if (typeStr == "DO") tag.tagType = TagType::DO;
    else tag.tagType = TagType::AI;

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

    m_tagManager.addTag(tag);
}
