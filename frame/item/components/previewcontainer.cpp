/*
 * Copyright (C) 2011 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     sbw <sbw@sbw.so>
 *
 * Maintainer: sbw <sbw@sbw.so>
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

#include "previewcontainer.h"

#include <QDesktopWidget>
#include <QScreen>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDesktopWidget>

#define SPACING           0
#define MARGIN            0
#define SNAP_HEIGHT_WITHOUT_COMPOSITE       30

PreviewContainer::PreviewContainer(QWidget *parent)
    : QWidget(parent)
    , m_needActivate(false)
    , m_floatingPreview(new FloatingPreview(this))
    , m_mouseLeaveTimer(new QTimer(this))
    , m_wmHelper(DWindowManagerHelper::instance())
{
    m_windowListLayout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_windowListLayout->setSpacing(SPACING);
    m_windowListLayout->setContentsMargins(MARGIN, MARGIN, MARGIN, MARGIN);

    m_mouseLeaveTimer->setSingleShot(true);
    m_mouseLeaveTimer->setInterval(300);

    m_floatingPreview->setVisible(false);

    m_waitForShowPreviewTimer = new QTimer(this);
    m_waitForShowPreviewTimer->setSingleShot(true);
    m_waitForShowPreviewTimer->setInterval(200);

    setAcceptDrops(true);
    setFixedSize(SNAP_WIDTH, SNAP_HEIGHT);

    connect(m_mouseLeaveTimer, &QTimer::timeout, this, &PreviewContainer::checkMouseLeave, Qt::QueuedConnection);
    connect(m_waitForShowPreviewTimer, &QTimer::timeout, this, &PreviewContainer::previewFloating);
}

void PreviewContainer::setWindowInfos(const WindowInfoMap &infos, const WindowList &allowClose)
{
    // check removed window
    for (auto it(m_snapshots.begin()); it != m_snapshots.end();) {
        //初始化预览界面边距
        it.value()->setContentsMargins(0, 0, 0, 0);

        if (!infos.contains(it.key())) {
            m_windowListLayout->removeWidget(it.value());
            it.value()->deleteLater();
            it = m_snapshots.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it(infos.cbegin()); it != infos.cend(); ++it) {
        const WId key = it.key();
        if (!m_snapshots.contains(key))
            appendSnapWidget(key);
        m_snapshots[key]->setWindowInfo(it.value());
        m_snapshots[key]->setCloseAble(allowClose.contains(key));
    }

    if (m_snapshots.isEmpty()) {
        emit requestCancelPreviewWindow();
        emit requestHidePopup();
    }

    adjustSize(m_wmHelper->hasComposite());
}

void PreviewContainer::updateSnapshots()
{
    for (AppSnapshot *snap : m_snapshots)
        snap->fetchSnapshot();
}

void PreviewContainer::updateLayoutDirection(const Dock::Position dockPos)
{
    if (m_wmHelper->hasComposite() && (dockPos == Dock::Top || dockPos == Dock::Bottom))
        m_windowListLayout->setDirection(QBoxLayout::LeftToRight);
    else
        m_windowListLayout->setDirection(QBoxLayout::TopToBottom);

    adjustSize(m_wmHelper->hasComposite());
}

void PreviewContainer::checkMouseLeave()
{
    const bool hover = underMouse();

    if (hover)
        return;

    m_floatingPreview->setVisible(false);

    if (m_wmHelper->hasComposite()) {
        if (m_needActivate) {
            m_needActivate = false;
            emit requestActivateWindow(m_floatingPreview->trackedWid());
        } else {
            Q_EMIT requestHidePopup();
            emit requestCancelPreviewWindow();
        }
    }

    emit requestHidePopup();
}

void PreviewContainer::prepareHide()
{
    m_mouseLeaveTimer->start();
}

void PreviewContainer::adjustSize(bool composite)
{
    int count = m_snapshots.size();
    const int screenWidth = QDesktopWidget().screenGeometry(this).width();
    const int screenHeight = QDesktopWidget().screenGeometry(this).height();

    //先根据屏幕宽高计算出能预览的最大数量,然后根据数量计算界面宽高
    if (composite) {
        // 3D
        const int padding = 20;
        const bool horizontal = m_windowListLayout->direction() == QBoxLayout::LeftToRight;
        if (horizontal) {
            count = qMin(count, screenWidth *2 / SNAP_WIDTH);

            const int h = SNAP_HEIGHT + MARGIN * 2;
            const int w = SNAP_WIDTH * count + MARGIN * 2 + SPACING * (count - 1);

            setFixedHeight(h);
            setFixedWidth(qMin(w, screenWidth - padding));
        } else {
            count = qMin(count, screenWidth *2 / SNAP_HEIGHT);

            const int w = SNAP_WIDTH + MARGIN * 2;
            const int h = SNAP_HEIGHT * count + MARGIN * 2 + SPACING * (count - 1);

            setFixedWidth(w);
            setFixedHeight(qMin(h, screenHeight - padding));
        }
    } else if (m_windowListLayout->count()) {
        // 2D
        count = qMin(count, screenWidth / SNAP_HEIGHT_WITHOUT_COMPOSITE);
        const int h = SNAP_HEIGHT_WITHOUT_COMPOSITE * count + MARGIN * 2 + SPACING * (count - 1);

        // 根据appitem title 设置自适应宽度
        auto appSnapshot = static_cast<AppSnapshot *>(m_windowListLayout->itemAt(0)->widget());

        auto font = appSnapshot->layout()->itemAt(0)->widget()->font();
        QFontMetrics fontMetrics(font);
        const int titleWidth = fontMetrics.boundingRect(appSnapshot->title()).width();
        // 关闭按键的宽度和边缘间距的和，调整标题居中
        const int closeBtnMargin = 2 * (SNAP_CLOSE_BTN_WIDTH + SNAP_CLOSE_BTN_MARGIN);
        if (titleWidth < SNAP_WIDTH - closeBtnMargin) {
            setFixedSize(titleWidth + closeBtnMargin, h);
        } else {
            setFixedSize(SNAP_WIDTH, h);
        }
    }

    //根据计算的数量,将相应的预览界面添加到布局并显示,其他的暂时不添加,减少界面刷新次数
    int i = 0;
    for (AppSnapshot *snap : m_snapshots) {
        if (i < count && m_windowListLayout->indexOf(snap) < 0 ) {
            m_windowListLayout->addWidget(snap);
        }
        snap->setVisible(i < count);
        i++;
    }
}

void PreviewContainer::appendSnapWidget(const WId wid)
{
    //创建预览界面,默认不显示,等计算出显示数量后再加入布局并显示
    AppSnapshot *snap = new AppSnapshot(wid);
    snap->setVisible(false);

    connect(snap, &AppSnapshot::clicked, this, &PreviewContainer::onSnapshotClicked, Qt::QueuedConnection);
    connect(snap, &AppSnapshot::entered, this, &PreviewContainer::previewEntered, Qt::QueuedConnection);
    connect(snap, &AppSnapshot::requestCheckWindow, this, &PreviewContainer::requestCheckWindows, Qt::QueuedConnection);
    connect(snap, &AppSnapshot::requestCloseAppSnapshot, this, &PreviewContainer::onRequestCloseAppSnapshot);

    m_snapshots.insert(wid, snap);
}

void PreviewContainer::enterEvent(QEvent *e)
{
    QWidget::enterEvent(e);

    m_needActivate = false;
    m_mouseLeaveTimer->stop();

    if (m_wmHelper->hasComposite()) {
        m_waitForShowPreviewTimer->start();
    }
}

void PreviewContainer::leaveEvent(QEvent *e)
{
    QWidget::leaveEvent(e);

    m_mouseLeaveTimer->start();
    m_waitForShowPreviewTimer->stop();
}

void PreviewContainer::dragEnterEvent(QDragEnterEvent *e)
{
    if (!m_wmHelper->hasComposite())
        return;

    e->accept();

    m_needActivate = false;
    m_mouseLeaveTimer->stop();
}

void PreviewContainer::dragLeaveEvent(QDragLeaveEvent *e)
{
    e->ignore();

    m_needActivate = true;
    m_mouseLeaveTimer->start();
}

void PreviewContainer::onSnapshotClicked(const WId wid)
{
    Q_EMIT requestActivateWindow(wid);
    m_needActivate = true;
    m_waitForShowPreviewTimer->stop();
    requestHidePopup();
}

void PreviewContainer::previewEntered(const WId wid)
{
    if (!m_wmHelper->hasComposite())
        return;

    AppSnapshot *snap = static_cast<AppSnapshot *>(sender());
    if (!snap) {
        return;
    }
    snap->setContentsMargins(100, 0, 100, 0);

    AppSnapshot *preSnap = m_floatingPreview->trackedWindow();
    if (preSnap && preSnap != snap) {
        preSnap->setContentsMargins(0, 0, 0, 0);
        preSnap->setWindowState();
    }

    m_currentWId = wid;

    m_floatingPreview->trackWindow(snap);

    if (m_waitForShowPreviewTimer->isActive()) {
        return;
    }

    previewFloating();
}

void PreviewContainer::previewFloating()
{
    if (!m_waitForShowPreviewTimer->isActive()) {
        m_floatingPreview->setVisible(true);
        m_floatingPreview->raise();

        requestPreviewWindow(m_currentWId);
    }
    return;
}

void PreviewContainer::onRequestCloseAppSnapshot()
{
    if (!m_wmHelper->hasComposite())
        return ;

    if (m_currentWId != m_snapshots.lastKey()) {
        Q_EMIT requestHidePopup();
        Q_EMIT requestCancelPreviewWindow();
    }
}
