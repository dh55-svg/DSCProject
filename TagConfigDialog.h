#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QHash>

#include "TagDef.h"

class TagConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit TagConfigDialog(QWidget* parent = nullptr);

private slots:
    void onApply();
    void onSaveAll();

private:
    void loadTags();
    void populateForm(const TagInfo& tag);
    bool validateForm();
    void updateTagFromForm(quint32 tagId);

    QTableWidget* m_tagTable = nullptr;

    // Form widgets
    QLineEdit* m_editName = nullptr;
    QLineEdit* m_editDesc = nullptr;
    QLineEdit* m_editUnit = nullptr;
    QComboBox* m_comboType = nullptr;
    QDoubleSpinBox* m_spinEngLow = nullptr;
    QDoubleSpinBox* m_spinEngHigh = nullptr;
    QDoubleSpinBox* m_spinHH = nullptr;
    QDoubleSpinBox* m_spinH = nullptr;
    QDoubleSpinBox* m_spinL = nullptr;
    QDoubleSpinBox* m_spinLL = nullptr;
    QDoubleSpinBox* m_spinDeadband = nullptr;
    QSpinBox* m_spinServerAddr = nullptr;
    QSpinBox* m_spinRegAddr = nullptr;
    QSpinBox* m_spinRegCount = nullptr;

    QPushButton* m_btnApply = nullptr;
    QPushButton* m_btnSave = nullptr;

    QHash<quint32, TagInfo> m_originalTags;
};
