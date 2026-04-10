// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "PCAP.hpp"

#include "ConsoleConnector.hpp"
#include "ResponseBuilder.hpp"
#include "ProtocolSchema.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QNetworkInterface>
#include <QTemporaryFile>

namespace {
constexpr char kTcpdumpTemplate[] = "tcpdump -i <interface> -w <file_name>";
constexpr char kStopTemplate[] = "true";
constexpr char kPlaceholderInterface[] = "<interface>";
constexpr char kPlaceholderFileName[] = "<file_name>";

bool isPcapWriteRequest(const QJsonObject &obj) {
    const QString group = obj.value(FieldNameKey(FieldName::Group)).toString();
    const QString action = obj.value(FieldNameKey(FieldName::Action)).toString();
    return group == QLatin1String(kGroupPcap) && action == QLatin1String(kActionWrite);
}

bool isPcapReadRequest(const QJsonObject &obj) {
    const QString group = obj.value(FieldNameKey(FieldName::Group)).toString();
    const QString action = obj.value(FieldNameKey(FieldName::Action)).toString();
    return group == QLatin1String(kGroupPcap) && action == QLatin1String(kActionRead);
}

QHash<QString, QString> buildPcapValues(const QString &iface, const QString &filePath) {
    QHash<QString, QString> values;
    values.insert(QLatin1String(kPlaceholderInterface), iface);
    values.insert(QLatin1String(kPlaceholderFileName), filePath);
    return values;
}

QString extractInterface(const QJsonObject &request) {
    const QJsonObject parameters =
        request.value(FieldNameKey(FieldName::Parameters)).toObject();
    const QJsonObject general = parameters.value(QLatin1String(kKeyGeneral)).toObject();
    const QString iface = general.value(QLatin1String(kKeyInterface)).toString();
    return iface.trimmed();
}

QString createTempPcapFile() {
    QTemporaryFile tempFile(QDir::tempPath() + QLatin1String("/pcap_XXXXXX.pcap"));
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        return QString();
    }
    const QString filePath = tempFile.fileName();
    tempFile.close();
    return filePath;
}

QByteArray encodeFile(QFile &file) {
    const QByteArray payload = file.readAll();
    return payload.toBase64(QByteArray::Base64Encoding);
}

bool isInterfaceAvailable(const QString &iface) {
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &netIf : interfaces) {
        if (netIf.name() == iface) {
            return netIf.flags().testFlag(QNetworkInterface::IsUp);
        }
    }
    return false;
}
} // namespace

PCAP::PCAP(QObject *parent)
    : QObject(parent), m_console(new ConsoleConnector(this)) {}

void PCAP::enqueueRequest(const QJsonObject &request) {
    m_queue.enqueue(request);
    processQueue();
}

void PCAP::startCapture(const QJsonObject &request) {
    const QString requestId = request.value(FieldNameKey(FieldName::RequestId)).toString();
    if (m_busy || m_recording) {
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorPcapBusy)));
        processQueue();
        return;
    }

    const QString iface = extractInterface(request);
    if (iface.isEmpty()) {
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorInvalidParams)));
        processQueue();
        return;
    }
    if (!isInterfaceAvailable(iface)) {
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorInvalidParams)));
        processQueue();
        return;
    }

    m_busy = true;
    const QString tempPath = createTempPcapFile();
    if (tempPath.isEmpty()) {
        m_busy = false;
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorFileIoFailed)));
        processQueue();
        return;
    }
    m_lastFile = tempPath;

    ConsoleConnector::ExecOptions options;
    options.stop = false;
    const ConsoleConnector::RunResult result = m_console->executeTemplate(
        QLatin1String(kTcpdumpTemplate),
        buildPcapValues(iface, m_lastFile),
        options,
        ConsoleConnector::ExecMode::Async);
    if (result.exitCode != 0) {
        m_busy = false;
        m_lastFile.clear();
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorInvalidParams)));
        processQueue();
        return;
    }

    m_recording = true;
    m_busy = false;
    emit responseReady(ResponseBuilder::pcapWriteAck(requestId));
    processQueue();
}

void PCAP::readCapture(const QJsonObject &request) {
    const QString requestId = request.value(FieldNameKey(FieldName::RequestId)).toString();
    if (m_busy) {
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorPcapBusy)));
        processQueue();
        return;
    }
    if (!m_recording || m_lastFile.isEmpty()) {
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorNotRecording)));
        processQueue();
        return;
    }

    m_busy = true;
    ConsoleConnector::ExecOptions options;
    options.stop = true;
    const ConsoleConnector::RunResult result = m_console->executeTemplate(
        QLatin1String(kStopTemplate),
        QHash<QString, QString>(),
        options,
        ConsoleConnector::ExecMode::Async);
    if (result.exitCode != 0) {
        m_busy = false;
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorPcapStopFailed)));
        processQueue();
        return;
    }

    QFile file(m_lastFile);
    if (!file.open(QIODevice::ReadOnly)) {
        m_busy = false;
        emit responseReady(ResponseBuilder::error(requestId, QLatin1String(kErrorFileIoFailed)));
        processQueue();
        return;
    }

    const QByteArray encoded = encodeFile(file);

    file.close();
    emit responseReady(ResponseBuilder::pcapReadResult(requestId, m_lastFile, encoded));
    m_lastFile.clear();
    m_recording = false;
    m_busy = false;
    processQueue();
}

void PCAP::processQueue() {
    if (m_busy) {
        return;
    }

    while (!m_queue.isEmpty()) {
        const QJsonObject next = m_queue.head();
        if (isPcapWriteRequest(next)) {
            m_queue.dequeue();
            startCapture(next);
            return;
        }

        if (isPcapReadRequest(next)) {
            m_queue.dequeue();
            readCapture(next);
            return;
        }

        m_queue.dequeue();
    }
}
