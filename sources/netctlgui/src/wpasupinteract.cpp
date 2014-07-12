/***************************************************************************
 *   This file is part of netctl-gui                                       *
 *                                                                         *
 *   netctl-gui is free software: you can redistribute it and/or           *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation, either version 3 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   netctl-gui is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with netctl-gui. If not, see http://www.gnu.org/licenses/       *
 ***************************************************************************/

#include <QDebug>
#include <QProcess>

#include <netctlgui/netctlinteract.h>
#include <netctlgui/sleepthread.h>
#include <netctlgui/wpasupinteract.h>


WpaSup::WpaSup(const bool debugCmd, const QMap<QString, QString> settings)
    : debug(debugCmd)
{
    netctlCommand = new Netctl(debug, settings);

    ctrlDir = settings[QString("CTRL_DIR")];
    ctrlGroup = settings[QString("CTRL_GROUP")];
    ifaceDirectory = new QDir(settings[QString("IFACE_DIR")]);
    mainInterface = settings[QString("PREFERED_IFACE")];
    pidFile = settings[QString("PID_FILE")];
    sudoCommand = settings[QString("SUDO_PATH")];
    wpaCliPath = settings[QString("WPACLI_PATH")];
    wpaDrivers = settings[QString("WPA_DRIVERS")];
    wpaSupPath = settings[QString("WPASUP_PATH")];

    // terminate old loaded profile
    if (QFile(pidFile).exists() || QDir(ctrlDir).exists())
        stopWpaSupplicant();
}


WpaSup::~WpaSup()
{
    if (debug) qDebug() << "[WpaSup]" << "[~WpaSup]";

    delete netctlCommand;
    delete ifaceDirectory;
}


// general information
QString WpaSup::existentProfile(const QString profile)
{
    if (debug) qDebug() << "[WpaSup]" << "[existentProfile]";

    QString profileFile = QString("");
    QList<QStringList> profileList = netctlCommand->getProfileList();
    for (int i=0; i<profileList.count(); i++)
        if (profile == netctlCommand->getSsidFromProfile(profileList[i][0]))
            profileFile = profileList[i][0];

    return profileFile;
}


QStringList WpaSup::getInterfaceList()
{
    if (debug) qDebug() << "[WpaSup]" << "[getInterfaceList]";

    QStringList interfaces;

    if (!mainInterface.isEmpty())
        interfaces.append(mainInterface);
    QStringList allInterfaces = ifaceDirectory->entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (int i=0; i<allInterfaces.count(); i++) {
        if (debug) qDebug() << "[WpaSup]" << "[getInterfaceList]" << ":" << "Check directory"
                 << ifaceDirectory->path() + QDir::separator() + allInterfaces[i] + QDir::separator() + QString("wireless");
        if (QDir(ifaceDirectory->path() + QDir::separator() + allInterfaces[i] +
                 QDir::separator() + QString("wireless")).exists())
            interfaces.append(allInterfaces[i]);
    }

    return interfaces;
}


bool WpaSup::isProfileActive(const QString profile)
{
    if (debug) qDebug() << "[WpaSup]" << "[isProfileActive]";

    QString profileFile;
    QList<QStringList> profileList = netctlCommand->getProfileList();
    for (int i=0; i<profileList.count(); i++)
        if (profile == netctlCommand->getSsidFromProfile(profileList[i][0]))
            profileFile = profileList[i][0];

    return netctlCommand->isProfileActive(profileFile);
}


bool WpaSup::isProfileExists(const QString profile)
{
    if (debug) qDebug() << "[WpaSup]" << "[isProfileExists]";

    bool exists = false;
    QList<QStringList> profileList = netctlCommand->getProfileList();
    for (int i=0; i<profileList.count(); i++)
        if (profile == netctlCommand->getSsidFromProfile(profileList[i][0]))
            exists = true;

    return exists;
}


// functions
bool WpaSup::wpaCliCall(const QString commandLine)
{
    if (debug) qDebug() << "[WpaSup]" << "[getInterfaceList]";

    QString interface = getInterfaceList()[0];
    QProcess command;
    QString commandText = wpaCliPath + QString(" -i ") + interface + QString(" -p ") + ctrlDir +
            QString(" -P ") + pidFile + QString(" ") + commandLine;
    if (debug) qDebug() << "[WpaSup]" << "[wpaCliCall]" << ":" << "Run cmd" << commandText;
    command.start(commandText);
    command.waitForFinished(-1);
    SleepThread::sleep(1);
    if (debug) qDebug() << "[WpaSup]" << "[wpaCliCall]" << ":" << "Cmd returns" << command.exitCode();
    if (command.exitCode() == 0)
        return true;
    else
        return false;
}


QString WpaSup::getWpaCliOutput(const QString commandLine)
{
    if (debug) qDebug() << "[WpaSup]" << "[getWpaCliOutput]";

    QString interface = getInterfaceList()[0];
    QProcess command;
    QString commandText = wpaCliPath + QString(" -i ") + interface + QString(" -p ") + ctrlDir +
            QString(" -P ") + pidFile + QString(" ") + commandLine;
    if (debug) qDebug() << "[WpaSup]" << "[getWpaCliOutput]" << ":" << "Run cmd" << commandText;
    command.start(commandText);
    command.waitForFinished(-1);

    return command.readAllStandardOutput();
}


QList<QStringList> WpaSup::scanWifi()
{
    if (debug) qDebug() << "[WpaSup]" << "[scanWifi]";

    QList<QStringList> scanResults;
    if (!startWpaSupplicant()) {
        stopWpaSupplicant();
        return scanResults;
    }
    if (!wpaCliCall(QString("scan")))
        return scanResults;
    SleepThread::sleep(3);

    QStringList rawOutput = getWpaCliOutput(QString("scan_results")).split(QChar('\n'), QString::SkipEmptyParts);
    // remove table header
    rawOutput.removeFirst();
    // remove duplicates
    QStringList rawList;
    for (int i=0; i<rawOutput.count(); i++) {
        bool exist = false;
        if (rawOutput[i].split(QChar('\t'), QString::SkipEmptyParts).count() > 4)
            for (int j=0; j<rawList.count(); j++)
                if (rawList[j].split(QChar('\t'), QString::SkipEmptyParts).count() > 4)
                    if (rawOutput[i].split(QChar('\t'), QString::SkipEmptyParts)[4] ==
                            rawList[j].split(QChar('\t'), QString::SkipEmptyParts)[4])
                        exist = true;
        if (!exist)
            rawList.append(rawOutput[i]);
    }

    for (int i=0; i<rawList.count(); i++) {
        QStringList wifiPoint;

        // point name
        if (rawList[i].split(QChar('\t'), QString::SkipEmptyParts).count() > 4)
            wifiPoint.append(rawList[i].split(QChar('\t'), QString::SkipEmptyParts)[4]);
        else
            wifiPoint.append(QString("<hidden>"));
        // profile status
        QString status;
        if (isProfileExists(wifiPoint[0])) {
            status = QString("exists");
            if (isProfileActive(wifiPoint[0]))
                status = status + QString(" (active)");
            else
                status = status + QString(" (inactive)");
        }
        else
            status = QString("new");
        wifiPoint.append(status);
        // point signal
        wifiPoint.append(rawList[i].split(QChar('\t'), QString::SkipEmptyParts)[2]);
        // point security
        QString security = rawList[i].split(QChar('\t'), QString::SkipEmptyParts)[3];
        if (security.contains(QString("WPA2")))
            security = QString("WPA2");
        else if (security.contains(QString("WPA")))
            security = QString("WPA");
        else if (security.contains(QString("WEP")))
            security = QString("WEP");
        else
            security = QString("none");
        wifiPoint.append(security);

        scanResults.append(wifiPoint);
    }

    stopWpaSupplicant();
    return scanResults;
}


bool WpaSup::startWpaSupplicant()
{
    if (debug) qDebug() << "[WpaSup]" << "[startWpaSupplicant]";

    if (!QFile(pidFile).exists()) {
        QString interface = getInterfaceList()[0];
        QProcess command;
        QString commandText = sudoCommand + QString(" ") + wpaSupPath + QString(" -B -P ") + pidFile +
                QString(" -i ") + interface + QString(" -D ") + wpaDrivers +
                QString(" -C \"DIR=") + ctrlDir + QString(" GROUP=") + ctrlGroup + QString("\"");
        if (debug) qDebug() << "[WpaSup]" << "[startWpaSupplicant]" << ":" << "Run cmd" << commandText;
        command.start(commandText);
        command.waitForFinished(-1);
        SleepThread::sleep(1);
        if (debug) qDebug() << "[WpaSup]" << "[startWpaSupplicant]" << ":" << "Cmd returns" << command.exitCode();
        if (command.exitCode() != 0)
            return false;
    }

    return true;
}


bool WpaSup::stopWpaSupplicant()
{
    if (debug) qDebug() << "[WpaSup]" << "[stopWpaSupplicant]";

    return wpaCliCall(QString("terminate"));
}