#pragma once

#include <QObject>
#include <QClipboard>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QSslCertificate>

class ClipboardManager : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardManager(const QString& hostAddress, int httpsPort,
                              const QSslCertificate& serverCert,
                              const QString& uniqueId, QObject* parent = nullptr);
    ~ClipboardManager();

    void start();
    void stop();

private slots:
    void onLocalClipboardChanged();
    void onPollTimerExpired();
    void onClipboardFetchComplete(QNetworkReply* reply);
    void onClipboardSetComplete(QNetworkReply* reply);

private:
    QNetworkRequest createRequest(const QString& path);
    void checkLocalClipboard();
    void fetchRemoteClipboard();
    void setRemoteClipboard(const QString& content);

    QClipboard* m_Clipboard;
    QNetworkAccessManager* m_NetworkManager;
    QTimer* m_PollTimer;
    QString m_HostAddress;
    int m_HttpsPort;
    QSslCertificate m_ServerCert;
    QString m_UniqueId;
    QString m_LastKnownRemoteContent;
    QString m_LastSentLocalContent;
    bool m_UpdatingClipboard;
    bool m_Started;
};
