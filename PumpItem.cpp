#include "PumpItem.h"
#include <QTime>
PumpItem::PumpItem(QGraphicsItem* parent):BaseGraphicsItem(parent)
{
	m_bodyColor = QColor(100,100,100);
	setFlags(ItemIsSelectable|ItemIsFocusable);
}
void PumpItem::loadFromJson(const QJsonObject& json)
{
	BaseGraphicsItem::loadFromJson(json);
	m_size = json["size"].toDouble(30.0);
	updateAppearance();
}

void PumpItem::setSize(qreal size)
{
	m_size = size;
	update();
}

QRectF PumpItem::boundingRect() const
{
	return QRectF(-m_size,-m_size-15,m_size*2,m_size*2+30);
}

void PumpItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
	Q_UNUSED(option);
	Q_UNUSED(widget);

	painter->setRenderHint(QPainter::Antialiasing,true);


	painter->setPen(QPen(m_bodyColor.darker(150),2));
	painter->setBrush(m_bodyColor);
	painter->drawEllipse(QPointF(0,0),m_size*0.8,m_size*0.8);


	painter->setPen(QPen(m_bodyColor.darker(120), 3));
	painter->drawLine(QPointF(-m_size * 0.8, 0), QPointF(-m_size, 0));
	painter->drawLine(QPointF(0, -m_size * 0.8), QPointF(0, -m_size));


	painter->save();
	painter->translate(0,0);
	painter->rotate(m_running?m_rotationAngle:0);

	QPen bladePen(QColor(200, 200, 200), 2);
	painter->setPen(bladePen);

	for (int i = 0; i < 3; i++)
	{
		painter->rotate(120);
		painter->drawLine(QPointF(0,0),QPointF(m_size*0.6,0));
	}
	painter->restore();

	painter->setPen(Qt::white);
	QFont font = painter->font();
	font.setPointSize(8);
	painter->setFont(font);
	painter->drawText(QRectF(-m_size, m_size * 0.8, m_size * 2, 15),
		Qt::AlignCenter, m_tagName);
	if (m_running)
	{
		painter->setBrush(QColor(0, 200, 0));
	}
	else {
		painter->setBrush(QColor(150, 150, 150));
	}
	painter->setPen(Qt::NoPen);
	painter->drawEllipse(QPointF(m_size * 0.5, m_size * 0.5), 4, 4);

	if (m_alarmState == AlarmState::HighHigh || m_alarmState == AlarmState::LowLow)
	{
		QTime now = QTime::currentTime();
		if (now.msec() < 500) {
			painter->setPen(QPen(QColor(255, 0, 0), 3));
			painter->setBrush(Qt::NoBrush);
			painter->drawEllipse(QPointF(0, 0), m_size * 0.85, m_size * 0.85);
		}
	}

	if (m_running) {
		m_rotationAngle += 5.0;
		if (m_rotationAngle >= 360.0) {
			m_rotationAngle -= 360.0;
		}
	}

}

void PumpItem::updateAppearance()
{


	m_running = (m_tagValue>0.5f);


	if (m_alarmState != AlarmState::Normal) {
		m_bodyColor = alarmColor();
	}
	else if (m_running) {
		m_bodyColor = QColor(0, 150, 0);
	}
	else {
		m_bodyColor = QColor(100, 100, 100);
	}

	update();
}
