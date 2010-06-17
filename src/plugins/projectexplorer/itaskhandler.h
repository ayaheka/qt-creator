/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#ifndef PROJECTEXPLORER_ITASKHANDLER_H
#define PROJECTEXPLORER_ITASKHANDLER_H

#include "projectexplorer_export.h"

#include <QtCore/QObject>
#include <QtCore/QString>

QT_BEGIN_NAMESPACE
class QAction;
class QObject;
QT_END_NAMESPACE

namespace ProjectExplorer {
class Task;

class PROJECTEXPLORER_EXPORT ITaskHandler : public QObject
{
    Q_OBJECT

public:
    explicit ITaskHandler(const QString &id) :
        m_id(id)
    { }

    QString id() const
    {
        return m_id;
    }

    virtual bool canHandle(const Task &) { return true; }
    virtual void handle(const Task &) = 0;
    virtual QAction *createAction(QObject *parent = 0) = 0;

private:
    const QString m_id;
};

} // namespace ProjectExplorer

#endif // PROJECTEXPLORER_ITASKHANDLER_H
