/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QVIDEOWINDOWCONTROL_H
#define QVIDEOWINDOWCONTROL_H

#include "qmediacontrol.h"
#include <QtCore/qrect.h>
#include <QtCore/qsize.h>
#include <QtGui/qwindowdefs.h>

QT_BEGIN_HEADER

QT_BEGIN_NAMESPACE

QT_MODULE(Multimedia)

// Required for QDoc workaround
class QString;

class Q_MULTIMEDIA_EXPORT QVideoWindowControl : public QMediaControl
{
    Q_OBJECT

public:
    ~QVideoWindowControl();

    virtual WId winId() const = 0;
    virtual void setWinId(WId id) = 0;

    virtual QRect displayRect() const = 0;
    virtual void setDisplayRect(const QRect &rect) = 0;

    virtual bool isFullScreen() const = 0;
    virtual void setFullScreen(bool fullScreen) = 0;

    virtual void repaint() = 0;

    virtual QSize nativeSize() const = 0;

    virtual Qt::AspectRatioMode aspectRatioMode() const = 0;
    virtual void setAspectRatioMode(Qt::AspectRatioMode mode) = 0;

    virtual int brightness() const = 0;
    virtual void setBrightness(int brightness) = 0;

    virtual int contrast() const = 0;
    virtual void setContrast(int contrast) = 0;

    virtual int hue() const = 0;
    virtual void setHue(int hue) = 0;

    virtual int saturation() const = 0;
    virtual void setSaturation(int saturation) = 0;

Q_SIGNALS:
    void fullScreenChanged(bool fullScreen);
    void brightnessChanged(int brightness);
    void contrastChanged(int contrast);
    void hueChanged(int hue);
    void saturationChanged(int saturation);
    void nativeSizeChanged();

protected:
    QVideoWindowControl(QObject *parent = 0);
};

#define QVideoWindowControl_iid "org.qt-project.qt.videowindowcontrol/5.0"
Q_MEDIA_DECLARE_CONTROL(QVideoWindowControl, QVideoWindowControl_iid)

QT_END_NAMESPACE

QT_END_HEADER


#endif
