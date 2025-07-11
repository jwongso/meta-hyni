#include "provider_manager.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(hyniProvider, "hyni.gui.provider")

ProviderManager::ProviderManager(QObject *parent)
    : QObject(parent)
{
    qCDebug(hyniProvider) << "ProviderManager initialized";
}

void ProviderManager::addProvider(const QString &name, std::shared_ptr<ProviderInfo> info)
{
    if (name.isEmpty() || !info) {
        qCWarning(hyniProvider) << "Cannot add provider with empty name or null info";
        return;
    }

    m_providers[name.toStdString()] = info;
    qCInfo(hyniProvider) << "Added provider:" << name
                         << "version:" << info->version
                         << "models:" << info->availableModels.size();
}

std::shared_ptr<ProviderInfo> ProviderManager::getProvider(const QString &name) const
{
    auto it = m_providers.find(name.toStdString());
    if (it != m_providers.end()) {
        return it->second;
    }

    qCWarning(hyniProvider) << "Provider not found:" << name;
    return nullptr;
}

bool ProviderManager::hasProvider(const QString &name) const
{
    return m_providers.find(name.toStdString()) != m_providers.end();
}

QStringList ProviderManager::getProviderNames() const
{
    QStringList names;
    names.reserve(static_cast<int>(m_providers.size()));

    for (const auto &[name, info] : m_providers) {
        names.append(QString::fromStdString(name));
    }

    names.sort();
    return names;
}

void ProviderManager::clear()
{
    qCInfo(hyniProvider) << "Clearing" << m_providers.size() << "providers";
    m_providers.clear();
}
