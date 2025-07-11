#pragma once

#include <QMainWindow>
#include <QSettings>
#include <memory>
#include <unordered_map>
#include "chat_api.h"
#include "schema_registry.h"
#include "context_factory.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QComboBox;
class QMenu;
class QAction;
QT_END_NAMESPACE

class ChatWidget;
class ProviderManager;
class SchemaLoader;
class ApiWorker;
struct ProviderInfo;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void sendMessage();
    void clearConversation();
    void selectSchemaDirectory();
    void reloadSchemas();
    void reloadApiKeys();
    void setApiKey();
    void setSystemMessage();
    void viewApiKeyStatus();
    void showDebugInfo();
    void showAbout();
    void changeProvider(const QString &providerName);
    void onModelChanged(const QString &modelName);
    void onProviderLoaded(const QString &providerName, std::shared_ptr<ProviderInfo> info);
    void onSchemaError(const QString &error);
    void onSchemasLoaded();
    void onStreamingChunk(const QString &chunk);
    void onResponseReceived(const QString &response);
    void onApiError(const QString &error);
    void onWorkerFinished();

private:
    void initUI();
    void createMenuBar();
    void loadSchemasFromDirectory(const QString &directory);
    void setupProvider(const QString &providerName);
    void updateProviderMenu();
    void updateApiKeyStatus();
    void cancelCurrentOperation();
    bool setApiKeyForProvider(const QString &providerName);
    QString getApiKeyForProvider(const QString &providerName) const;
    QString getEnvVarName(const QString &providerName) const;
    void showNoSchemasMessage();

private:
    QSettings m_settings;

    // UI components
    ChatWidget *m_chatWidget;
    QLabel *m_statusLabel;
    QComboBox *m_modelCombo;
    QMenu *m_providerMenu;

    // Provider management
    std::unique_ptr<ProviderManager> m_providerManager;
    QString m_currentProvider;
    QString m_currentModel;
    QString m_schemaDir;

    // API management
    std::shared_ptr<hyni::schema_registry> m_schemaRegistry;
    std::shared_ptr<hyni::context_factory> m_contextFactory;
    std::unique_ptr<hyni::chat_api> m_chatApi;
    std::unordered_map<std::string, std::string> m_apiKeys;
    std::unordered_map<std::string, std::string> m_apiKeySources;

    // System message
    QString m_systemMessage;

    // Workers
    SchemaLoader *m_schemaLoader = nullptr;
    ApiWorker *m_streamingWorker = nullptr;
    ApiWorker *m_nonStreamingWorker = nullptr;
};
