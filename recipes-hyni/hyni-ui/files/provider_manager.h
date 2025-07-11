#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>

struct ProviderInfo {
    QString name;
    QString displayName;
    QString version;
    QString schemaPath;
    QString endpoint;
    QStringList availableModels;
    QString defaultModel;
    bool supportsStreaming = false;
    bool supportsVision = false;
    bool supportsSystemMessages = false;

    // Authentication info
    QString authType = "header";
    QString keyName = "Authorization";
    QString keyPrefix;

    nlohmann::json rawSchema;
};

class ProviderManager : public QObject
{
    Q_OBJECT

public:
    explicit ProviderManager(QObject *parent = nullptr);

    void addProvider(const QString &name, std::shared_ptr<ProviderInfo> info);
    std::shared_ptr<ProviderInfo> getProvider(const QString &name) const;
    bool hasProvider(const QString &name) const;
    QStringList getProviderNames() const;
    void clear();
    size_t size() const { return m_providers.size(); }

private:
    std::unordered_map<std::string, std::shared_ptr<ProviderInfo>> m_providers;
};
