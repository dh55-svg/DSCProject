#pragma once
#include "export.h"
#include "TagDef.h"
#include "RealtimeDb.h"
#include <QGraphicsItem>
#include <QJsonObject>
#include <QString>
#include <QColor>
#include <functional>
/**
 * @brief 工业图元基类
 *
 * 所有DCS画面上的图元（管道、阀门、泵、液位计等）都继承自此类。
 *
 * 核心设计：
 * 1. 属性绑定：每个图元可以绑定一个位号(tagName)，数据变化时自动更新外观
 * 2. JSON组态：图元的属性从JSON配置加载，不需要硬编码
 * 3. 状态驱动：图元根据绑定位号的值/报警状态/质量码自动改变颜色/动画
 *
 * 踩坑经验：
 * - QGraphicsItem的paint()函数每帧都会调用，不能在里面做复杂计算
 * - 图元创建后尽量复用，不要频繁delete/new，会导致内存碎片
 * - 图元的boundingRect()必须准确，否则会出现残影或裁剪
 * - 大量图元时必须开启QGraphicsView的缓存模式
 */

class GRAPHICS_EXPORT BaseGraphicsItem :public QGraphicsObject {
	Q_OBJECT
public:
    explicit BaseGraphicsItem(QGraphicsItem* parent = nullptr);
    ~BaseGraphicsItem() override = default;

    // 位号绑定
    void bindTag(const QString & tagName);
    QString boundTagName() const { return m_tagName; }
    quint32 boundTagId() const { return m_tagId; }

    // 从JSON加载属性
    virtual void loadFromJson(const QJsonObject & json);

    // 获取图元类型名（用于组态识别）
    virtual QString itemTypeName() const = 0;

    // 数据更新回调（由RealtimeDb的回调机制触发）
    void onTagValueChanged(quint32 tagId, float newValue);
signals:
    // 图元被点击（用于弹出操作面板）
    void itemClicked(BaseGraphicsItem* item);
    // 图元状态变化（用于报警高亮等）
    void stateChanged();
protected:
	// 子类实现：根据当前绑定位号的值更新外观
	virtual void updateAppearance() = 0;
	// 获取绑定位号的当前值
	float tagValue() const { return m_tagValue; }
	AlarmLimit tagAlarmState() const { return m_alarmState; }
	DataQuality tagQuality() const { return m_quality; }

    // 根据报警状态返回对应颜色
    QColor alarmColor() const;

    // 根据数据质量返回显示文本
    QString qualityText() const;

    // 鼠标点击事件
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    QString m_tagName;              // 绑定的位号名
    quint32 m_tagId = 0;            // 绑定的位号ID
    float m_tagValue = 0.0f;        // 当前绑定位号的值
    AlarmLimit m_alarmState = AlarmLimit::Normal; // 报警状态
    DataQuality m_quality = DataQuality::Good;    // 数据质量码

};

