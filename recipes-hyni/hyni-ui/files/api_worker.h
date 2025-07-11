#pragma once

#include <QThread>
#include <QString>
#include <atomic>

namespace hyni {
class chat_api;
}

class ApiWorker : public QThread
{
    Q_OBJECT

public:
    explicit ApiWorker(hyni::chat_api *api, const QString &message,
                       bool keepHistory, QObject *parent = nullptr);

    void cancel();

signals:
    void chunkReceived(const QString &chunk);
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);

protected:
    hyni::chat_api *m_api;
    QString m_message;
    bool m_keepHistory;
    std::atomic<bool> m_cancelled{false};
};

class StreamingApiWorker : public ApiWorker
{
    Q_OBJECT

public:
    using ApiWorker::ApiWorker;

protected:
    void run() override;
};

class NonStreamingApiWorker : public ApiWorker
{
    Q_OBJECT

public:
    using ApiWorker::ApiWorker;

protected:
    void run() override;
};
