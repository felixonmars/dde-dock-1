/*
 * Copyright (C) 2022 ~ 2022 Deepin Technology Co., Ltd.
 *
 * Author:     donghualin <donghualin@uniontech.com>
 *
 * Maintainer:  donghualin <donghualin@uniontech.com>
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
#include "indicatorplugin.h"

#include <QLabel>
#include <QDBusConnection>
#include <QJsonObject>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QApplication>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include <QDBusMessage>
#include <thread>

class IndicatorPluginPrivate
{
public:
    explicit IndicatorPluginPrivate(IndicatorPlugin *parent) : q_ptr(parent) {}

    void init();

    void updateContent();

    void initDBus(const QString &indicatorName);

    template<typename Func>
    void featData(const QString &key,
                  const QJsonObject &data,
                  const char *propertyChangedSlot,
                  Func const &callback)
    {
        Q_Q(IndicatorPlugin);
        auto dataConfig = data.value(key).toObject();
        auto dbusService = dataConfig.value("dbus_service").toString();
        auto dbusPath = dataConfig.value("dbus_path").toString();
        auto dbusInterface = dataConfig.value("dbus_interface").toString();
        auto isSystemBus = dataConfig.value("system_dbus").toBool(false);
        auto bus = isSystemBus ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();

        QDBusInterface interface(dbusService, dbusPath, dbusInterface, bus, q);

        if (dataConfig.contains("dbus_method")) {
            QString methodName = dataConfig.value("dbus_method").toString();
            auto ratio = qApp->devicePixelRatio();
            QDBusReply<QByteArray> reply = interface.call(methodName.toStdString().c_str(), ratio);
            callback(reply.value());
        }

        if (dataConfig.contains("dbus_properties")) {
            auto propertyName = dataConfig.value("dbus_properties").toString();
            auto propertyNameCStr = propertyName.toStdString();
            propertyInterfaceNames.insert(key, dbusInterface);
            propertyNames.insert(key, QString::fromStdString(propertyNameCStr));
            QDBusConnection::sessionBus().connect(dbusService,
                                                  dbusPath,
                                                  "org.freedesktop.DBus.Properties",
                                                  "PropertiesChanged",
                                                  "sa{sv}as",
                                                  q,
                                                  propertyChangedSlot);

            // FIXME(sbw): hack for qt dbus property changed signal.
            // see: https://bugreports.qt.io/browse/QTBUG-48008
            QDBusConnection::sessionBus().connect(dbusService,
                                                  dbusPath,
                                                  dbusInterface,
                                                  QString("%1Changed").arg(propertyName),
                                                  "s",
                                                  q,
                                                  propertyChangedSlot);

            callback(interface.property(propertyNameCStr.c_str()));
        }
    }

    template<typename Func>
    void propertyChanged(const QString &key, const QDBusMessage &msg, Func const &callback)
    {
        QList<QVariant> arguments = msg.arguments();
        if (1 == arguments.count())
        {
            const QString &v = msg.arguments().at(0).toString();
            callback(v);
            return;
        } else if (3 != arguments.count()) {
            qDebug() << "arguments count must be 3";
            return;
        }

        QString interfaceName = msg.arguments().at(0).toString();
        if (interfaceName != propertyInterfaceNames.value(key)) {
            qDebug() << "interfaceName mismatch" << interfaceName << propertyInterfaceNames.value(key) << key;
            return;
        }
        QVariantMap changedProps = qdbus_cast<QVariantMap>(arguments.at(1).value<QDBusArgument>());
        if (changedProps.contains(propertyNames.value(key))) {
            callback(changedProps.value(propertyNames.value(key)));
        }
    }

    IndicatorTrayItem*    indicatorTrayWidget = Q_NULLPTR;
    QString                 indicatorName;
    QMap<QString, QString>  propertyNames;
    QMap<QString, QString>  propertyInterfaceNames;

    IndicatorPlugin *q_ptr;
    Q_DECLARE_PUBLIC(IndicatorPlugin)
};

void IndicatorPluginPrivate::init()
{
    //Q_Q(IndicatorTray);

    indicatorTrayWidget = new IndicatorTrayItem(indicatorName);

    initDBus(indicatorName);
    updateContent();
}

void IndicatorPluginPrivate::updateContent()
{
    indicatorTrayWidget->update();

    Q_EMIT indicatorTrayWidget->iconChanged();
}

void IndicatorPluginPrivate::initDBus(const QString &indicatorName)
{
    Q_Q(IndicatorPlugin);

    QString filepath = QString("/etc/dde-dock/indicator/%1.json").arg(indicatorName);
    QFile confFile(filepath);
    if (!confFile.open(QIODevice::ReadOnly)) {
        qCritical() << "read indicator config Error";
    }

    QJsonDocument doc = QJsonDocument::fromJson(confFile.readAll());
    confFile.close();
    auto config = doc.object();

    auto delay = config.value("delay").toInt(0);

    qDebug() << "delay load" << delay << indicatorName << q;

    QTimer::singleShot(delay, [ = ]() {
        auto data = config.value("data").toObject();

        if (data.contains("text")) {
            featData("text", data, SLOT(textPropertyChanged(QDBusMessage)), [ = ](QVariant v) {
                if (v.toString().isEmpty()) {
                    q->m_isLoaded = false;
                    Q_EMIT q->removed();
                    return;
                }
                q->m_isLoaded = true;
                Q_EMIT q->delayLoaded();
                indicatorTrayWidget->setText(v.toString());
                updateContent();
            });
        }

        if (data.contains("icon")) {
            featData("icon", data, SLOT(iconPropertyChanged(QDBusMessage)), [ = ](QVariant v) {
                if (v.toByteArray().isEmpty()) {
                    q->m_isLoaded = false;
                    Q_EMIT q->removed();
                    return;
                }
                q->m_isLoaded = true;
                Q_EMIT q->delayLoaded();
                indicatorTrayWidget->setPixmapData(v.toByteArray());
                updateContent();
            });
        }

        const QJsonObject action = config.value("action").toObject();
        if (!action.isEmpty() && indicatorTrayWidget)
            q->connect(indicatorTrayWidget, &IndicatorTrayItem::clicked, q, [ = ](uint8_t button_index, int x, int y) {
                std::thread t([ = ] ()-> void {
                    auto triggerConfig = action.value("trigger").toObject();
                    auto dbusService = triggerConfig.value("dbus_service").toString();
                    auto dbusPath = triggerConfig.value("dbus_path").toString();
                    auto dbusInterface = triggerConfig.value("dbus_interface").toString();
                    auto methodName = triggerConfig.value("dbus_method").toString();
                    auto isSystemBus = triggerConfig.value("system_dbus").toBool(false);
                    auto bus = isSystemBus ? QDBusConnection::systemBus() : QDBusConnection::sessionBus();

                    QDBusInterface interface(dbusService, dbusPath, dbusInterface, bus);
                    QDBusReply<void> reply = interface.call(methodName, button_index, x, y);
                    qDebug() << (reply.isValid() ? reply.error() : interface.call(methodName));
                });
                t.detach();
            });
    });
}

IndicatorPlugin::IndicatorPlugin(const QString &indicatorName, QObject *parent)
    : QObject(parent)
    , d_ptr(new IndicatorPluginPrivate(this))
    , m_isLoaded(false)
{
    Q_D(IndicatorPlugin);

    d->indicatorName = indicatorName;
    d->init();
}

IndicatorPlugin::~IndicatorPlugin()
{

}

IndicatorTrayItem *IndicatorPlugin::widget()
{
    Q_D(IndicatorPlugin);

    if (!d->indicatorTrayWidget) {
        d->init();
    }

    return d->indicatorTrayWidget;
}

void IndicatorPlugin::removeWidget()
{
    Q_D(IndicatorPlugin);

    d->indicatorTrayWidget = nullptr;
}

bool IndicatorPlugin::isLoaded()
{
    return m_isLoaded;
}

void IndicatorPlugin::textPropertyChanged(const QDBusMessage &message)
{
    Q_D(IndicatorPlugin);

    d->propertyChanged("text", message, [ = ] (const QVariant &value) {
        if (value.toString().isEmpty()) {
            m_isLoaded = false;
            Q_EMIT removed();
            return;
        }

        if (!d->indicatorTrayWidget) {
            d->init();
        }

        d->indicatorTrayWidget->setText(value.toByteArray());
        Q_EMIT delayLoaded();
    });
}

void IndicatorPlugin::iconPropertyChanged(const QDBusMessage &message)
{
    Q_D(IndicatorPlugin);

    d->propertyChanged("icon", message, [ = ] (const QVariant &value) {
        if (value.toByteArray().isEmpty()) {
            m_isLoaded = false;
            Q_EMIT removed();
            return;
        }

        if (!d->indicatorTrayWidget) {
            d->init();
        }

        d->indicatorTrayWidget->setPixmapData(value.toByteArray());
        Q_EMIT delayLoaded();
    });
}
