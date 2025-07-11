#include "schema_loader.h"
#include "provider_manager.h"
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <nlohmann/json.hpp>
#include <fstream>
#include <memory>

Q_LOGGING_CATEGORY(hyniSchemaLoader, "hyni.gui.schema_loader")

SchemaLoader::SchemaLoader(const QString &schemaDir, QObject *parent)
    : QThread(parent)
    , m_schemaDir(schemaDir)
{
    qCInfo(hyniSchemaLoader) << "Initializing schema loader for directory:" << schemaDir;
}

void SchemaLoader::run()
{
    int loadedCount = 0;

    try {
        QDir dir(m_schemaDir);

        if (!dir.exists() || !dir.isReadable()) {
            QString error = QString("Invalid schema directory: %1").arg(m_schemaDir);
            qCCritical(hyniSchemaLoader) << error;
            emit errorOccurred(error);
            return;
        }

        // Get all JSON files
        QStringList filters;
        filters << "*.json";
        dir.setNameFilters(filters);

        QFileInfoList jsonFiles = dir.entryInfoList(QDir::Files | QDir::Readable);
        qCInfo(hyniSchemaLoader) << "Found" << jsonFiles.size() << "JSON files in" << m_schemaDir;

        for (const QFileInfo &fileInfo : jsonFiles) {
            try {
                qCDebug(hyniSchemaLoader) << "Loading schema file:" << fileInfo.filePath();

                // Use nlohmann::json to parse the file
                std::ifstream file(fileInfo.filePath().toStdString());
                if (!file.is_open()) {
                    QString error = QString("Cannot open file: %1").arg(fileInfo.fileName());
                    qCWarning(hyniSchemaLoader) << error;
                    emit errorOccurred(error);
                    continue;
                }

                nlohmann::json schema;
                try {
                    file >> schema;
                } catch (const nlohmann::json::parse_error &e) {
                    QString error = QString("JSON parse error in %1: %2")
                    .arg(fileInfo.fileName())
                        .arg(e.what());
                    qCWarning(hyniSchemaLoader) << error;
                    emit errorOccurred(error);
                    continue;
                }

                // Validate it's a valid schema
                if (!schema.contains("provider") || !schema.contains("api")) {
                    qCWarning(hyniSchemaLoader) << "Invalid schema file (missing provider or api):"
                                                << fileInfo.fileName();
                    continue;
                }

                // Extract provider info
                auto providerInfo = std::make_shared<ProviderInfo>();
                providerInfo->schemaPath = fileInfo.filePath();

                // Provider section
                auto provider = schema["provider"];
                providerInfo->name = QString::fromStdString(
                    provider.value("name", fileInfo.baseName().toStdString()));
                providerInfo->displayName = QString::fromStdString(
                    provider.value("display_name", providerInfo->name.toStdString()));
                providerInfo->version = QString::fromStdString(
                    provider.value("version", "1.0"));

                qCInfo(hyniSchemaLoader) << "Loaded provider:" << providerInfo->displayName
                                         << "(name:" << providerInfo->name
                                         << ", version:" << providerInfo->version << ")";

                // API section
                auto api = schema["api"];
                providerInfo->endpoint = QString::fromStdString(
                    api.value("endpoint", ""));
                qCDebug(hyniSchemaLoader) << "Provider" << providerInfo->name
                                          << "endpoint:" << providerInfo->endpoint;

                // Models section
                if (schema.contains("models")) {
                    auto models = schema["models"];

                    if (models.contains("available") && models["available"].is_array()) {
                        for (const auto &model : models["available"]) {
                            if (model.is_string()) {
                                providerInfo->availableModels.append(
                                    QString::fromStdString(model.get<std::string>()));
                            }
                        }
                    }

                    if (models.contains("default") && models["default"].is_string()) {
                        providerInfo->defaultModel = QString::fromStdString(
                            models["default"].get<std::string>());
                    } else if (!providerInfo->availableModels.isEmpty()) {
                        providerInfo->defaultModel = providerInfo->availableModels.first();
                    }

                    qCDebug(hyniSchemaLoader) << "Provider" << providerInfo->name
                                              << "models:" << providerInfo->availableModels
                                              << ", default:" << providerInfo->defaultModel;
                }

                // Features section
                if (schema.contains("features")) {
                    auto features = schema["features"];
                    providerInfo->supportsStreaming = features.value("streaming", false);
                    providerInfo->supportsVision = features.value("vision", false);
                    providerInfo->supportsSystemMessages = features.value("system_messages", false);

                    qCDebug(hyniSchemaLoader) << "Provider" << providerInfo->name
                                              << "features - streaming:" << providerInfo->supportsStreaming
                                              << ", vision:" << providerInfo->supportsVision
                                              << ", system_messages:" << providerInfo->supportsSystemMessages;
                }

                // Authentication section (optional)
                if (schema.contains("authentication")) {
                    auto auth = schema["authentication"];
                    providerInfo->authType = QString::fromStdString(
                        auth.value("type", "header"));
                    providerInfo->keyName = QString::fromStdString(
                        auth.value("key_name", "Authorization"));
                    providerInfo->keyPrefix = QString::fromStdString(
                        auth.value("key_prefix", ""));

                    qCDebug(hyniSchemaLoader) << "Provider" << providerInfo->name
                                              << "auth - type:" << providerInfo->authType
                                              << ", key_name:" << providerInfo->keyName
                                              << ", key_prefix:" << providerInfo->keyPrefix;
                }

                // Emit signal with the provider info
                emit providerLoaded(providerInfo->displayName, providerInfo);
                loadedCount++;

            } catch (const nlohmann::json::exception &e) {
                QString error = QString("JSON error in %1: %2")
                .arg(fileInfo.fileName())
                    .arg(e.what());
                qCCritical(hyniSchemaLoader) << error;
                emit errorOccurred(error);
            } catch (const std::exception &e) {
                QString error = QString("Error loading %1: %2")
                .arg(fileInfo.fileName())
                    .arg(e.what());
                qCCritical(hyniSchemaLoader) << error;
                emit errorOccurred(error);
            }
        }

        qCInfo(hyniSchemaLoader) << "Finished loading schemas. Successfully loaded"
                                 << loadedCount << "providers";

    } catch (const std::exception &e) {
        QString error = QString("Error scanning directory: %1").arg(e.what());
        qCCritical(hyniSchemaLoader) << error;
        emit errorOccurred(error);
    }
}
