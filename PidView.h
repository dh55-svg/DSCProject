#pragma once

#include <QGraphicsView>

class PidView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit PidView(QGraphicsScene* scene, QWidget* parent = nullptr)
        : QGraphicsView(scene, parent) {}
};
