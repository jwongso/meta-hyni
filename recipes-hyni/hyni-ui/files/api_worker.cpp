#include "api_worker.h"
#include "chat_api.h"
#include <QLoggingCategory>
#include <exception>

Q_LOGGING_CATEGORY(hyniApiWorker, "hyni.gui.api_worker")

// Base ApiWorker implementation
ApiWorker::ApiWorker(hyni::chat_api *api, const QString &message,
                     bool keepHistory, QObject *parent)
    : QThread(parent)
    , m_api(api)
    , m_message(message)
    , m_keepHistory(keepHistory)
{
    qCInfo(hyniApiWorker) << "Created API worker for message (keep_history="
                          << keepHistory << ")";
}

void ApiWorker::cancel()
{
    qCInfo(hyniApiWorker) << "Cancelling API request";
    m_cancelled.store(true);
}

// StreamingApiWorker implementation
void StreamingApiWorker::run()
{
    try {
        qCInfo(hyniApiWorker) << "Starting streaming request";

        QString accumulatedResponse;

        auto onChunk = [this, &accumulatedResponse](const std::string &chunk) {
            if (!m_cancelled.load()) {
                qCDebug(hyniApiWorker) << "Received chunk:"
                                       << QString::fromStdString(chunk).left(50) << "...";
                accumulatedResponse += QString::fromStdString(chunk);
                emit chunkReceived(QString::fromStdString(chunk));
            }
        };

        auto onComplete = [this, &accumulatedResponse](const hyni::http_response &) {
            if (!m_cancelled.load()) {
                qCInfo(hyniApiWorker) << "Streaming completed";
                // Add assistant message to history if multi-turn is enabled
                if (m_keepHistory && !accumulatedResponse.isEmpty()) {
                    m_api->get_context().add_assistant_message(accumulatedResponse.toStdString());
                    qCDebug(hyniApiWorker) << "Added assistant response to conversation history";
                }
            }
        };

        auto cancelCheck = [this]() {
            return m_cancelled.load();
        };

        m_api->send_message_stream(
            m_message.toStdString(),
            onChunk,
            onComplete,
            cancelCheck
            );

    } catch (const hyni::streaming_not_supported_error &e) {
        QString error = "Streaming is not supported by this provider";
        qCCritical(hyniApiWorker) << error;
        emit errorOccurred(error);
    } catch (const std::exception &e) {
        QString error = QString::fromStdString(e.what());
        qCCritical(hyniApiWorker) << "Streaming error:" << error;
        emit errorOccurred(error);
    }
}

// NonStreamingApiWorker implementation
void NonStreamingApiWorker::run()
{
    try {
        qCInfo(hyniApiWorker) << "Starting non-streaming request";

        auto cancelCheck = [this]() {
            return m_cancelled.load();
        };

        std::string response = m_api->send_message(m_message.toStdString(), cancelCheck);

        if (!m_cancelled.load()) {
            qCInfo(hyniApiWorker) << "Received response:"
                                  << QString::fromStdString(response).left(100) << "...";

            // Add assistant message to history if multi-turn is enabled
            if (m_keepHistory) {
                m_api->get_context().add_assistant_message(response);
                qCDebug(hyniApiWorker) << "Added assistant response to conversation history";
            }

            emit responseReceived(QString::fromStdString(response));
        }

    } catch (const std::exception &e) {
        if (!m_cancelled.load()) {
            QString error = QString::fromStdString(e.what());
            qCCritical(hyniApiWorker) << "Non-streaming error:" << error;
            emit errorOccurred(error);
        }
    }
}
