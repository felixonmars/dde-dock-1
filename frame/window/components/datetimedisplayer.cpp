// Copyright (C) 2022 ~ 2022 Deepin Technology Co., Ltd.
// SPDX-FileCopyrightText: 2018 - 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "datetimedisplayer.h"
#include "tipswidget.h"
#include "dockpopupwindow.h"
#include "utils.h"
#include "dbusutil.h"

#include <DFontSizeManager>
#include <DDBusSender>
#include <DGuiApplicationHelper>

#include <QHBoxLayout>
#include <QPainter>
#include <QFont>
#include <QMenu>
#include <QPainterPath>

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

#define DATETIMESIZE 40
#define ITEMSPACE 8

static QMap<int, QString> dateFormat{{ 0,"yyyy/M/d" }, { 1,"yyyy-M-d" }, { 2,"yyyy.M.d" }, { 3,"yyyy/MM/dd" },
                                     { 4,"yyyy-MM-dd" }, { 5,"yyyy.MM.dd" }, { 6,"yy/M/d" }, { 7,"yy-M-d" }, { 8,"yy.M.d" }};
static QMap<int, QString> timeFormat{{0, "h:mm"}, {1, "hh:mm"}};

DateTimeDisplayer::DateTimeDisplayer(bool showMultiRow, QWidget *parent)
    : QWidget (parent)
    , m_timedateInter(new Timedate("org.deepin.dde.Timedate1", "/org/deepin/dde/Timedate1", QDBusConnection::sessionBus(), this))
    , m_position(Dock::Position::Bottom)
    , m_dateFont(DFontSizeManager::instance()->t10())
    , m_tipsWidget(new Dock::TipsWidget(this))
    , m_menu(new QMenu(this))
    , m_tipsTimer(new QTimer(this))
    , m_currentSize(0)
    , m_oneRow(false)
    , m_showMultiRow(showMultiRow)
    , m_isEnter(false)
{
    m_tipPopupWindow.reset(new DockPopupWindow);
    // 日期格式变化的时候，需要重绘
    connect(m_timedateInter, &Timedate::ShortDateFormatChanged, this, &DateTimeDisplayer::onDateTimeFormatChanged);
    // 时间格式变化的时候，需要重绘
    connect(m_timedateInter, &Timedate::ShortTimeFormatChanged, this, &DateTimeDisplayer::onDateTimeFormatChanged);
    // 是否使用24小时制发生变化的时候，也需要重绘
    connect(m_timedateInter, &Timedate::Use24HourFormatChanged, this, &DateTimeDisplayer::onDateTimeFormatChanged);
    // 连接日期时间修改信号,更新日期时间插件的布局
    connect(m_timedateInter, &Timedate::TimeUpdate, this, static_cast<void (QWidget::*)()>(&DateTimeDisplayer::update));
    // 连接定时器和时间显示的tips信号,一秒钟触发一次，显示时间
    connect(m_tipsTimer, &QTimer::timeout, this, &DateTimeDisplayer::onTimeChanged);
    m_tipsTimer->setInterval(1000);
    m_tipsTimer->start();
    updatePolicy();
    createMenuItem();
    if (Utils::IS_WAYLAND_DISPLAY)
        m_tipPopupWindow->setWindowFlags(m_tipPopupWindow->windowFlags() | Qt::FramelessWindowHint);
    m_tipPopupWindow->hide();
}

DateTimeDisplayer::~DateTimeDisplayer()
{
}

void DateTimeDisplayer::setPositon(Dock::Position position)
{
    if (m_position == position)
        return;

    m_position = position;
    updatePolicy();
    update();
}

void DateTimeDisplayer::setOneRow(bool oneRow)
{
    m_oneRow = oneRow;
    update();
}

void DateTimeDisplayer::updatePolicy()
{
    switch (m_position) {
    case Dock::Position::Top: {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_tipPopupWindow->setArrowDirection(DArrowRectangle::ArrowDirection::ArrowTop);
        m_tipPopupWindow->setContent(m_tipsWidget);
        break;
    }
    case Dock::Position::Bottom: {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_tipPopupWindow->setArrowDirection(DArrowRectangle::ArrowDirection::ArrowBottom);
        m_tipPopupWindow->setContent(m_tipsWidget);
        break;
    }
    case Dock::Position::Left: {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_tipPopupWindow->setArrowDirection(DArrowRectangle::ArrowDirection::ArrowLeft);
        m_tipPopupWindow->setContent(m_tipsWidget);
        break;
    }
    case Dock::Position::Right: {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_tipPopupWindow->setArrowDirection(DArrowRectangle::ArrowDirection::ArrowRight);
        m_tipPopupWindow->setContent(m_tipsWidget);
        break;
    }
    }
}

QSize DateTimeDisplayer::suitableSize() const
{
    return suitableSize(m_position);
}

QSize DateTimeDisplayer::suitableSize(const Dock::Position &position) const
{
    DateTimeInfo info = dateTimeInfo(position);
    if (position == Dock::Position::Left || position == Dock::Position::Right)
        return QSize(width(), info.m_timeRect.height() + info.m_dateRect.height());

    // 如果在上下显示
    if (m_showMultiRow) {
        // 如果显示多行的情况,一般是在高效模式下显示，因此，返回最大的尺寸
        return QSize(qMax(info.m_timeRect.width(), info.m_dateRect.width()), height());
    }

    return QSize(info.m_timeRect.width() + info.m_dateRect.width() + 16, height());
}

void DateTimeDisplayer::mousePressEvent(QMouseEvent *event)
{
    if ((event->button() != Qt::RightButton))
        return QWidget::mousePressEvent(event);

    m_menu->exec(QCursor::pos());
}

void DateTimeDisplayer::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);

    DDBusSender().service("org.deepin.dde.Widgets1")
            .path("/org/deepin/dde/Widgets1")
            .interface("org.deepin.dde.Widgets1")
            .method("Toggle").call();
}

QString DateTimeDisplayer::getTimeString(const Dock::Position &position) const
{
    QString tFormat = QString("hh:mm");
    int type = m_timedateInter->shortTimeFormat();
    if (timeFormat.contains(type))
        tFormat = timeFormat[type];

    if (!m_timedateInter->use24HourFormat()) {
        if (position == Dock::Top || position == Dock::Bottom)
            tFormat = tFormat.append(" AP");
        else
            tFormat = tFormat.append("\nAP");
    }

    return QDateTime::currentDateTime().toString(tFormat);
}

QString DateTimeDisplayer::getDateString() const
{
    return getDateString(m_position);
}

QString DateTimeDisplayer::getDateString(const Dock::Position &position) const
{
    int type = m_timedateInter->shortDateFormat();
    QString shortDateFormat = "yyyy-MM-dd";
    if (dateFormat.contains(type))
        shortDateFormat = dateFormat.value(type);
    // 如果是左右方向，则不显示年份
    if (position == Dock::Position::Left || position == Dock::Position::Right) {
        static QStringList yearStrList{"yyyy/", "yyyy-", "yyyy.", "yy/", "yy-", "yy."};
        for (int i = 0; i < yearStrList.size() ; i++) {
            const QString &yearStr = yearStrList[i];
            if (shortDateFormat.contains(yearStr)) {
                shortDateFormat = shortDateFormat.remove(yearStr);
                break;
            }
        }
    }

    return QDateTime::currentDateTime().toString(shortDateFormat);
}

DateTimeDisplayer::DateTimeInfo DateTimeDisplayer::dateTimeInfo(const Dock::Position &position) const
{
    DateTimeInfo info;
    info.m_timeRect = rect();
    info.m_dateRect = rect();

    info.m_time = getTimeString(position);
    info.m_date = getDateString(position);

    // 如果是左右方向
    if (position == Dock::Position::Left || position == Dock::Position::Right) {
        int textWidth = rect().width();
        info.m_timeRect = QRect(0, 0, textWidth, DATETIMESIZE / 2);
        info.m_dateRect = QRect(0, DATETIMESIZE / 2 + 1, textWidth, DATETIMESIZE / 2);
        return info;
    }
    int timeWidth = QFontMetrics(timeFont()).boundingRect(info.m_time).width();
    int dateWidth = QFontMetrics(m_dateFont).boundingRect(info.m_date).width();

    // 如果是上下方向
    if (m_showMultiRow) {
        // 日期时间多行显示（一般是高效模式下，向下和向上偏移2个像素）
        info.m_timeRect = QRect(0, 2, timeWidth, height() / 2);
        info.m_dateRect = QRect(0, height() / 2 -2, dateWidth, height() / 2);
    } else {
        // 3:时间和日期3部分间隔
        if (rect().width() > (ITEMSPACE * 3 + timeWidth + dateWidth)) {
            info.m_timeRect = QRect(ITEMSPACE, 0, timeWidth, height());

            int dateX = info.m_timeRect.right() + (rect().width() -(ITEMSPACE * 2 + timeWidth + dateWidth));
            info.m_dateRect = QRect(dateX, 0, dateWidth, height());
        } else {
            // 宽度不满足间隔为ITEMSPACE的，需要自己计算间隔。
            int itemSpace = (rect().width() - timeWidth - dateWidth) / 3;
            info.m_timeRect = QRect(itemSpace, 0, timeWidth, height());

            int dateX = info.m_timeRect.right() + itemSpace;
            info.m_dateRect = QRect(dateX, 0, dateWidth, height());
        }
    }

    return info;
}

void DateTimeDisplayer::onTimeChanged()
{
    const QDateTime currentDateTime = QDateTime::currentDateTime();

    if (m_timedateInter->use24HourFormat())
        m_tipsWidget->setText(currentDateTime.date().toString(Qt::SystemLocaleLongDate) + currentDateTime.toString(" HH:mm:ss"));
    else
        m_tipsWidget->setText(currentDateTime.date().toString(Qt::SystemLocaleLongDate) + currentDateTime.toString(" hh:mm:ss AP"));

    // 如果时间和日期有一个不等，则实时刷新界面
    if (m_lastDateString != getDateString() || m_lastTimeString != getTimeString())
        update();
}

void DateTimeDisplayer::onDateTimeFormatChanged()
{
    int lastSize = m_currentSize;
    // 此处需要强制重绘，因为在重绘过程中才会改变m_currentSize信息，方便在后面判断是否需要调整尺寸
    repaint();
    // 如果日期时间的格式发生了变化，需要通知外部来调整日期时间的尺寸
    if (lastSize != m_currentSize)
        Q_EMIT requestUpdate();
}

void DateTimeDisplayer::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    DateTimeInfo info = dateTimeInfo(m_position);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().brightText(), 1));

    // 绘制背景色
    if (m_isEnter) {
        QColor backColor = DGuiApplicationHelper::ColorType::DarkType == DGuiApplicationHelper::instance()->themeType() ? QColor(20, 20, 20) : Qt::white;
        backColor.setAlphaF(0.2);
        // 鼠标进入的时候，绘制底色
        painter.fillRect(rect(), backColor);
    }

    int timeAlignFlag = Qt::AlignCenter;
    int dateAlignFlag = Qt::AlignCenter;

    QFont strTimeFont = timeFont();

    if (m_showMultiRow) {
        timeAlignFlag = Qt::AlignHCenter | Qt::AlignBottom;
        dateAlignFlag = Qt::AlignHCenter | Qt::AlignTop;
    } else {
        if (strTimeFont.pixelSize() >= rect().height()) {
            strTimeFont.setPixelSize(rect().height() - 2);
            m_dateFont.setPixelSize(rect().height() - 2);
        }
    }

    painter.setFont(strTimeFont);
    painter.drawText(textRect(info.m_timeRect), timeAlignFlag, info.m_time);
    painter.setFont(m_dateFont);
    painter.drawText(textRect(info.m_dateRect), dateAlignFlag, info.m_date);

    updateLastData(info);
}

QPoint DateTimeDisplayer::tipsPoint() const
{
    QPoint pointInTopWidget = parentWidget()->mapTo(topLevelWidget(), pos());
    switch (m_position) {
    case Dock::Position::Left: {
        pointInTopWidget.setX(topLevelWidget()->x() + topLevelWidget()->width());
        pointInTopWidget.setY(pointInTopWidget.y() + height() / 2);
        break;
    }
    case Dock::Position::Top: {
        pointInTopWidget.setY(y() + topLevelWidget()->y() + topLevelWidget()->height());
        pointInTopWidget.setX(pointInTopWidget.x() + width() / 2);
        break;
    }
    case Dock::Position::Right: {
        pointInTopWidget.setY(pointInTopWidget.y() + height() / 2);
        pointInTopWidget.setX(pointInTopWidget.x() - width() / 2);
        break;
    }
    case Dock::Position::Bottom: {
        pointInTopWidget.setY(0);
        pointInTopWidget.setX(pointInTopWidget.x() + width() / 2);
        break;
    }
    }
    return topLevelWidget()->mapToGlobal(pointInTopWidget);
}

QFont DateTimeDisplayer::timeFont() const
{
    if (m_position == Dock::Position::Left || m_position == Dock::Position::Right)
        return DFontSizeManager::instance()->t6();

#define MINHEIGHT 40

    // 如果是上下方向，且当前只有一行，则始终显示小号字体
    if (m_oneRow || rect().height() <= MINHEIGHT)
        return DFontSizeManager::instance()->t10();

    static QList<QFont> dateFontSize = { DFontSizeManager::instance()->t10(),
                DFontSizeManager::instance()->t9(),
                DFontSizeManager::instance()->t8(),
                DFontSizeManager::instance()->t7() };

    int index = qMin(qMax(static_cast<int>((rect().height() - MINHEIGHT) / 3), 0), dateFontSize.size() - 1);
    return dateFontSize[index];
}

void DateTimeDisplayer::createMenuItem()
{
    QAction *timeFormatAction = new QAction(this);
    m_timedateInter->use24HourFormat() ? timeFormatAction->setText(tr("12-hour time"))
                                       : timeFormatAction->setText(tr("24-hour time"));

    connect(timeFormatAction, &QAction::triggered, this, [ = ] {
        m_timedateInter->setUse24HourFormat(!m_timedateInter->use24HourFormat());
        m_timedateInter->use24HourFormat() ? timeFormatAction->setText(tr("12-hour time"))
                                           : timeFormatAction->setText(tr("24-hour time"));
    });
    m_menu->addAction(timeFormatAction);

    if (!QFile::exists(ICBC_CONF_FILE)) {
        QAction *timeSettingAction = new QAction(tr("Time settings"), this);
        connect(timeSettingAction, &QAction::triggered, this, [ = ] {
            DDBusSender()
                    .service(controllCenterService)
                    .path(controllCenterPath)
                    .interface(controllCenterInterface)
                    .method(QString("ShowPage"))
                    .arg(QString("datetime"))
                    .call();
        });

        m_menu->addAction(timeSettingAction);
    }
}

QRect DateTimeDisplayer::textRect(const QRect &sourceRect) const
{
    // 如果是上下，则不做任何变化
    if (!m_showMultiRow && (m_position == Dock::Position::Top || m_position == Dock::Position::Bottom))
        return sourceRect;

    QRect resultRect = sourceRect;
    QSize size = suitableSize();
    // 如果是左右或者上下多行显示，设置宽度
    resultRect.setWidth(size.width());
    return resultRect;
}

void DateTimeDisplayer::enterEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_isEnter = true;
    update();
    m_tipPopupWindow->show(tipsPoint());
}

void DateTimeDisplayer::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_isEnter = false;
    update();
    m_tipPopupWindow->hide();
}

void DateTimeDisplayer::updateLastData(const DateTimeInfo &info)
{
    m_lastDateString = info.m_date;
    m_lastTimeString = info.m_time;
    QSize dateTimeSize = suitableSize();
    if (m_position == Dock::Position::Top || m_position == Dock::Position::Bottom)
        m_currentSize = dateTimeSize.width();
    else
        m_currentSize = dateTimeSize.height();
}

QString DateTimeDisplayer::getTimeString() const
{
    return getTimeString(m_position);
}
