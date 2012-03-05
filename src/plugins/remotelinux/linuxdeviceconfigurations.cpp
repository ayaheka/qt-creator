/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/
#include "linuxdeviceconfigurations.h"

#include "remotelinuxutils.h"

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/persistentsettings.h>
#include <utils/qtcassert.h>

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QSettings>
#include <QString>
#include <QVariantHash>
#include <QVariantList>

#include <algorithm>
#include <limits>

namespace RemoteLinux {
namespace Internal {

const char FileFormatVersionKey[] = "DeviceManagerFileFormatVersion";
const char DeviceManagerKey[] = "DeviceManager";
const char DeviceListKey[] = "DeviceList";
const char DefaultKeyFilePathKey[] = "DefaultKeyFile";
const char DefaultDevicesKey[] = "DefaultDevices";

namespace {

class DevConfNameMatcher
{
public:
    DevConfNameMatcher(const QString &name) : m_name(name) {}
    bool operator()(const LinuxDeviceConfiguration::ConstPtr &devConfig)
    {
        return devConfig->displayName() == m_name;
    }
private:
    const QString m_name;
};

} // anonymous namespace

class LinuxDeviceConfigurationsPrivate
{
public:
    static LinuxDeviceConfigurations *instance;
    static LinuxDeviceConfigurations *clonedInstance;
    QList<LinuxDeviceConfiguration::Ptr> devConfigs;
    QHash<QString, LinuxDeviceConfiguration::Id> defaultConfigs;
    QString defaultSshKeyFilePath;
};
LinuxDeviceConfigurations *LinuxDeviceConfigurationsPrivate::instance = 0;
LinuxDeviceConfigurations *LinuxDeviceConfigurationsPrivate::clonedInstance = 0;

} // namespace Internal

using namespace Internal;


LinuxDeviceConfigurations *LinuxDeviceConfigurations::instance(QObject *parent)
{
    if (LinuxDeviceConfigurationsPrivate::instance == 0) {
        LinuxDeviceConfigurationsPrivate::instance = new LinuxDeviceConfigurations(parent);
        LinuxDeviceConfigurationsPrivate::instance->load();
    }
    return LinuxDeviceConfigurationsPrivate::instance;
}

int LinuxDeviceConfigurations::deviceCount() const
{
    return d->devConfigs.count();
}

void LinuxDeviceConfigurations::replaceInstance()
{
    QTC_ASSERT(LinuxDeviceConfigurationsPrivate::instance, return);

    copy(LinuxDeviceConfigurationsPrivate::clonedInstance,
        LinuxDeviceConfigurationsPrivate::instance, false);
    LinuxDeviceConfigurationsPrivate::instance->save();
    emit LinuxDeviceConfigurationsPrivate::instance->deviceListChanged();
    emit LinuxDeviceConfigurationsPrivate::instance->updated();
}

void LinuxDeviceConfigurations::removeClonedInstance()
{
    delete LinuxDeviceConfigurationsPrivate::clonedInstance;
    LinuxDeviceConfigurationsPrivate::clonedInstance = 0;
}

LinuxDeviceConfigurations *LinuxDeviceConfigurations::cloneInstance()
{
    QTC_ASSERT(!LinuxDeviceConfigurationsPrivate::clonedInstance, return 0);

    LinuxDeviceConfigurationsPrivate::clonedInstance
        = new LinuxDeviceConfigurations(LinuxDeviceConfigurationsPrivate::instance);
    copy(LinuxDeviceConfigurationsPrivate::instance,
        LinuxDeviceConfigurationsPrivate::clonedInstance, true);
    return LinuxDeviceConfigurationsPrivate::clonedInstance;
}

void LinuxDeviceConfigurations::copy(const LinuxDeviceConfigurations *source,
    LinuxDeviceConfigurations *target, bool deep)
{
    if (deep) {
        foreach (const LinuxDeviceConfiguration::ConstPtr &devConf, source->d->devConfigs)
            target->d->devConfigs << devConf->clone();
    } else {
        target->d->devConfigs = source->d->devConfigs;
    }
    target->d->defaultSshKeyFilePath = source->d->defaultSshKeyFilePath;
    target->d->defaultConfigs = source->d->defaultConfigs;
}

void LinuxDeviceConfigurations::save()
{
    Utils::PersistentSettingsWriter writer;
    writer.saveValue(QLatin1String(FileFormatVersionKey), 1);
    writer.saveValue(QLatin1String(DeviceManagerKey), toMap());
    writer.save(settingsFilePath(), QLatin1String("QtCreatorDevices"), Core::ICore::mainWindow());
}

void LinuxDeviceConfigurations::load()
{
    Utils::PersistentSettingsReader reader;
    if (reader.load(settingsFilePath())) {
        const QVariantMap data = reader.restoreValues();
        const int version = data.value(QLatin1String(FileFormatVersionKey), 0).toInt();
        if (version < 1)
            return;
        fromMap(data.value(QLatin1String(DeviceManagerKey)).toMap());
    } else {
        loadPre2_6();
    }
    ensureOneDefaultConfigurationPerDeviceType();
}

void LinuxDeviceConfigurations::loadPre2_6()
{
    QSettings *settings = Core::ICore::settings();
    settings->beginGroup(QLatin1String("MaemoDeviceConfigs"));
    d->defaultSshKeyFilePath = settings->value(QLatin1String(DefaultKeyFilePathKey),
        LinuxDeviceConfiguration::defaultPrivateKeyFilePath()).toString();
    const QVariantHash defaultDevsHash = settings->value(QLatin1String("DefaultConfigs")).toHash();
    for (QVariantHash::ConstIterator it = defaultDevsHash.constBegin();
            it != defaultDevsHash.constEnd(); ++it) {
        d->defaultConfigs.insert(it.key(), it.value().toULongLong());
    }
    int count = settings->beginReadArray(QLatin1String("ConfigList"));
    for (int i = 0; i < count; ++i) {
        settings->setArrayIndex(i);
        LinuxDeviceConfiguration::Ptr devConf = LinuxDeviceConfiguration::create(*settings);
        if (devConf->internalId() == LinuxDeviceConfiguration::InvalidId)
            devConf->setInternalId(unusedId());
        d->devConfigs << devConf;
    }
    settings->endArray();
    settings->endGroup();
}

void LinuxDeviceConfigurations::fromMap(const QVariantMap &map)
{
    d->defaultSshKeyFilePath = map.value(QLatin1String(DefaultKeyFilePathKey),
        LinuxDeviceConfiguration::defaultPrivateKeyFilePath()).toString();
    const QVariantMap defaultDevsMap = map.value(QLatin1String(DefaultDevicesKey)).toMap();
    for (QVariantMap::ConstIterator it = defaultDevsMap.constBegin();
         it != defaultDevsMap.constEnd(); ++it) {
        d->defaultConfigs.insert(it.key(), it.value().toULongLong());
    }
    const QVariantList deviceList = map.value(QLatin1String(DeviceListKey)).toList();
    foreach (const QVariant &v, deviceList) {
        LinuxDeviceConfiguration::Ptr device = LinuxDeviceConfiguration::create();
        device->fromMap(v.toMap());
        d->devConfigs << device;
    }
}

QVariantMap LinuxDeviceConfigurations::toMap() const
{
    QVariantMap map;
    map.insert(QLatin1String(DefaultKeyFilePathKey), d->defaultSshKeyFilePath);
    QVariantMap defaultDeviceMap;
    typedef QHash<QString, LinuxDeviceConfiguration::Id> TypeIdHash;
    for (TypeIdHash::ConstIterator it = d->defaultConfigs.constBegin();
             it != d->defaultConfigs.constEnd(); ++it) {
        defaultDeviceMap.insert(it.key(), it.value());
    }
    map.insert(QLatin1String(DefaultDevicesKey), defaultDeviceMap);
    QVariantList deviceList;
    foreach (const LinuxDeviceConfiguration::ConstPtr &device, d->devConfigs) {
        if (!device->isAutoDetected())
            deviceList << device->toMap();
    }
    map.insert(QLatin1String(DeviceListKey), deviceList);
    return map;
}

QString LinuxDeviceConfigurations::settingsFilePath()
{
    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    return QFileInfo(pm->settings()->fileName()).absolutePath() + QLatin1String("/devices.xml");
}

void LinuxDeviceConfigurations::addConfiguration(const LinuxDeviceConfiguration::Ptr &devConfig)
{
    QTC_ASSERT(this != LinuxDeviceConfigurationsPrivate::instance
        || (devConfig->isAutoDetected()), return);

    // Ensure uniqueness of name.
    QString name = devConfig->displayName();
    if (hasConfig(name)) {
        const QString nameTemplate = name + QLatin1String(" (%1)");
        int suffix = 2;
        do
            name = nameTemplate.arg(QString::number(suffix++));
        while (hasConfig(name));
    }
    devConfig->setDisplayName(name);
    devConfig->setInternalId(unusedId());
    if (!defaultDeviceConfig(devConfig->type()))
        d->defaultConfigs.insert(devConfig->type(), devConfig->internalId());
    d->devConfigs << devConfig;
    if (this == d->instance && d->clonedInstance) {
        d->clonedInstance->addConfiguration(
            LinuxDeviceConfiguration::Ptr(new LinuxDeviceConfiguration(*devConfig)));
    }
    emit deviceAdded(devConfig);
    emit updated();
}

void LinuxDeviceConfigurations::removeConfiguration(int idx)
{
    QTC_ASSERT(idx >= 0 && idx < deviceCount(), return);
    const LinuxDeviceConfiguration::ConstPtr deviceConfig = deviceAt(idx);
    QTC_ASSERT(this != LinuxDeviceConfigurationsPrivate::instance
        || deviceConfig->isAutoDetected(), return);

    const bool wasDefault
        = d->defaultConfigs.value(deviceConfig->type()) == deviceConfig->internalId();
    const QString deviceType = deviceConfig->type();
    d->devConfigs.removeAt(idx);

    emit deviceRemoved(idx);

    if (wasDefault) {
        for (int i = 0; i < d->devConfigs.count(); ++i) {
            if (deviceAt(i)->type() == deviceType) {
                d->defaultConfigs.insert(deviceAt(i)->type(), deviceAt(i)->internalId());
                emit defaultStatusChanged(i);
                break;
            }
        }
    }
    if (this == d->instance && d->clonedInstance) {
        d->clonedInstance->removeConfiguration(d->clonedInstance->
            indexForInternalId(deviceConfig->internalId()));
    }

    emit updated();
}

void LinuxDeviceConfigurations::setDefaultSshKeyFilePath(const QString &path)
{
    QTC_ASSERT(this != LinuxDeviceConfigurationsPrivate::instance, return);

    d->defaultSshKeyFilePath = path;
}

QString LinuxDeviceConfigurations::defaultSshKeyFilePath() const
{
    return d->defaultSshKeyFilePath;
}

void LinuxDeviceConfigurations::setConfigurationName(int i, const QString &name)
{
    QTC_ASSERT(this != LinuxDeviceConfigurationsPrivate::instance, return);
    QTC_ASSERT(i >= 0 && i < deviceCount(), return);

    d->devConfigs.at(i)->setDisplayName(name);
    emit displayNameChanged(i);
}

void LinuxDeviceConfigurations::setDefaultDevice(int idx)
{
    QTC_ASSERT(this != LinuxDeviceConfigurationsPrivate::instance, return);
    QTC_ASSERT(idx >= 0 && idx < deviceCount(), return);

    const LinuxDeviceConfiguration::ConstPtr &devConf = d->devConfigs.at(idx);
    const LinuxDeviceConfiguration::ConstPtr &oldDefaultDevConf
        = defaultDeviceConfig(devConf->type());
    if (devConf == oldDefaultDevConf)
        return;
    d->defaultConfigs.insert(devConf->type(), devConf->internalId());
    emit defaultStatusChanged(idx);
    for (int i = 0; i < d->devConfigs.count(); ++i) {
        if (d->devConfigs.at(i) == oldDefaultDevConf) {
            emit defaultStatusChanged(i);
            break;
        }
    }

    emit updated();
}

LinuxDeviceConfigurations::LinuxDeviceConfigurations(QObject *parent)
    : QObject(parent), d(new LinuxDeviceConfigurationsPrivate)
{
}

LinuxDeviceConfiguration::Ptr LinuxDeviceConfigurations::mutableDeviceAt(int idx) const
{
    QTC_ASSERT(idx >= 0 && idx < deviceCount(), return LinuxDeviceConfiguration::Ptr());
    return d->devConfigs.at(idx);
}

LinuxDeviceConfigurations::~LinuxDeviceConfigurations()
{
    delete d;
}

LinuxDeviceConfiguration::ConstPtr LinuxDeviceConfigurations::deviceAt(int idx) const
{
    QTC_ASSERT(idx >= 0 && idx < deviceCount(), return LinuxDeviceConfiguration::ConstPtr());
    return d->devConfigs.at(idx);
}

bool LinuxDeviceConfigurations::hasConfig(const QString &name) const
{
    QList<LinuxDeviceConfiguration::Ptr>::ConstIterator resultIt =
        std::find_if(d->devConfigs.constBegin(), d->devConfigs.constEnd(),
            DevConfNameMatcher(name));
    return resultIt != d->devConfigs.constEnd();
}

LinuxDeviceConfiguration::ConstPtr LinuxDeviceConfigurations::find(LinuxDeviceConfiguration::Id id) const
{
    const int index = indexForInternalId(id);
    return index == -1 ? LinuxDeviceConfiguration::ConstPtr() : deviceAt(index);
}

LinuxDeviceConfiguration::ConstPtr LinuxDeviceConfigurations::defaultDeviceConfig(const QString &deviceType) const
{
    const LinuxDeviceConfiguration::Id id = d->defaultConfigs.value(deviceType,
        LinuxDeviceConfiguration::InvalidId);
    if (id == LinuxDeviceConfiguration::InvalidId)
        return LinuxDeviceConfiguration::ConstPtr();
    return find(id);
}

int LinuxDeviceConfigurations::indexForInternalId(LinuxDeviceConfiguration::Id internalId) const
{
    for (int i = 0; i < d->devConfigs.count(); ++i) {
        if (deviceAt(i)->internalId() == internalId)
            return i;
    }
    return -1;
}

LinuxDeviceConfiguration::Id LinuxDeviceConfigurations::internalId(LinuxDeviceConfiguration::ConstPtr devConf) const
{
    return devConf ? devConf->internalId() : LinuxDeviceConfiguration::InvalidId;
}

void LinuxDeviceConfigurations::ensureOneDefaultConfigurationPerDeviceType()
{
    foreach (const LinuxDeviceConfiguration::Ptr &devConf, d->devConfigs) {
        if (!defaultDeviceConfig(devConf->type()))
            d->defaultConfigs.insert(devConf->type(), devConf->internalId());
    }
}

LinuxDeviceConfiguration::Id LinuxDeviceConfigurations::unusedId() const
{
    typedef LinuxDeviceConfiguration::Id IdType;
    for (IdType id = 0; id <= std::numeric_limits<IdType>::max(); ++id) {
        if (id != LinuxDeviceConfiguration::InvalidId && !find(id))
            return id;
    }
    QTC_CHECK(false);
    return LinuxDeviceConfiguration::InvalidId;
}

} // namespace RemoteLinux
