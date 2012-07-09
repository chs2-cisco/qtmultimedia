/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists for the convenience
// of other Qt classes.  This header file may change from version to
// version without notice, or even be removed.
//
// INTERNAL USE ONLY: Do NOT use for any other purpose.
//

#include <QtCore/qendian.h>
#include <QtCore/qtimer.h>
#include <QtCore/qdebug.h>

#include <qaudioinput.h>

#include "qaudio_mac_p.h"
#include "qaudioinput_mac_p.h"
#include "qaudiodeviceinfo_mac_p.h"
#include "qaudiohelpers_p.h"

QT_BEGIN_NAMESPACE


namespace QtMultimediaInternal
{

static const int default_buffer_size = 4 * 1024;

class QAudioBufferList
{
public:
    QAudioBufferList(AudioStreamBasicDescription const& streamFormat):
        owner(false),
        sf(streamFormat)
    {
        const bool isInterleaved = (sf.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0;
        const int numberOfBuffers = isInterleaved ? 1 : sf.mChannelsPerFrame;

        dataSize = 0;

        bfs = reinterpret_cast<AudioBufferList*>(malloc(sizeof(AudioBufferList) +
                                                                (sizeof(AudioBuffer) * numberOfBuffers)));

        bfs->mNumberBuffers = numberOfBuffers;
        for (int i = 0; i < numberOfBuffers; ++i) {
            bfs->mBuffers[i].mNumberChannels = isInterleaved ? numberOfBuffers : 1;
            bfs->mBuffers[i].mDataByteSize = 0;
            bfs->mBuffers[i].mData = 0;
        }
    }

    QAudioBufferList(AudioStreamBasicDescription const& streamFormat, char* buffer, int bufferSize):
        owner(false),
        sf(streamFormat),
        bfs(0)
    {
        dataSize = bufferSize;

        bfs = reinterpret_cast<AudioBufferList*>(malloc(sizeof(AudioBufferList) + sizeof(AudioBuffer)));

        bfs->mNumberBuffers = 1;
        bfs->mBuffers[0].mNumberChannels = 1;
        bfs->mBuffers[0].mDataByteSize = dataSize;
        bfs->mBuffers[0].mData = buffer;
    }

    QAudioBufferList(AudioStreamBasicDescription const& streamFormat, int framesToBuffer):
        owner(true),
        sf(streamFormat),
        bfs(0)
    {
        const bool isInterleaved = (sf.mFormatFlags & kAudioFormatFlagIsNonInterleaved) == 0;
        const int numberOfBuffers = isInterleaved ? 1 : sf.mChannelsPerFrame;

        dataSize = framesToBuffer * sf.mBytesPerFrame;

        bfs = reinterpret_cast<AudioBufferList*>(malloc(sizeof(AudioBufferList) +
                                                                (sizeof(AudioBuffer) * numberOfBuffers)));
        bfs->mNumberBuffers = numberOfBuffers;
        for (int i = 0; i < numberOfBuffers; ++i) {
            bfs->mBuffers[i].mNumberChannels = isInterleaved ? numberOfBuffers : 1;
            bfs->mBuffers[i].mDataByteSize = dataSize;
            bfs->mBuffers[i].mData = malloc(dataSize);
        }
    }

    ~QAudioBufferList()
    {
        if (owner) {
            for (UInt32 i = 0; i < bfs->mNumberBuffers; ++i)
                free(bfs->mBuffers[i].mData);
        }

        free(bfs);
    }

    AudioBufferList* audioBufferList() const
    {
        return bfs;
    }

    char* data(int buffer = 0) const
    {
        return static_cast<char*>(bfs->mBuffers[buffer].mData);
    }

    qint64 bufferSize(int buffer = 0) const
    {
        return bfs->mBuffers[buffer].mDataByteSize;
    }

    int frameCount(int buffer = 0) const
    {
        return bfs->mBuffers[buffer].mDataByteSize / sf.mBytesPerFrame;
    }

    int packetCount(int buffer = 0) const
    {
        return bfs->mBuffers[buffer].mDataByteSize / sf.mBytesPerPacket;
    }

    int packetSize() const
    {
        return sf.mBytesPerPacket;
    }

    void reset()
    {
        for (UInt32 i = 0; i < bfs->mNumberBuffers; ++i) {
            bfs->mBuffers[i].mDataByteSize = dataSize;
            bfs->mBuffers[i].mData = 0;
        }
    }

private:
    bool    owner;
    int     dataSize;
    AudioStreamBasicDescription sf;
    AudioBufferList* bfs;
};

class QAudioPacketFeeder
{
public:
    QAudioPacketFeeder(QAudioBufferList* abl):
        audioBufferList(abl)
    {
        totalPackets = audioBufferList->packetCount();
        position = 0;
    }

    bool feed(AudioBufferList& dst, UInt32& packetCount)
    {
        if (position == totalPackets) {
            dst.mBuffers[0].mDataByteSize = 0;
            packetCount = 0;
            return false;
        }

        if (totalPackets - position < packetCount)
            packetCount = totalPackets - position;

        dst.mBuffers[0].mDataByteSize = packetCount * audioBufferList->packetSize();
        dst.mBuffers[0].mData = audioBufferList->data() + (position * audioBufferList->packetSize());

        position += packetCount;

        return true;
    }

    bool empty() const
    {
        return position == totalPackets;
    }

private:
    UInt32 totalPackets;
    UInt32 position;
    QAudioBufferList*   audioBufferList;
};

class QAudioInputBuffer : public QObject
{
    Q_OBJECT

public:
    QAudioInputBuffer(int bufferSize,
                        int maxPeriodSize,
                        AudioStreamBasicDescription const& inputFormat,
                        AudioStreamBasicDescription const& outputFormat,
                        QObject* parent):
        QObject(parent),
        m_deviceError(false),
        m_audioConverter(0),
        m_inputFormat(inputFormat),
        m_outputFormat(outputFormat),
        m_volume(qreal(1.0f))
    {
        m_maxPeriodSize = maxPeriodSize;
        m_periodTime = m_maxPeriodSize / m_outputFormat.mBytesPerFrame * 1000 / m_outputFormat.mSampleRate;

        m_buffer = new QAudioRingBuffer(bufferSize);

        m_inputBufferList = new QAudioBufferList(m_inputFormat);

        m_flushTimer = new QTimer(this);
        connect(m_flushTimer, SIGNAL(timeout()), SLOT(flushBuffer()));

        if (toQAudioFormat(inputFormat) != toQAudioFormat(outputFormat)) {
            if (AudioConverterNew(&m_inputFormat, &m_outputFormat, &m_audioConverter) != noErr) {
                qWarning() << "QAudioInput: Unable to create an Audio Converter";
                m_audioConverter = 0;
            }
        }

        m_qFormat = toQAudioFormat(inputFormat); // we adjust volume before conversion
    }

    ~QAudioInputBuffer()
    {
        delete m_buffer;
    }

    qreal volume() const
    {
        return m_volume;
    }

    void setVolume(qreal v)
    {
        m_volume = v;
    }

    qint64 renderFromDevice(AudioUnit audioUnit,
                             AudioUnitRenderActionFlags* ioActionFlags,
                             const AudioTimeStamp* inTimeStamp,
                             UInt32 inBusNumber,
                             UInt32 inNumberFrames)
    {
        const bool  pullMode = m_device == 0;

        OSStatus    err;
        qint64      framesRendered = 0;

        m_inputBufferList->reset();
        err = AudioUnitRender(audioUnit,
                                ioActionFlags,
                                inTimeStamp,
                                inBusNumber,
                                inNumberFrames,
                                m_inputBufferList->audioBufferList());

        // adjust volume, if necessary
        if (!qFuzzyCompare(m_volume, qreal(1.0f))) {
            QAudioHelperInternal::qMultiplySamples(m_volume,
                                                   m_qFormat,
                                                   m_inputBufferList->data(), /* input */
                                                   m_inputBufferList->data(), /* output */
                                                   m_inputBufferList->bufferSize());
        }

        if (m_audioConverter != 0) {
            QAudioPacketFeeder  feeder(m_inputBufferList);

            int     copied = 0;
            const int available = m_buffer->free();

            while (err == noErr && !feeder.empty()) {
                QAudioRingBuffer::Region region = m_buffer->acquireWriteRegion(available - copied);

                if (region.second == 0)
                    break;

                AudioBufferList     output;
                output.mNumberBuffers = 1;
                output.mBuffers[0].mNumberChannels = 1;
                output.mBuffers[0].mDataByteSize = region.second;
                output.mBuffers[0].mData = region.first;

                UInt32  packetSize = region.second / m_outputFormat.mBytesPerPacket;
                err = AudioConverterFillComplexBuffer(m_audioConverter,
                                                      converterCallback,
                                                      &feeder,
                                                      &packetSize,
                                                      &output,
                                                      0);
                region.second = output.mBuffers[0].mDataByteSize;
                copied += region.second;

                m_buffer->releaseWriteRegion(region);
            }

            framesRendered += copied / m_outputFormat.mBytesPerFrame;
        }
        else {
            const int available = m_inputBufferList->bufferSize();
            bool    wecan = true;
            int     copied = 0;

            while (wecan && copied < available) {
                QAudioRingBuffer::Region region = m_buffer->acquireWriteRegion(available - copied);

                if (region.second > 0) {
                    memcpy(region.first, m_inputBufferList->data() + copied, region.second);
                    copied += region.second;
                }
                else
                    wecan = false;

                m_buffer->releaseWriteRegion(region);
            }

            framesRendered = copied / m_outputFormat.mBytesPerFrame;
        }

        if (pullMode && framesRendered > 0)
            emit readyRead();

        return framesRendered;
    }

    qint64 readBytes(char* data, qint64 len)
    {
        bool    wecan = true;
        qint64  bytesCopied = 0;

        len -= len % m_maxPeriodSize;
        while (wecan && bytesCopied < len) {
            QAudioRingBuffer::Region region = m_buffer->acquireReadRegion(len - bytesCopied);

            if (region.second > 0) {
                memcpy(data + bytesCopied, region.first, region.second);
                bytesCopied += region.second;
            }
            else
                wecan = false;

            m_buffer->releaseReadRegion(region);
        }

        return bytesCopied;
    }

    void setFlushDevice(QIODevice* device)
    {
        if (m_device != device)
            m_device = device;
    }

    void startFlushTimer()
    {
        if (m_device != 0) {
            // We use the period time for the timer, since that's
            // around the buffer size (pre conversion >.>)
            m_flushTimer->start(qMax(1, m_periodTime));
        }
    }

    void stopFlushTimer()
    {
        m_flushTimer->stop();
    }

    void flush(bool all = false)
    {
        if (m_device == 0)
            return;

        const int used = m_buffer->used();
        const int readSize = all ? used : used - (used % m_maxPeriodSize);

        if (readSize > 0) {
            bool    wecan = true;
            int     flushed = 0;

            while (!m_deviceError && wecan && flushed < readSize) {
                QAudioRingBuffer::Region region = m_buffer->acquireReadRegion(readSize - flushed);

                if (region.second > 0) {
                    int bytesWritten = m_device->write(region.first, region.second);
                    if (bytesWritten < 0) {
                        stopFlushTimer();
                        m_deviceError = true;
                    }
                    else {
                        region.second = bytesWritten;
                        flushed += bytesWritten;
                        wecan = bytesWritten != 0;
                    }
                }
                else
                    wecan = false;

                m_buffer->releaseReadRegion(region);
            }
        }
    }

    void reset()
    {
        m_buffer->reset();
        m_deviceError = false;
    }

    int available() const
    {
        return m_buffer->free();
    }

    int used() const
    {
        return m_buffer->used();
    }

signals:
    void readyRead();

private slots:
    void flushBuffer()
    {
        flush();
    }

private:
    bool        m_deviceError;
    int         m_maxPeriodSize;
    int         m_periodTime;
    QIODevice*  m_device;
    QTimer*     m_flushTimer;
    QAudioRingBuffer*   m_buffer;
    QAudioBufferList*   m_inputBufferList;
    AudioConverterRef   m_audioConverter;
    AudioStreamBasicDescription m_inputFormat;
    AudioStreamBasicDescription m_outputFormat;
    QAudioFormat m_qFormat;
    qreal     m_volume;

    const static OSStatus as_empty = 'qtem';

    // Converter callback
    static OSStatus converterCallback(AudioConverterRef inAudioConverter,
                                UInt32* ioNumberDataPackets,
                                AudioBufferList* ioData,
                                AudioStreamPacketDescription** outDataPacketDescription,
                                void* inUserData)
    {
        Q_UNUSED(inAudioConverter);
        Q_UNUSED(outDataPacketDescription);

        QAudioPacketFeeder* feeder = static_cast<QAudioPacketFeeder*>(inUserData);

        if (!feeder->feed(*ioData, *ioNumberDataPackets))
            return as_empty;

        return noErr;
    }
};


class MacInputDevice : public QIODevice
{
    Q_OBJECT

public:
    MacInputDevice(QAudioInputBuffer* audioBuffer, QObject* parent):
        QIODevice(parent),
        m_audioBuffer(audioBuffer)
    {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        connect(m_audioBuffer, SIGNAL(readyRead()), SIGNAL(readyRead()));
    }

    qint64 readData(char* data, qint64 len)
    {
        return m_audioBuffer->readBytes(data, len);
    }

    qint64 writeData(const char* data, qint64 len)
    {
        Q_UNUSED(data);
        Q_UNUSED(len);

        return 0;
    }

    bool isSequential() const
    {
        return true;
    }

private:
    QAudioInputBuffer*   m_audioBuffer;
};

}


QAudioInputPrivate::QAudioInputPrivate(const QByteArray& device)
{
    QDataStream ds(device);
    quint32 did, mode;

    ds >> did >> mode;

    if (QAudio::Mode(mode) == QAudio::AudioOutput)
        errorCode = QAudio::OpenError;
    else {
        audioDeviceInfo = new QAudioDeviceInfoInternal(device, QAudio::AudioInput);
        isOpen = false;
        audioDeviceId = AudioDeviceID(did);
        audioUnit = 0;
        startTime = 0;
        totalFrames = 0;
        audioBuffer = 0;
        internalBufferSize = QtMultimediaInternal::default_buffer_size;
        clockFrequency = AudioGetHostClockFrequency() / 1000;
        errorCode = QAudio::NoError;
        stateCode = QAudio::StoppedState;

        m_volume = qreal(1.0f);

        intervalTimer = new QTimer(this);
        intervalTimer->setInterval(1000);
        connect(intervalTimer, SIGNAL(timeout()), SIGNAL(notify()));
    }
}

QAudioInputPrivate::~QAudioInputPrivate()
{
    close();
    delete audioDeviceInfo;
}

bool QAudioInputPrivate::open()
{
    UInt32  size = 0;

    if (isOpen)
        return true;

    ComponentDescription    cd;
    cd.componentType = kAudioUnitType_Output;
    cd.componentSubType = kAudioUnitSubType_HALOutput;
    cd.componentManufacturer = kAudioUnitManufacturer_Apple;
    cd.componentFlags = 0;
    cd.componentFlagsMask = 0;

    // Open
    Component cp = FindNextComponent(NULL, &cd);
    if (cp == 0) {
        qWarning() << "QAudioInput: Failed to find HAL Output component";
        return false;
    }

    if (OpenAComponent(cp, &audioUnit) != noErr) {
        qWarning() << "QAudioInput: Unable to Open Output Component";
        return false;
    }

    // Set mode
    // switch to input mode
    UInt32 enable = 1;
    if (AudioUnitSetProperty(audioUnit,
                               kAudioOutputUnitProperty_EnableIO,
                               kAudioUnitScope_Input,
                               1,
                               &enable,
                               sizeof(enable)) != noErr) {
        qWarning() << "QAudioInput: Unable to switch to input mode (Enable Input)";
        return false;
    }

    enable = 0;
    if (AudioUnitSetProperty(audioUnit,
                            kAudioOutputUnitProperty_EnableIO,
                            kAudioUnitScope_Output,
                            0,
                            &enable,
                            sizeof(enable)) != noErr) {
        qWarning() << "QAudioInput: Unable to switch to input mode (Disable output)";
        return false;
    }

    // register callback
    AURenderCallbackStruct cb;
    cb.inputProc = inputCallback;
    cb.inputProcRefCon = this;

    if (AudioUnitSetProperty(audioUnit,
                               kAudioOutputUnitProperty_SetInputCallback,
                               kAudioUnitScope_Global,
                               0,
                               &cb,
                               sizeof(cb)) != noErr) {
        qWarning() << "QAudioInput: Failed to set AudioUnit callback";
        return false;
    }

    // Set Audio Device
    if (AudioUnitSetProperty(audioUnit,
                                kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global,
                                0,
                                &audioDeviceId,
                                sizeof(audioDeviceId)) != noErr) {
        qWarning() << "QAudioInput: Unable to use configured device";
        return false;
    }

    // Set format
    // Wanted
    streamFormat = toAudioStreamBasicDescription(audioFormat);

    // Required on unit
    if (audioFormat == audioDeviceInfo->preferredFormat()) {
        deviceFormat = streamFormat;
        AudioUnitSetProperty(audioUnit,
                               kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Output,
                               1,
                               &deviceFormat,
                               sizeof(deviceFormat));
    }
    else {
        size = sizeof(deviceFormat);
        if (AudioUnitGetProperty(audioUnit,
                                    kAudioUnitProperty_StreamFormat,
                                    kAudioUnitScope_Input,
                                    1,
                                    &deviceFormat,
                                    &size) != noErr) {
            qWarning() << "QAudioInput: Unable to retrieve device format";
            return false;
        }

        if (AudioUnitSetProperty(audioUnit,
                                   kAudioUnitProperty_StreamFormat,
                                   kAudioUnitScope_Output,
                                   1,
                                   &deviceFormat,
                                   sizeof(deviceFormat)) != noErr) {
            qWarning() << "QAudioInput: Unable to set device format";
            return false;
        }
    }

    // Setup buffers
    UInt32 numberOfFrames;
    size = sizeof(UInt32);
    if (AudioUnitGetProperty(audioUnit,
                                kAudioDevicePropertyBufferFrameSize,
                                kAudioUnitScope_Global,
                                0,
                                &numberOfFrames,
                                &size) != noErr) {
        qWarning() << "QAudioInput: Failed to get audio period size";
        return false;
    }

    AudioValueRange bufferRange;
    size = sizeof(AudioValueRange);

    if (AudioUnitGetProperty(audioUnit,
                             kAudioDevicePropertyBufferFrameSizeRange,
                             kAudioUnitScope_Global,
                             0,
                             &bufferRange,
                             &size) != noErr) {
        qWarning() << "QAudioInput: Failed to get audio period size range";
        return false;
    }

    // See if the requested buffer size is permissible
    UInt32 frames = qBound((UInt32)bufferRange.mMinimum, internalBufferSize / streamFormat.mBytesPerFrame, (UInt32)bufferRange.mMaximum);

    // Set it back
    if (AudioUnitSetProperty(audioUnit,
                             kAudioDevicePropertyBufferFrameSize,
                             kAudioUnitScope_Global,
                             0,
                             &frames,
                             sizeof(UInt32)) != noErr) {
        qWarning() << "QAudioInput: Failed to set audio buffer size";
        return false;
    }

    // Now allocate a few buffers to be safe.
    periodSizeBytes = internalBufferSize = frames * streamFormat.mBytesPerFrame;

    audioBuffer = new QtMultimediaInternal::QAudioInputBuffer(internalBufferSize * 4,
                                        periodSizeBytes,
                                        deviceFormat,
                                        streamFormat,
                                        this);

    audioBuffer->setVolume(m_volume);
    audioIO = new QtMultimediaInternal::MacInputDevice(audioBuffer, this);

    // Init
    if (AudioUnitInitialize(audioUnit) != noErr) {
        qWarning() << "QAudioInput: Failed to initialize AudioUnit";
        return false;
    }

    isOpen = true;

    return isOpen;
}

void QAudioInputPrivate::close()
{
    if (audioUnit != 0) {
        AudioOutputUnitStop(audioUnit);
        AudioUnitUninitialize(audioUnit);
        CloseComponent(audioUnit);
    }

    delete audioBuffer;
}

QAudioFormat QAudioInputPrivate::format() const
{
    return audioFormat;
}

void QAudioInputPrivate::setFormat(const QAudioFormat& fmt)
{
    if (stateCode == QAudio::StoppedState)
        audioFormat = fmt;
}

void QAudioInputPrivate::start(QIODevice* device)
{
    QIODevice*  op = device;

    if (!audioDeviceInfo->isFormatSupported(audioFormat) || !open()) {
        stateCode = QAudio::StoppedState;
        errorCode = QAudio::OpenError;
        return;
    }

    reset();
    audioBuffer->reset();
    audioBuffer->setFlushDevice(op);

    if (op == 0)
        op = audioIO;

    // Start
    startTime = AudioGetCurrentHostTime();
    totalFrames = 0;

    stateCode = QAudio::IdleState;
    errorCode = QAudio::NoError;
    emit stateChanged(stateCode);

    audioThreadStart();
}

QIODevice* QAudioInputPrivate::start()
{
    QIODevice*  op = 0;

    if (!audioDeviceInfo->isFormatSupported(audioFormat) || !open()) {
        stateCode = QAudio::StoppedState;
        errorCode = QAudio::OpenError;
        return audioIO;
    }

    reset();
    audioBuffer->reset();
    audioBuffer->setFlushDevice(op);

    if (op == 0)
        op = audioIO;

    // Start
    startTime = AudioGetCurrentHostTime();
    totalFrames = 0;

    stateCode = QAudio::IdleState;
    errorCode = QAudio::NoError;
    emit stateChanged(stateCode);

    audioThreadStart();

    return op;
}

void QAudioInputPrivate::stop()
{
    QMutexLocker    lock(&mutex);
    if (stateCode != QAudio::StoppedState) {
        audioThreadStop();
        audioBuffer->flush(true);

        errorCode = QAudio::NoError;
        stateCode = QAudio::StoppedState;
        QMetaObject::invokeMethod(this, "stateChanged", Qt::QueuedConnection, Q_ARG(QAudio::State, stateCode));
    }
}

void QAudioInputPrivate::reset()
{
    QMutexLocker    lock(&mutex);
    if (stateCode != QAudio::StoppedState) {
        audioThreadStop();

        errorCode = QAudio::NoError;
        stateCode = QAudio::StoppedState;
        audioBuffer->reset();
        QMetaObject::invokeMethod(this, "stateChanged", Qt::QueuedConnection, Q_ARG(QAudio::State, stateCode));
    }
}

void QAudioInputPrivate::suspend()
{
    QMutexLocker    lock(&mutex);
    if (stateCode == QAudio::ActiveState || stateCode == QAudio::IdleState) {
        audioThreadStop();

        errorCode = QAudio::NoError;
        stateCode = QAudio::SuspendedState;
        QMetaObject::invokeMethod(this, "stateChanged", Qt::QueuedConnection, Q_ARG(QAudio::State, stateCode));
    }
}

void QAudioInputPrivate::resume()
{
    QMutexLocker    lock(&mutex);
    if (stateCode == QAudio::SuspendedState) {
        audioThreadStart();

        errorCode = QAudio::NoError;
        stateCode = QAudio::ActiveState;
        QMetaObject::invokeMethod(this, "stateChanged", Qt::QueuedConnection, Q_ARG(QAudio::State, stateCode));
    }
}

int QAudioInputPrivate::bytesReady() const
{
    if (!audioBuffer)
        return 0;
    return audioBuffer->used();
}

int QAudioInputPrivate::periodSize() const
{
    return periodSizeBytes;
}

void QAudioInputPrivate::setBufferSize(int bs)
{
    internalBufferSize = bs;
}

int QAudioInputPrivate::bufferSize() const
{
    return internalBufferSize;
}

void QAudioInputPrivate::setNotifyInterval(int milliSeconds)
{
    if (intervalTimer->interval() == milliSeconds)
        return;

    if (milliSeconds <= 0)
        milliSeconds = 0;

    intervalTimer->setInterval(milliSeconds);
}

int QAudioInputPrivate::notifyInterval() const
{
    return intervalTimer->interval();
}

qint64 QAudioInputPrivate::processedUSecs() const
{
    return totalFrames * 1000000 / audioFormat.sampleRate();
}

qint64 QAudioInputPrivate::elapsedUSecs() const
{
    if (stateCode == QAudio::StoppedState)
        return 0;

    return (AudioGetCurrentHostTime() - startTime) / (clockFrequency / 1000);
}

QAudio::Error QAudioInputPrivate::error() const
{
    return errorCode;
}

QAudio::State QAudioInputPrivate::state() const
{
    return stateCode;
}

qreal QAudioInputPrivate::volume() const
{
    return m_volume;
}

void QAudioInputPrivate::setVolume(qreal volume)
{
    m_volume = volume;
    if (audioBuffer)
        audioBuffer->setVolume(m_volume);
}


void QAudioInputPrivate::audioThreadStop()
{
    stopTimers();
    if (audioThreadState.testAndSetAcquire(Running, Stopped))
        threadFinished.wait(&mutex);
}

void QAudioInputPrivate::audioThreadStart()
{
    startTimers();
    audioThreadState.store(Running);
    AudioOutputUnitStart(audioUnit);
}

void QAudioInputPrivate::audioDeviceStop()
{
    AudioOutputUnitStop(audioUnit);
    audioThreadState.store(Stopped);
    threadFinished.wakeOne();
}

void QAudioInputPrivate::audioDeviceActive()
{
    QMutexLocker    lock(&mutex);
    if (stateCode == QAudio::IdleState) {
        stateCode = QAudio::ActiveState;
        QMetaObject::invokeMethod(this, "stateChanged",  Qt::QueuedConnection, Q_ARG(QAudio::State, stateCode));
    }
}

void QAudioInputPrivate::audioDeviceFull()
{
    QMutexLocker    lock(&mutex);
    if (stateCode == QAudio::ActiveState) {
        errorCode = QAudio::UnderrunError;
        stateCode = QAudio::IdleState;
        QMetaObject::invokeMethod(this, "stateChanged",  Qt::QueuedConnection, Q_ARG(QAudio::State, stateCode));
    }
}

void QAudioInputPrivate::audioDeviceError()
{
    QMutexLocker    lock(&mutex);
    if (stateCode == QAudio::ActiveState) {
        audioDeviceStop();

        errorCode = QAudio::IOError;
        stateCode = QAudio::StoppedState;
        QMetaObject::invokeMethod(this, "deviceStopped", Qt::QueuedConnection);
    }
}

void QAudioInputPrivate::startTimers()
{
    audioBuffer->startFlushTimer();
    if (intervalTimer->interval() > 0)
        intervalTimer->start();
}

void QAudioInputPrivate::stopTimers()
{
    audioBuffer->stopFlushTimer();
    intervalTimer->stop();
}

void QAudioInputPrivate::deviceStopped()
{
    stopTimers();
    emit stateChanged(stateCode);
}

// Input callback
OSStatus QAudioInputPrivate::inputCallback(void* inRefCon,
                                AudioUnitRenderActionFlags* ioActionFlags,
                                const AudioTimeStamp* inTimeStamp,
                                UInt32 inBusNumber,
                                UInt32 inNumberFrames,
                                AudioBufferList* ioData)
{
    Q_UNUSED(ioData);

    QAudioInputPrivate* d = static_cast<QAudioInputPrivate*>(inRefCon);

    const int threadState = d->audioThreadState.loadAcquire();
    if (threadState == Stopped)
        d->audioDeviceStop();
    else {
        qint64      framesWritten;

        framesWritten = d->audioBuffer->renderFromDevice(d->audioUnit,
                                                         ioActionFlags,
                                                         inTimeStamp,
                                                         inBusNumber,
                                                         inNumberFrames);

        if (framesWritten > 0) {
            d->totalFrames += framesWritten;
            d->audioDeviceActive();
        } else if (framesWritten == 0)
            d->audioDeviceFull();
        else if (framesWritten < 0)
            d->audioDeviceError();
    }

    return noErr;
}


QT_END_NAMESPACE

#include "qaudioinput_mac_p.moc"
