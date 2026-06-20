#pragma once

#include <QObject>
#include <QClipboard>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

class ClipboardManager : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardManager(const QString& hostAddress, int httpPort,
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
    void fetchRemoteClipboard();
    void setRemoteClipboard(const QString& content);

    QClipboard* m_Clipboard;
    QNetworkAccessManager* m_NetworkManager;
    QTimer* m_PollTimer;
    QString m_HostAddress;
    int m_HttpPort;
    QString m_UniqueId;
    QString m_LastKnownRemoteContent;
    bool m_UpdatingClipboard;
    bool m_Started;
};
