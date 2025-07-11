#include "main_window.h"
#include "chat_widget.h"
#include "provider_manager.h"
#include "schema_loader.h"
#include "api_worker.h"
#include "dialogs.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QComboBox>
#include <QStatusBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QProgressDialog>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(hyniGui, "hyni.gui")

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings("Hyni", "GUI")
    , m_providerManager(std::make_unique<ProviderManager>())
{
    qCInfo(hyniGui) << "Initializing Hyni GUI";

    m_schemaDir = m_settings.value("schema_dir", "schemas").toString();
    qCInfo(hyniGui) << "Schema directory:" << m_schemaDir;

    // Initialize schema registry
    m_schemaRegistry = hyni::schema_registry::create()
                           .set_schema_directory(m_schemaDir.toStdString())
                           .build();

    m_contextFactory = std::make_shared<hyni::context_factory>(m_schemaRegistry);

    initUI();

    // Load schemas if directory exists
    if (QDir(m_schemaDir).exists()) {
        loadSchemasFromDirectory(m_schemaDir);
    } else {
        showNoSchemasMessage();
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::initUI()
{
    setWindowTitle("Hyni - LLM Chat Interface");
    setGeometry(100, 100, 1000, 700);

    // Create central widget
    m_chatWidget = new ChatWidget(this);
    setCentralWidget(m_chatWidget);

    // Create menu bar
    createMenuBar();

    // Create status bar
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel);

    // Add stretch
    auto *stretch = new QWidget();
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    statusBar()->addWidget(stretch, 1);

    // Model selector
    statusBar()->addPermanentWidget(new QLabel("Model:"));
    m_modelCombo = new QComboBox();
    connect(m_modelCombo, &QComboBox::currentTextChanged,
            this, &MainWindow::onModelChanged);
    statusBar()->addPermanentWidget(m_modelCombo);

    // Connect signals
    connect(m_chatWidget, &ChatWidget::sendRequested,
            this, &MainWindow::sendMessage);

    qCInfo(hyniGui) << "UI initialized";
}

void MainWindow::createMenuBar()
{
    auto *menuBar = this->menuBar();

    // File menu
    auto *fileMenu = menuBar->addMenu("&File");

    auto *selectSchemaDirAction = fileMenu->addAction("Select Schema Directory...");
    connect(selectSchemaDirAction, &QAction::triggered,
            this, &MainWindow::selectSchemaDirectory);

    auto *reloadSchemasAction = fileMenu->addAction("Reload Schemas");
    reloadSchemasAction->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(reloadSchemasAction, &QAction::triggered,
            this, &MainWindow::reloadSchemas);

    fileMenu->addSeparator();

    auto *clearAction = fileMenu->addAction("Clear Conversation");
    clearAction->setShortcut(QKeySequence("Ctrl+L"));
    connect(clearAction, &QAction::triggered,
            this, &MainWindow::clearConversation);

    fileMenu->addSeparator();

    auto *reloadKeysAction = fileMenu->addAction("Reload API Keys");
    reloadKeysAction->setShortcut(QKeySequence("Ctrl+R"));
    connect(reloadKeysAction, &QAction::triggered,
            this, &MainWindow::reloadApiKeys);

    fileMenu->addSeparator();

    auto *exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // Provider menu
    m_providerMenu = menuBar->addMenu("&Provider");
    m_providerMenu->setEnabled(false);

    // Settings menu
    auto *settingsMenu = menuBar->addMenu("&Settings");

    auto *apiKeyAction = settingsMenu->addAction("Set API Key...");
    apiKeyAction->setShortcut(QKeySequence("Ctrl+K"));
    connect(apiKeyAction, &QAction::triggered,
            this, &MainWindow::setApiKey);

    settingsMenu->addSeparator();

    auto *systemMessageAction = settingsMenu->addAction("Set System Message...");
    systemMessageAction->setShortcut(QKeySequence("Ctrl+M"));
    connect(systemMessageAction, &QAction::triggered,
            this, &MainWindow::setSystemMessage);

    settingsMenu->addSeparator();

    auto *viewKeysAction = settingsMenu->addAction("View API Key Status...");
    connect(viewKeysAction, &QAction::triggered,
            this, &MainWindow::viewApiKeyStatus);

    settingsMenu->addSeparator();

    auto *debugAction = settingsMenu->addAction("Show Debug Info...");
    debugAction->setShortcut(QKeySequence("Ctrl+D"));
    connect(debugAction, &QAction::triggered,
            this, &MainWindow::showDebugInfo);

    // Help menu
    auto *helpMenu = menuBar->addMenu("&Help");

    auto *aboutAction = helpMenu->addAction("About Hyni");
    connect(aboutAction, &QAction::triggered,
            this, &MainWindow::showAbout);
}

void MainWindow::sendMessage()
{
    QString message = m_chatWidget->getInputText();
    if (message.isEmpty()) {
        return;
    }

    qCInfo(hyniGui) << "Sending message:" << message.left(50) << "...";

    if (m_currentProvider.isEmpty()) {
        QMessageBox::warning(this, "No Provider Selected",
                             "Please select a provider from the Provider menu.");
        return;
    }

    // Check if API key is set
    if (getApiKeyForProvider(m_currentProvider).isEmpty()) {
        auto envVar = getEnvVarName(m_currentProvider);
        QMessageBox::warning(this, "API Key Required",
                             QString("Please set an API key for %1.\n\n"
                                     "You can:\n"
                                     "1. Set it via Settings → Set API Key\n"
                                     "2. Configure environment variable (%2)\n"
                                     "3. Add it to ~/.hynirc file")
                                 .arg(m_currentProvider)
                                 .arg(envVar));
        return;
    }

    // Handle multi-turn conversation
    bool keepHistory = m_chatWidget->isMultiTurnEnabled();
    if (!keepHistory) {
        m_chatApi->get_context().clear_user_messages();
        qCInfo(hyniGui) << "Cleared conversation history (multi-turn disabled)";
    }

    // Disable send button
    m_chatWidget->setSendEnabled(false);

    // Add user message to display
    m_chatWidget->appendMessage(ChatWidget::User, message);

    // Check if streaming is requested and supported
    auto providerInfo = m_providerManager->getProvider(m_currentProvider);
    bool useStreaming = m_chatWidget->isStreamingEnabled() &&
                        providerInfo->supportsStreaming;

    qCInfo(hyniGui) << "Streaming requested:" << m_chatWidget->isStreamingEnabled()
                    << "Provider supports:" << providerInfo->supportsStreaming
                    << "Will use streaming:" << useStreaming;

    // Cancel any existing operation
    cancelCurrentOperation();

    // Create appropriate worker
    if (useStreaming) {
        m_streamingWorker = new StreamingApiWorker(m_chatApi.get(), message, keepHistory, this);

        connect(m_streamingWorker, &ApiWorker::chunkReceived,
                this, &MainWindow::onStreamingChunk);
        connect(m_streamingWorker, &ApiWorker::errorOccurred,
                this, &MainWindow::onApiError);
        connect(m_streamingWorker, &ApiWorker::finished,
                this, &MainWindow::onWorkerFinished);

        m_streamingWorker->start();
    } else {
        m_nonStreamingWorker = new NonStreamingApiWorker(m_chatApi.get(), message, keepHistory, this);

        connect(m_nonStreamingWorker, &ApiWorker::responseReceived,
                this, &MainWindow::onResponseReceived);
        connect(m_nonStreamingWorker, &ApiWorker::errorOccurred,
                this, &MainWindow::onApiError);
        connect(m_nonStreamingWorker, &ApiWorker::finished,
                this, &MainWindow::onWorkerFinished);

        m_nonStreamingWorker->start();
    }
}

void MainWindow::clearConversation()
{
    auto reply = QMessageBox::question(
        this,
        "Clear Conversation",
        "Are you sure you want to clear the conversation?",
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply == QMessageBox::Yes) {
        qCInfo(hyniGui) << "Clearing conversation";
        m_chatWidget->clearConversation();

        if (m_chatApi) {
            m_chatApi->get_context().clear_user_messages();

            // Re-apply system message if set
            if (!m_systemMessage.isEmpty()) {
                auto providerInfo = m_providerManager->getProvider(m_currentProvider);
                if (providerInfo->supportsSystemMessages) {
                    m_chatApi->get_context().set_system_message(m_systemMessage.toStdString());
                }
            }
        }
    }
}

void MainWindow::selectSchemaDirectory()
{
    qCInfo(hyniGui) << "Opening schema directory selection dialog";

    QString dirPath = QFileDialog::getExistingDirectory(
        this,
        "Select Schema Directory",
        m_schemaDir,
        QFileDialog::ShowDirsOnly
        );

    if (!dirPath.isEmpty()) {
        qCInfo(hyniGui) << "User selected schema directory:" << dirPath;
        m_schemaDir = dirPath;
        m_settings.setValue("schema_dir", dirPath);
        loadSchemasFromDirectory(dirPath);
    }
}

void MainWindow::reloadSchemas()
{
    qCInfo(hyniGui) << "Reloading schemas";
    if (QDir(m_schemaDir).exists()) {
        loadSchemasFromDirectory(m_schemaDir);
    } else {
        QMessageBox::warning(
            this,
            "Schema Directory Not Found",
            QString("Schema directory not found: %1\n\n"
                    "Please select a valid schema directory.")
                .arg(m_schemaDir)
            );
    }
}

void MainWindow::reloadApiKeys()
{
    qCInfo(hyniGui) << "Reloading API keys";

    // Reload keys for all loaded providers
    for (const QString &displayName : m_providerManager->getProviderNames()) {
        auto providerInfo = m_providerManager->getProvider(displayName);
        QString apiKey = getApiKeyForProvider(providerInfo->name);

        if (!apiKey.isEmpty()) {
            m_apiKeys[displayName.toStdString()] = apiKey.toStdString();

            // Determine source
            QString envVar = getEnvVarName(providerInfo->name);
            if (!qEnvironmentVariable(envVar.toUtf8()).isEmpty()) {
                m_apiKeySources[displayName.toStdString()] = "environment";
            } else {
                m_apiKeySources[displayName.toStdString()] = ".hynirc";
            }
        }
    }

    updateProviderMenu();
    updateApiKeyStatus();

    // Update current provider if key was loaded
    if (!m_currentProvider.isEmpty() &&
        m_apiKeys.find(m_currentProvider.toStdString()) != m_apiKeys.end() &&
        m_chatApi) {
        m_chatApi->get_context().set_api_key(
            m_apiKeys[m_currentProvider.toStdString()]);
    }

    QMessageBox::information(this, "API Keys Reloaded",
                             QString("Found API keys for %1 provider(s)")
                                 .arg(m_apiKeys.size()));
}

void MainWindow::setApiKey()
{
    if (!m_currentProvider.isEmpty()) {
        setApiKeyForProvider(m_currentProvider);
    } else {
        QMessageBox::warning(this, "No Provider Selected",
                             "Please select a provider first.");
    }
}

void MainWindow::setSystemMessage()
{
    if (m_currentProvider.isEmpty()) {
        QMessageBox::warning(this, "No Provider Selected",
                             "Please select a provider first.");
        return;
    }

    auto providerInfo = m_providerManager->getProvider(m_currentProvider);
    if (!providerInfo->supportsSystemMessages) {
        QMessageBox::warning(this, "Not Supported",
                             QString("%1 does not support system messages.")
                                 .arg(m_currentProvider));
        return;
    }

    SystemMessageDialog dialog(m_systemMessage, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_systemMessage = dialog.getSystemMessage();

        if (m_chatApi && !m_systemMessage.isEmpty()) {
            m_chatApi->get_context().set_system_message(m_systemMessage.toStdString());
            qCInfo(hyniGui) << "Set system message:" << m_systemMessage.left(50) << "...";
            m_chatWidget->appendMessage(ChatWidget::System,
                                        QString("System message set: %1").arg(m_systemMessage));
        } else if (m_systemMessage.isEmpty()) {
            if (m_chatApi) {
                m_chatApi->get_context().clear_system_message();
            }
            qCInfo(hyniGui) << "Cleared system message";
            m_chatWidget->appendMessage(ChatWidget::System, "System message cleared");
        }
    }
}

void MainWindow::viewApiKeyStatus()
{
    QString statusText = "API Key Status:\n\n";

    for (const QString &displayName : m_providerManager->getProviderNames()) {
        auto providerInfo = m_providerManager->getProvider(displayName);
        QString envVar = getEnvVarName(providerInfo->name);

        auto it = m_apiKeys.find(displayName.toStdString());
        if (it != m_apiKeys.end()) {
            auto sourceIt = m_apiKeySources.find(displayName.toStdString());
            QString source = (sourceIt != m_apiKeySources.end()) ?
                                 QString::fromStdString(sourceIt->second) : "manual";

            // Show masked key
            QString key = QString::fromStdString(it->second);
            QString maskedKey = key.length() > 8 ?
                                    key.left(4) + "..." + key.right(4) : "***";

            statusText += QString("✓ %1: %2 (from %3)\n")
                              .arg(displayName).arg(maskedKey).arg(source);
        } else {
            statusText += QString("✗ %1: Not set\n").arg(displayName);
            statusText += QString("   Set via: %1\n").arg(envVar);
        }
    }

    QMessageBox::information(this, "API Key Status", statusText);
}

void MainWindow::showDebugInfo()
{
    QString debugInfo = "=== Hyni Debug Information ===\n\n";

    // Environment variables
    debugInfo += "Environment Variables (API Keys):\n";
    QStringList envVars = {"OA_API_KEY", "CL_API_KEY", "DS_API_KEY", "MS_API_KEY"};

    for (const QString &key : envVars) {
        QString value = qEnvironmentVariable(key.toUtf8());
        if (!value.isEmpty()) {
            QString masked = value.length() > 8 ?
                                 value.left(4) + "..." + value.right(4) : "***";
            debugInfo += QString("  %1 = %2\n").arg(key).arg(masked);
        } else {
            debugInfo += QString("  %1 = <not set>\n").arg(key);
        }
    }

    debugInfo += "\n";

    // System message
    if (!m_systemMessage.isEmpty()) {
        debugInfo += QString("System Message: %1...\n\n")
        .arg(m_systemMessage.left(100));
    } else {
        debugInfo += "System Message: <not set>\n\n";
    }

    // Loaded providers
    debugInfo += "Loaded Providers:\n";
    for (const QString &displayName : m_providerManager->getProviderNames()) {
        auto providerInfo = m_providerManager->getProvider(displayName);
        debugInfo += QString("\n  %1:\n").arg(displayName);
        debugInfo += QString("    Schema name: %1\n").arg(providerInfo->name);
        debugInfo += QString("    Endpoint: %1\n").arg(providerInfo->endpoint);
        debugInfo += QString("    Expected env var: %1\n").arg(getEnvVarName(providerInfo->name));
        debugInfo += QString("    Supports system messages: %1\n")
                         .arg(providerInfo->supportsSystemMessages ? "yes" : "no");
        debugInfo += QString("    Supports streaming: %1\n")
                         .arg(providerInfo->supportsStreaming ? "yes" : "no");

        auto it = m_apiKeys.find(displayName.toStdString());
        if (it != m_apiKeys.end()) {
            QString key = QString::fromStdString(it->second);
            QString masked = key.length() > 8 ?
                                 key.left(4) + "..." + key.right(4) : "***";

            auto sourceIt = m_apiKeySources.find(displayName.toStdString());
            QString source = (sourceIt != m_apiKeySources.end()) ?
                                 QString::fromStdString(sourceIt->second) : "unknown";

            debugInfo += QString("    API Key: %1 (from %2)\n").arg(masked).arg(source);
        } else {
            debugInfo += "    API Key: <not loaded>\n";
        }
    }

    debugInfo += "\n";

    // Current state
    if (!m_currentProvider.isEmpty() && m_chatApi) {
        debugInfo += QString("Current Provider: %1\n").arg(m_currentProvider);
        debugInfo += QString("Current Model: %1\n").arg(m_currentModel);
        debugInfo += QString("Multi-turn enabled: %1\n")
                         .arg(m_chatWidget->isMultiTurnEnabled() ? "yes" : "no");
        debugInfo += QString("Markdown enabled: %1\n")
                         .arg(m_chatWidget->isMarkdownEnabled() ? "yes" : "no");

        // Message history
        const auto &messages = m_chatApi->get_context().get_messages();
        debugInfo += QString("\nConversation History (%1 messages):\n")
                         .arg(messages.size());

        int i = 1;
        for (const auto &msg : messages) {
            std::string role = msg.value("role", "unknown");
            std::string content;

            if (msg.contains("content")) {
                auto contentVal = msg["content"];
                if (contentVal.is_string()) {
                    content = contentVal.get<std::string>();
                } else if (contentVal.is_array() && !contentVal.empty()) {
                    auto firstItem = contentVal[0];
                    if (firstItem.is_object() && firstItem.contains("text")) {
                        content = firstItem["text"].get<std::string>();
                    } else if (firstItem.is_string()) {
                        content = firstItem.get<std::string>();
                    }
                }
            }

            debugInfo += QString("  %1. %2: %3...\n")
                             .arg(i++)
                             .arg(QString::fromStdString(role))
                             .arg(QString::fromStdString(content).left(50));
        }
    }

    DebugDialog dialog(this);
    dialog.setDebugInfo(debugInfo);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QString aboutText = "Hyni - C++ LLM Interface\n\n";
    aboutText += "A Qt6-based GUI for interacting with various Language Models.\n\n";

    if (m_providerManager->size() > 0) {
        aboutText += "Loaded Providers:\n";
        for (const QString &displayName : m_providerManager->getProviderNames()) {
            auto info = m_providerManager->getProvider(displayName);
            aboutText += QString("• %1 (v%2)\n").arg(displayName).arg(info->version);

            QStringList features;
            if (info->supportsStreaming) features.append("streaming");
            if (info->supportsVision) features.append("vision");
            if (info->supportsSystemMessages) features.append("system messages");

            if (!features.isEmpty()) {
                aboutText += QString("  Supports: %1\n").arg(features.join(", "));
            }
        }
    }

    aboutText += "\nFeatures:\n";
    aboutText += "• Dynamic provider loading from schema files\n";
    aboutText += "• Automatic API key loading from environment\n";
    aboutText += "• Support for ~/.hynirc configuration\n";
    aboutText += "• Markdown rendering for responses\n";
    aboutText += "• Optional streaming responses\n";
    aboutText += "• Multi-turn conversation support\n";
    aboutText += "• System message configuration\n";
    aboutText += "• Multiple model selection per provider\n\n";

    aboutText += "API Key Environment Variables:\n";
    aboutText += "• OA_API_KEY (OpenAI)\n";
    aboutText += "• CL_API_KEY (Claude)\n";
    aboutText += "• DS_API_KEY (DeepSeek)\n";
    aboutText += "• MS_API_KEY (Mistral)\n\n";

    aboutText += "Version: 1.0.0\n";
    aboutText += "© 2024 Hyni Project";

    QMessageBox::about(this, "About Hyni", aboutText);
}

void MainWindow::changeProvider(const QString &providerName)
{
    if (providerName != m_currentProvider) {
        qCInfo(hyniGui) << "Changing provider from" << m_currentProvider
                        << "to" << providerName;

        // Cancel any ongoing operations
        cancelCurrentOperation();

        // Check if API key is set for the new provider
        if (m_apiKeys.find(providerName.toStdString()) == m_apiKeys.end()) {
            auto providerInfo = m_providerManager->getProvider(providerName);
            QString envVar = getEnvVarName(providerInfo->name);

            auto reply = QMessageBox::question(
                this,
                "API Key Required",
                QString("No API key found for %1.\n\n"
                        "You can set it:\n"
                        "1. Via environment variable (%2)\n"
                        "2. In ~/.hynirc file\n"
                        "3. Manually now\n\n"
                        "Would you like to set it manually now?")
                    .arg(providerName).arg(envVar),
                QMessageBox::Yes | QMessageBox::No
                );

            if (reply == QMessageBox::Yes) {
                if (!setApiKeyForProvider(providerName)) {
                    return;
                }
            } else {
                return;
            }
        }

        setupProvider(providerName);
    }
}

void MainWindow::onModelChanged(const QString &modelName)
{
    if (!modelName.isEmpty() && m_chatApi) {
        qCInfo(hyniGui) << "Changing model to:" << modelName;
        m_currentModel = modelName;
        m_chatApi->get_context().set_model(modelName.toStdString());
    }
}

void MainWindow::onProviderLoaded(const QString &providerName, std::shared_ptr<ProviderInfo> info)
{
    qCInfo(hyniGui) << "Provider loaded signal received:" << providerName;

    if (!info) {
        qCCritical(hyniGui) << "Received null ProviderInfo for" << providerName;
        return;
    }

    m_providerManager->addProvider(providerName, info);
    qCInfo(hyniGui) << "Added provider to manager. Total providers:"
                    << m_providerManager->size();

    // Load API key for this provider
    QString apiKey = getApiKeyForProvider(info->name);
    if (!apiKey.isEmpty()) {
        m_apiKeys[providerName.toStdString()] = apiKey.toStdString();

        // Determine source
        QString envVar = getEnvVarName(info->name);
        if (!qEnvironmentVariable(envVar.toUtf8()).isEmpty()) {
            m_apiKeySources[providerName.toStdString()] = "environment";
        } else {
            m_apiKeySources[providerName.toStdString()] = ".hynirc";
        }

        qCInfo(hyniGui) << "Loaded API key for" << providerName
                        << "from" << QString::fromStdString(m_apiKeySources[providerName.toStdString()]);
    }
}

void MainWindow::onSchemaError(const QString &error)
{
    qCCritical(hyniGui) << "Schema loading error:" << error;
    m_chatWidget->appendMessage(ChatWidget::Error, error);
}

void MainWindow::onSchemasLoaded()
{
    qCInfo(hyniGui) << "Schema loading completed. Loaded"
                    << m_providerManager->size() << "providers";

    if (m_providerManager->size() == 0) {
        showNoSchemasMessage();
        return;
    }

    // Populate provider menu
    m_providerMenu->setEnabled(true);

    for (const QString &displayName : m_providerManager->getProviderNames()) {
        auto *action = new QAction(displayName, this);
        action->setCheckable(true);

        // Add checkmark if API key is available
        if (m_apiKeys.find(displayName.toStdString()) != m_apiKeys.end()) {
            action->setText(QString("%1 ✓").arg(displayName));
        }

        connect(action, &QAction::triggered,
                [this, displayName]() { changeProvider(displayName); });
        m_providerMenu->addAction(action);
    }

    // Select first provider if available
    QStringList providers = m_providerManager->getProviderNames();
    if (!providers.isEmpty()) {
        setupProvider(providers.first());
    }

    // Show status
    m_chatWidget->appendMessage(
        ChatWidget::System,
        QString("Loaded %1 provider(s) from %2")
            .arg(m_providerManager->size())
            .arg(m_schemaDir)
        );

    // Show API key status
    if (!m_apiKeys.empty()) {
        QStringList providersWithKeys;
        for (const auto &[provider, source] : m_apiKeySources) {
            providersWithKeys.append(QString("%1 (%2)")
                                         .arg(QString::fromStdString(provider))
                                         .arg(QString::fromStdString(source)));
        }

        QString message = QString("API keys found for: %1")
                              .arg(providersWithKeys.join(", "));
        m_chatWidget->appendMessage(ChatWidget::System, message);
    }
}

void MainWindow::onStreamingChunk(const QString &chunk)
{
    m_chatWidget->appendStreamingChunk(chunk, m_currentModel);
}

void MainWindow::onResponseReceived(const QString &response)
{
    qCInfo(hyniGui) << "Response received";
    m_chatWidget->appendMessage(ChatWidget::Assistant, response, m_currentModel);
}

void MainWindow::onApiError(const QString &error)
{
    qCCritical(hyniGui) << "API error:" << error;
    m_chatWidget->appendMessage(ChatWidget::Error, error);
}

void MainWindow::onWorkerFinished()
{
    qCInfo(hyniGui) << "Worker finished";

    m_chatWidget->finishStreamingResponse();
    m_chatWidget->setSendEnabled(true);

    // Clean up workers
    if (m_streamingWorker) {
        m_streamingWorker->deleteLater();
        m_streamingWorker = nullptr;
    }
    if (m_nonStreamingWorker) {
        m_nonStreamingWorker->deleteLater();
        m_nonStreamingWorker = nullptr;
    }
}

// Private helper methods
void MainWindow::loadSchemasFromDirectory(const QString &directory)
{
    qCInfo(hyniGui) << "Loading schemas from directory:" << directory;

    // Clear existing providers
    m_providerManager->clear();
    m_providerMenu->clear();
    m_providerMenu->setEnabled(false);
    m_modelCombo->clear();

    // Show progress dialog
    auto *progress = new QProgressDialog("Loading schemas...", "Cancel", 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->show();

    // Create and start schema loader
    if (m_schemaLoader) {
        m_schemaLoader->wait(); // Wait for any existing loader to finish
        delete m_schemaLoader;
    }

    m_schemaLoader = new SchemaLoader(directory, this);

    // Use Qt::DirectConnection to ensure slots are called immediately
    connect(m_schemaLoader, &SchemaLoader::providerLoaded,
            this, &MainWindow::onProviderLoaded, Qt::DirectConnection);
    connect(m_schemaLoader, &SchemaLoader::errorOccurred,
            this, &MainWindow::onSchemaError, Qt::DirectConnection);

    // Use a lambda to ensure we process after the thread is done
    connect(m_schemaLoader, &QThread::finished, this, [this, progress]() {
        progress->close();
        progress->deleteLater();

        // Process the loaded schemas
        QTimer::singleShot(0, this, [this]() {
            onSchemasLoaded();

            // Clean up the loader
            if (m_schemaLoader) {
                m_schemaLoader->deleteLater();
                m_schemaLoader = nullptr;
            }
        });
    });

    m_schemaLoader->start();
}

void MainWindow::setupProvider(const QString &providerName)
{
    if (!m_providerManager->hasProvider(providerName)) {
        qCWarning(hyniGui) << "Provider not found:" << providerName;
        return;
    }

    qCInfo(hyniGui) << "Setting up provider:" << providerName;
    try {
        auto providerInfo = m_providerManager->getProvider(providerName);

        // Create context config
        hyni::context_config config;
        config.enable_streaming_support = false; // Disabled by default
        config.enable_validation = true;
        config.default_temperature = 0.7;
        config.default_max_tokens = 2000;

        // Build chat API using the schema path
        auto builder = hyni::chat_api_builder<>::create()
                           .schema(providerInfo->schemaPath.toStdString())
                           .config(config);

        // Set API key if available
        auto apiKey = getApiKeyForProvider(providerInfo->name);
        if (!apiKey.isEmpty()) {
            builder.api_key(apiKey.toStdString());
            qCDebug(hyniGui) << "Set API key for" << providerName;
        } else {
            qCWarning(hyniGui) << "No API key available for" << providerName;
        }

        m_chatApi = builder.build();

        // Set system message if available
        if (!m_systemMessage.isEmpty() && providerInfo->supportsSystemMessages) {
            m_chatApi->get_context().set_system_message(m_systemMessage.toStdString());
            qCDebug(hyniGui) << "Set system message for" << providerName;
        }

        // Update model list
        m_modelCombo->clear();
        m_modelCombo->addItems(providerInfo->availableModels);
        if (!providerInfo->defaultModel.isEmpty()) {
            m_modelCombo->setCurrentText(providerInfo->defaultModel);
            m_chatApi->get_context().set_model(providerInfo->defaultModel.toStdString());
        }

        m_currentProvider = providerName;
        updateProviderMenu();
        updateApiKeyStatus();
        m_statusLabel->setText(QString("Provider: %1").arg(providerName));

        // Update streaming checkbox state
        m_chatWidget->setStreamingEnabled(providerInfo->supportsStreaming);

        qCInfo(hyniGui) << "Provider" << providerName << "setup completed";

    } catch (const std::exception &e) {
        qCCritical(hyniGui) << "Failed to setup provider" << providerName << ":" << e.what();
        QMessageBox::critical(this, "Error",
                              QString("Failed to setup provider: %1").arg(e.what()));
    }
}

void MainWindow::updateProviderMenu()
{
    for (QAction *action : m_providerMenu->actions()) {
        QString displayName = action->text().replace(" ✓", "");

        // Update checkmark for API key availability
        if (m_apiKeys.find(displayName.toStdString()) != m_apiKeys.end()) {
            action->setText(QString("%1 ✓").arg(displayName));
        } else {
            action->setText(displayName);
        }

        // Update checked state
        action->setChecked(displayName == m_currentProvider);
    }
}

void MainWindow::updateApiKeyStatus()
{
    // This would update a status widget if you have one
    // For now, just log the status
    if (!m_currentProvider.isEmpty() &&
        m_apiKeys.find(m_currentProvider.toStdString()) != m_apiKeys.end()) {
        auto sourceIt = m_apiKeySources.find(m_currentProvider.toStdString());
        QString source = (sourceIt != m_apiKeySources.end()) ?
                             QString::fromStdString(sourceIt->second) : "manual";
        qCDebug(hyniGui) << "API key available for" << m_currentProvider
                         << "from" << source;
    } else {
        qCDebug(hyniGui) << "No API key for current provider";
    }
}

void MainWindow::cancelCurrentOperation()
{
    qCInfo(hyniGui) << "Cancelling current operation";

    if (m_streamingWorker && m_streamingWorker->isRunning()) {
        m_streamingWorker->cancel();
        m_streamingWorker->wait();
    }
    if (m_nonStreamingWorker && m_nonStreamingWorker->isRunning()) {
        m_nonStreamingWorker->cancel();
        m_nonStreamingWorker->wait();
    }
}

bool MainWindow::setApiKeyForProvider(const QString &providerName)
{
    if (!m_providerManager->hasProvider(providerName)) {
        return false;
    }

    auto providerInfo = m_providerManager->getProvider(providerName);
    auto it = m_apiKeys.find(providerName.toStdString());
    QString currentKey = (it != m_apiKeys.end()) ?
                             QString::fromStdString(it->second) : QString();

    // Show current status in dialog
    QString prompt;
    if (!currentKey.isEmpty()) {
        auto sourceIt = m_apiKeySources.find(providerName.toStdString());
        QString source = (sourceIt != m_apiKeySources.end()) ?
                             QString::fromStdString(sourceIt->second) : "manual";

        QString maskedKey = currentKey.length() > 8 ?
                                currentKey.left(4) + "..." + currentKey.right(4) : "***";

        prompt = QString("Current key (%1): %2\n\nEnter new API key for %3:")
                     .arg(source).arg(maskedKey).arg(providerName);
    } else {
        prompt = QString("Enter API key for %1:").arg(providerName);
    }

    bool ok;
    QString key = QInputDialog::getText(
        this,
        QString("Set API Key for %1").arg(providerName),
        prompt,
        QLineEdit::Password,
        QString(),
        &ok
        );

    if (ok && !key.isEmpty()) {
        qCInfo(hyniGui) << "Setting manual API key for" << providerName;
        m_apiKeys[providerName.toStdString()] = key.toStdString();
        m_apiKeySources[providerName.toStdString()] = "manual";

        if (providerName == m_currentProvider && m_chatApi) {
            m_chatApi->get_context().set_api_key(key.toStdString());
        }

        updateProviderMenu();
        updateApiKeyStatus();
        return true;
    }

    return false;
}

QString MainWindow::getApiKeyForProvider(const QString &providerName) const
{
    auto it = m_apiKeys.find(providerName.toStdString());
    if (it != m_apiKeys.end()) {
        return QString::fromStdString(it->second);
    }

    // Try environment variable
    auto envVar = getEnvVarName(providerName);
    auto envValue = qEnvironmentVariable(envVar.toUtf8());
    if (!envValue.isEmpty()) {
        return envValue;
    }

    // Try .hynirc file
    QFile rcFile(QDir::homePath() + "/.hynirc");
    if (rcFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&rcFile);
        while (!stream.atEnd()) {
            QString line = stream.readLine().trimmed();
            if (line.startsWith(QString("export %1=").arg(envVar)) ||
                line.startsWith(QString("%1=").arg(envVar))) {
                auto parts = line.split('=', Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    QString value = parts[1].trimmed();
                    // Remove quotes
                    if ((value.startsWith('"') && value.endsWith('"')) ||
                        (value.startsWith('\'') && value.endsWith('\''))) {
                        value = value.mid(1, value.length() - 2);
                    }
                    return value;
                }
            }
        }
    }

    return QString();
}

QString MainWindow::getEnvVarName(const QString &providerName) const
{
    static const QMap<QString, QString> envMapping = {
        {"claude", "CL_API_KEY"},
        {"openai", "OA_API_KEY"},
        {"deepseek", "DS_API_KEY"},
        {"mistral", "MS_API_KEY"}
    };

    auto lowerName = providerName.toLower();
    if (envMapping.contains(lowerName)) {
        return envMapping[lowerName];
    }

    return QString("%1_API_KEY").arg(providerName.toUpper());
}

void MainWindow::showNoSchemasMessage()
{
    qCWarning(hyniGui) << "No schema files found";
    m_chatWidget->appendMessage(
        ChatWidget::System,
        "No schema files found. Please select a directory containing schema JSON files "
        "via File → Select Schema Directory."
        );
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    qCInfo(hyniGui) << "Hyni GUI closing";

    cancelCurrentOperation();

    m_settings.setValue("schema_dir", m_schemaDir);

    event->accept();
}
