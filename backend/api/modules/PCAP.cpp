// SPDX-License-Identifier: Apache-2.0

// Copyright 2026 chargebyte GmbH

#include "PCAP.hpp"

#include "ConsoleConnector.hpp"
#include "ProtocolSchema.hpp"
#include "ResponseBuilder.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QTemporaryFile>

#include <stdexcept>

namespace {
constexpr char kTcpdumpTemplate[] = "tcpdump -i <interface> -w <file_name>";
constexpr char kStopTemplate[] = "true";
constexpr char kPlaceholderInterface[] = "<interface>";
constexpr char kPlaceholderFileName[] = "<file_name>";
constexpr char kPcapTmpFileName[] = "pcap_XXXXXX.pcap";

QHash<QString, QString> buildPcapValues(const QString &iface, const QString &filePath) {
    QHash<QString, QString> values;
    values.insert(QLatin1String(kPlaceholderInterface), iface);
    values.insert(QLatin1String(kPlaceholderFileName), filePath);
    return values;
}

QString createTempPcapFile() {
    QTemporaryFile tempFile(QDir::tempPath() + QLatin1String("/") + QLatin1String(kPcapTmpFileName));
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        return QString();
    }

    const QString filePath = tempFile.fileName();
    tempFile.close();
    return filePath;
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

ModuleResponse PCAP::handleRequest(const ModuleRequest &request) {
    switch (toPcapAction(request.action)) {
    case PCAPAction::Read:
        return handleReadRequest(request);
    case PCAPAction::Write:
        return handleWriteRequest(request);
    case PCAPAction::Unknown:
        throw std::runtime_error("PCAP::handleRequest got unsupported action");
    }

    throw std::runtime_error("PCAP::handleRequest reached unreachable code");
}

ModuleResponse PCAP::handleReadRequest(const ModuleRequest &request) {
    return readCapture(request);
}

ModuleResponse PCAP::handleWriteRequest(const ModuleRequest &request) {
    return startCapture(request);
}

void PCAP::enqueueRequest(const ModuleRequest &request) {
    m_queue.enqueue(request);
    processQueue();
}

void PCAP::processQueue() {
    while (!m_busy && !m_queue.isEmpty()) {
        const ModuleRequest next = m_queue.dequeue();
        const ModuleResponse response = handleRequest(next);
        emit responseReady(ResponseBuilder::buildResponse(response));
    }
}

ModuleResponse PCAP::startCapture(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupPcap),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    if (m_busy || m_recording) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorPcapBusy)},
        };
        return response;
    }

    const QString iface = extractInterface(request);
    if (iface.isEmpty() || !isInterfaceAvailable(iface)) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorInvalidParams)},
        };
        return response;
    }

    m_busy = true;
    const QString tempPath = createTempPcapFile();
    if (tempPath.isEmpty()) {
        m_busy = false;
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorFileIoFailed)},
        };
        return response;
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
        QFile::remove(m_lastFile);
        m_lastFile.clear();
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorInvalidParams)},
        };
        return response;
    }

    m_recording = true;
    m_busy = false;
    response.success = true;
    return response;
}

ModuleResponse PCAP::readCapture(const ModuleRequest &request) {
    ModuleResponse response{
        .requestId = request.requestId,
        .group = QLatin1String(kGroupPcap),
        .action = request.action,
        .parameters = QJsonObject{},
        .success = false,
        .final = true,
    };

    if (m_busy) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorPcapBusy)},
        };
        return response;
    }
    if (!m_recording || m_lastFile.isEmpty()) {
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorNotRecording)},
        };
        return response;
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
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorPcapStopFailed)},
        };
        return response;
    }

    QFile file(m_lastFile);
    if (!file.open(QIODevice::ReadOnly)) {
        m_busy = false;
        response.parameters = QJsonObject{
            {QLatin1String(kError), QLatin1String(kErrorFileIoFailed)},
        };
        return response;
    }

    const QByteArray encoded = file.readAll().toBase64(QByteArray::Base64Encoding);
    const QString pcapFileName = fileNameFromPath(m_lastFile);

    file.close();
    QFile::remove(m_lastFile);

    response.parameters = QJsonObject{
        {QLatin1String(kKeyFile), pcapFileName},
        {QLatin1String(kKeyDataB64), QString::fromLatin1(encoded)},
    };
    response.success = true;

    m_lastFile.clear();
    m_recording = false;
    m_busy = false;
    return response;
}

PCAPAction PCAP::toPcapAction(const QString &action) {
    if (action == QLatin1String(kActionRead)) {
        return PCAPAction::Read;
    }

    if (action == QLatin1String(kActionWrite)) {
        return PCAPAction::Write;
    }

    return PCAPAction::Unknown;
}

QString PCAP::extractInterface(const ModuleRequest &request) {
    const QJsonObject parameters = request.parameters;
    const QJsonObject general = parameters.value(QLatin1String(kKeyGeneral)).toObject();
    return general.value(QLatin1String(kKeyInterface)).toString().trimmed();
}

QString PCAP::fileNameFromPath(const QString &filePath) {
    if (filePath.isEmpty()) {
        return QString();
    }

    return QFileInfo(filePath).fileName();
}
