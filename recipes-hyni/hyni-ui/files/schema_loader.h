#pragma once

#include <QThread>
#include <QString>
#include <memory>

struct ProviderInfo;

class SchemaLoader : public QThread
{
    Q_OBJECT

public:
    explicit SchemaLoader(const QString &schemaDir, QObject *parent = nullptr);

signals:
    void providerLoaded(const QString &providerName, std::shared_ptr<ProviderInfo> info);
    void errorOccurred(const QString &error);

protected:
    void run() override;

private:
    QString m_schemaDir;
};
