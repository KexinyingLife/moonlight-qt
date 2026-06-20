#include "clipboardmanager.h"
#include "backend/identitymanager.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMimeData>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QUuid>
#include <QUrlQuery>

#include <SDL.h>

ClipboardManager::ClipboardManager(const QString& hostAddress, int httpsPort,
                                   const QSslCertificate& serverCert,
                                   const QString& uniqueId, QObject* parent)
    : QObject(parent),
      m_Clipboard(nullptr),
      m_NetworkManager(nullptr),
      m_PollTimer(nullptr),
      m_HostAddress(hostAddress),
      m_HttpsPort(httpsPort),
      m_ServerCert(serverCert),
      m_UniqueId(uniqueId),
      m_UpdatingClipboard(false),
      m_Started(false)
{
}

ClipboardManager::~ClipboardManager()
{
    stop();
}

void ClipboardManager::start()
{
    if (m_Started) {
        return;
    }
    m_Started = true;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Clipboard sync started for %s:%d",
                m_HostAddress.toLocal8Bit().constData(),
                m_HttpsPort);

    m_Clipboard = QGuiApplication::clipboard();
    m_NetworkManager = new QNetworkAccessManager(this);

    // Connect clipboard change signal
    connect(m_Clipboard, &QClipboard::dataChanged,
            this, &ClipboardManager::onLocalClipboardChanged);

    // Set up polling timer for remote clipboard changes
    m_PollTimer = new QTimer(this);
    connect(m_PollTimer, &QTimer::timeout,
            this, &ClipboardManager::onPollTimerExpired);
    m_PollTimer->start(500); // Poll every 500ms
}

void ClipboardManager::stop()
{
    if (!m_Started) {
        return;
    }
    m_Started = false;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Clipboard sync stopped");

    if (m_PollTimer) {
        m_PollTimer->stop();
        delete m_PollTimer;
        m_PollTimer = nullptr;
    }

    if (m_NetworkManager) {
        delete m_NetworkManager;
        m_NetworkManager = nullptr;
    }
}

void ClipboardManager::onLocalClipboardChanged()
{
    if (m_UpdatingClipboard || !m_Started) {
        return;
    }

    // Only sync text clipboard
    if (!m_Clipboard->mimeData()->hasText()) {
        return;
    }

    QString text = m_Clipboard->text();
    if (text.isEmpty()) {
        return;
    }

    // Don't send if it matches what we last received from the host
    if (text == m_LastKnownRemoteContent) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Local clipboard changed, syncing to host (%d chars)",
                text.length());

    setRemoteClipboard(text);
}

void ClipboardManager::onPollTimerExpired()
{
    if (!m_Started) {
        return;
    }
    fetchRemoteClipboard();
}

QNetworkRequest ClipboardManager::createRequest(const QString& path)
{
    QUrl url(QString("https://%1:%2%3")
             .arg(m_HostAddress)
             .arg(m_HttpsPort)
             .arg(path));

    // Add required query parameters
    QUrlQuery query;
    query.addQueryItem("type", "text");
    url.setQuery(query);

    QNetworkRequest request(url);

    // Configure SSL with client certificate for mTLS authentication
    QSslConfiguration sslConfig = IdentityManager::get()->getSslConfig();

    // Accept self-signed server certificates (Apollo default)
    sslConfig.setPeerVerifyMode(QSslSocket::QueryPeer);
    sslConfig.addCaCertificate(m_ServerCert);

    request.setSslConfiguration(sslConfig);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Disable HTTP/2
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
#endif

    request.setTransferTimeout(3000);

    return request;
}

void ClipboardManager::fetchRemoteClipboard()
{
    QNetworkRequest request = createRequest("/actions/clipboard");

    QNetworkReply* reply = m_NetworkManager->get(request);

    // Handle SSL errors by ignoring them (self-signed certs)
    connect(reply, &QNetworkReply::sslErrors, this, [reply](const QList<QSslError>& errors) {
        Q_UNUSED(errors);
        reply->ignoreSslErrors();
    });

    connect(reply, &QNetworkReply::finished,
            this, [this, reply]() { onClipboardFetchComplete(reply); });
}

void ClipboardManager::setRemoteClipboard(const QString& content)
{
    QNetworkRequest request = createRequest("/actions/clipboard");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");

    QNetworkReply* reply = m_NetworkManager->post(request, content.toUtf8());

    // Handle SSL errors by ignoring them (self-signed certs)
    connect(reply, &QNetworkReply::sslErrors, this, [reply](const QList<QSslError>& errors) {
        Q_UNUSED(errors);
        reply->ignoreSslErrors();
    });

    connect(reply, &QNetworkReply::finished,
            this, [this, reply]() { onClipboardSetComplete(reply); });
}

void ClipboardManager::onClipboardFetchComplete(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // Silently ignore fetch errors (server may not support clipboard API
        // or client may not have clipboard permission)
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,
                       "Clipboard fetch error: %s",
                       reply->errorString().toLocal8Bit().constData());
        return;
    }

    QByteArray data = reply->readAll();
    QString remoteText = QString::fromUtf8(data);

    if (remoteText.isEmpty()) {
        return;
    }

    // Don't update if it matches what we already have locally
    if (m_Clipboard && m_Clipboard->text() == remoteText) {
        return;
    }

    // Don't update if it matches what we last set remotely
    if (remoteText == m_LastKnownRemoteContent) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Remote clipboard changed, syncing to local (%d chars)",
                remoteText.length());

    m_LastKnownRemoteContent = remoteText;

    // Set local clipboard without triggering our change handler
    m_UpdatingClipboard = true;
    if (m_Clipboard) {
        m_Clipboard->setText(remoteText);
    }
    m_UpdatingClipboard = false;
}

void ClipboardManager::onClipboardSetComplete(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to set remote clipboard: %s",
                    reply->errorString().toLocal8Bit().constData());
    }
}
