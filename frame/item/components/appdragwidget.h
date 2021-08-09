/*
 * Copyright (C) 2011 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     listenerri <listenerri@gmail.com>
 *
 * Maintainer: listenerri <listenerri@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef APPDRAGWIDGET_H
#define APPDRAGWIDGET_H

#include "constants.h"

#include <QPixmap>
#include <QGraphicsObject>
#include <QGraphicsView>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QWidget>

#include "../widgets/tipswidget.h"
#include "dockpopupwindow.h"
#include "dockitem.h"

class AppGraphicsObject;
class AppDragWidget : public QGraphicsView
{
    Q_OBJECT
public:
    explicit AppDragWidget(QWidget *parent = Q_NULLPTR);
    virtual ~AppDragWidget() override;

    void setAppPixmap(const QPixmap &pix);
    void setDockInfo(Dock::Position dockPosition, const QRect &dockGeometry);
    void setOriginPos(const QPoint position);
    void setPixmapOpacity(qreal opacity);
    bool isRemoveAble(const QPoint &curPos);
    void setItem(DockItem *item) { m_item = item; }
    static bool isRemoveable(const Dock::Position &dockPos, const QRect &doctRect);
    void showRemoveAnimation();
    void showGoBackAnimation();

signals:
    void requestRemoveItem();

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void enterEvent(QEvent *event) override;

private:
    void initAnimations();
    void onRemoveAnimationStateChanged(QAbstractAnimation::State newState,
            QAbstractAnimation::State oldState);
    const QPoint popupMarkPoint(Dock::Position pos);
    const QPoint topleftPoint() const;
    void showRemoveTips();
    bool isRemoveItem();

private:
    QPointer<AppGraphicsObject> m_object;
    QGraphicsScene *m_scene;
    QTimer *m_followMouseTimer;
    QPropertyAnimation *m_animScale;
    QPropertyAnimation *m_animRotation;
    QPropertyAnimation *m_animOpacity;
    QParallelAnimationGroup *m_animGroup;
    QPropertyAnimation *m_goBackAnim;

    Dock::Position m_dockPosition;
    QRect m_dockGeometry;
    QPoint m_originPoint;
    QSize m_iconSize;
    Dock::TipsWidget *m_removeTips;
    QPointer<DockPopupWindow> m_popupWindow;
    /**
     * @brief m_distanceMultiple: 倍数
     * dock栏上应用区驻留应用被拖拽远离dock的距离除以dock的宽或者高（更小的一个）的比值
     */
    double m_distanceMultiple;

    bool m_bDragDrop = false; // 图标是否被拖拽
    DockItem *m_item;
};

#endif /* APPDRAGWIDGET_H */
