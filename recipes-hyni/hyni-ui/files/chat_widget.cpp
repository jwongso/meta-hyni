#include "chat_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextCursor>
#include <QKeyEvent>
#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(hyniChatWidget, "hyni.gui.chat")

ChatWidget::ChatWidget(QWidget *parent)
    : QWidget(parent)
{
    initUI();
    qCInfo(hyniChatWidget) << "Chat widget initialized with Markdown support";
}

void ChatWidget::initUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);

    // Conversation display
    m_conversationDisplay = new QTextBrowser();
    m_conversationDisplay->setOpenExternalLinks(true);
    m_conversationDisplay->setFont(QFont("Arial", 10));

    // Style the conversation display
    m_conversationDisplay->setStyleSheet(R"(
        QTextBrowser {
            background-color: #ffffff;
            color: #000000;
            border: 1px solid #ddd;
            border-radius: 4px;
        }
    )");

    // Input area with checkboxes
    auto *inputLayout = new QHBoxLayout();

    m_inputText = new QTextEdit();
    m_inputText->setMaximumHeight(100);
    m_inputText->setPlaceholderText("Type your message here...");
    m_inputText->setFont(QFont("Arial", 10));

    // Style the input text
    m_inputText->setStyleSheet(R"(
        QTextEdit {
            background-color: white;
            color: black;
            border: 1px solid #ccc;
            border-radius: 4px;
            padding: 5px;
        }
        QTextEdit:focus {
            border: 2px solid #0066cc;
        }
    )");

    // Options layout
    auto *optionsLayout = new QVBoxLayout();

    // Streaming checkbox - OFF by default
    m_streamingCheckbox = new QCheckBox("Stream");
    m_streamingCheckbox->setChecked(false);
    m_streamingCheckbox->setToolTip("Enable streaming responses (when supported)");

    // Multi-turn checkbox - ON by default
    m_multiTurnCheckbox = new QCheckBox("Multi-turn");
    m_multiTurnCheckbox->setChecked(true);
    m_multiTurnCheckbox->setToolTip("Keep conversation history for context");

    // Markdown checkbox - ON by default
    m_markdownCheckbox = new QCheckBox("Markdown");
    m_markdownCheckbox->setChecked(true);
    m_markdownCheckbox->setToolTip("Render responses as Markdown");
    connect(m_markdownCheckbox, &QCheckBox::stateChanged,
            this, &ChatWidget::onMarkdownToggled);

    // FIX: Style all checkboxes with explicit colors
    QString checkboxStyle = R"(
        QCheckBox {
            color: #808080;
            background-color: transparent;
            spacing: 5px;
            padding: 2px;
        }
        QCheckBox:hover {
            color: #0066cc;
        }
        QCheckBox:disabled {
            color: #999999;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QCheckBox::indicator:unchecked {
            border: 2px solid #999999;
            background-color: #ffffff;
        }
        QCheckBox::indicator:unchecked:hover {
            border: 2px solid #0066cc;
            background-color: #f0f8ff;
        }
        QCheckBox::indicator:checked {
            border: 2px solid #0066cc;
            background-color: #0066cc;
            image: url(:/icons/check-white.png);  /* Optional: add a checkmark icon */
        }
        QCheckBox::indicator:checked:hover {
            border: 2px solid #0052a3;
            background-color: #0052a3;
        }
        QCheckBox::indicator:disabled {
            border: 2px solid #cccccc;
            background-color: #f0f0f0;
        }
    )";

    m_streamingCheckbox->setStyleSheet(checkboxStyle);
    m_multiTurnCheckbox->setStyleSheet(checkboxStyle);
    m_markdownCheckbox->setStyleSheet(checkboxStyle);

    optionsLayout->addWidget(m_streamingCheckbox);
    optionsLayout->addWidget(m_multiTurnCheckbox);
    optionsLayout->addWidget(m_markdownCheckbox);

    m_sendButton = new QPushButton("Send");
    m_sendButton->setMinimumHeight(40);
    m_sendButton->setMinimumWidth(80);
    connect(m_sendButton, &QPushButton::clicked,
            this, &ChatWidget::sendRequested);

    // Style the send button
    m_sendButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0066cc;
            color: white;
            border: none;
            border-radius: 4px;
            padding: 8px 16px;
            font-weight: bold;
            font-size: 10pt;
        }
        QPushButton:hover {
            background-color: #0052a3;
        }
        QPushButton:pressed {
            background-color: #004080;
        }
        QPushButton:disabled {
            background-color: #cccccc;
            color: #666666;
        }
    )");

    inputLayout->addWidget(m_inputText, 1);
    inputLayout->addLayout(optionsLayout);
    inputLayout->addWidget(m_sendButton);

    // Add widgets to layout
    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(m_conversationDisplay);

    auto *inputWidget = new QWidget();
    inputWidget->setLayout(inputLayout);
    splitter->addWidget(inputWidget);

    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);

    layout->addWidget(splitter);

    // Install event filter for Ctrl+Enter
    m_inputText->installEventFilter(this);
}

bool ChatWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_inputText && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return &&
            keyEvent->modifiers() == Qt::ControlModifier) {
            m_sendButton->click();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ChatWidget::appendMessage(MessageRole role, const QString &content,
                               const QString &modelName)
{
    qCDebug(hyniChatWidget) << "Appending" << role << "message:" << content.left(50) << "...";

    QTextCursor cursor = m_conversationDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);

    // Add spacing
    if (cursor.position() > 0) {
        cursor.insertHtml("<br>");
    }

    // Add timestamp
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");

    // Create container div
    cursor.insertHtml("<div style=\"margin-bottom: 15px;\">");

    // Format based on role
    switch (role) {
    case User:
        cursor.insertHtml(QString("<p style=\"color: #0066cc; margin: 5px 0; "
                                  "font-weight: bold;\">You [%1]:</p>").arg(timestamp));
        {
            QString escaped = content.toHtmlEscaped().replace("\n", "<br>");
            cursor.insertHtml(QString("<div style=\"margin-left: 20px; padding: 10px; "
                                      "background-color: #f0f0f0; border-radius: 5px;\">%1</div>")
                                  .arg(escaped));
        }
        break;

    case Assistant:
    {
        QString displayName = modelName.isEmpty() ? "Assistant" : modelName;
        cursor.insertHtml(QString("<p style=\"color: #009900; margin: 5px 0; "
                                  "font-weight: bold;\">%1 [%2]:</p>")
                              .arg(displayName).arg(timestamp));

        if (m_useMarkdown) {
            cursor.insertHtml("<div style=\"margin-left: 20px; padding: 10px; "
                              "background-color: #f8f8f8; border-radius: 5px;\">");
            cursor.insertHtml(renderMarkdown(content));
            cursor.insertHtml("</div>");
        } else {
            QString escaped = content.toHtmlEscaped().replace("\n", "<br>");
            cursor.insertHtml(QString("<div style=\"margin-left: 20px; padding: 10px; "
                                      "background-color: #f8f8f8; border-radius: 5px;\">%1</div>")
                                  .arg(escaped));
        }
    }
    break;

    case Error:
        cursor.insertHtml(QString("<p style=\"color: #cc0000;                            margin: 5px 0; font-weight: bold;\">Error [%1]:</p>")
                              .arg(timestamp));
        {
            QString escaped = content.toHtmlEscaped().replace("\n", "<br>");
            cursor.insertHtml(QString("<div style=\"margin-left: 20px; padding: 10px; "
                                      "background-color: #ffe0e0; border-radius: 5px;\">%1</div>")
                                  .arg(escaped));
        }
        break;

    case System:
        cursor.insertHtml(QString("<p style=\"color: #666666; margin: 5px 0; "
                                  "font-weight: bold;\">System [%1]:</p>").arg(timestamp));
        {
            QString escaped = content.toHtmlEscaped().replace("\n", "<br>");
            cursor.insertHtml(QString("<div style=\"margin-left: 20px; padding: 10px; "
                                      "background-color: #e8e8e8; border-radius: 5px; "
                                      "color: #666666;\">%1</div>").arg(escaped));
        }
        break;
    }

    cursor.insertHtml("</div>");

    // Scroll to bottom
    m_conversationDisplay->verticalScrollBar()->setValue(
        m_conversationDisplay->verticalScrollBar()->maximum()
        );
}

void ChatWidget::appendStreamingChunk(const QString &chunk, const QString &modelName)
{
    QTextCursor cursor = m_conversationDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);

    // If this is the first chunk, add the assistant header
    if (m_currentResponsePosition == -1) {
        if (cursor.position() > 0) {
            cursor.insertHtml("<br>");
        }

        QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
        QString displayName = modelName.isEmpty() ? "Assistant" : modelName;

        cursor.insertHtml("<div style=\"margin-bottom: 15px;\">");
        cursor.insertHtml(QString("<p style=\"color: #009900; margin: 5px 0; "
                                  "font-weight: bold;\">%1 [%2] (streaming):</p>")
                              .arg(displayName).arg(timestamp));
        cursor.insertHtml("<div style=\"margin-left: 20px; padding: 10px; "
                          "background-color: #f8f8f8; border-radius: 5px;\" "
                          "id=\"streaming-response\">");

        m_currentResponsePosition = cursor.position();
        m_currentResponseText.clear();
    }

    // Accumulate response text
    m_currentResponseText += chunk;

    // For streaming, show plain text and render markdown when complete
    QString escaped = chunk.toHtmlEscaped().replace("\n", "<br>");
    cursor.insertHtml(escaped);

    // Scroll to bottom
    m_conversationDisplay->verticalScrollBar()->setValue(
        m_conversationDisplay->verticalScrollBar()->maximum()
        );
}

void ChatWidget::finishStreamingResponse()
{
    if (m_currentResponsePosition != -1) {
        QTextCursor cursor = m_conversationDisplay->textCursor();

        // If markdown is enabled, re-render the complete response
        if (m_useMarkdown && !m_currentResponseText.isEmpty()) {
            cursor.setPosition(m_currentResponsePosition);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.insertHtml(renderMarkdown(m_currentResponseText));
        }

        // Close the divs
        cursor.movePosition(QTextCursor::End);
        cursor.insertHtml("</div></div>");

        m_currentResponsePosition = -1;
        m_currentResponseText.clear();

        qCDebug(hyniChatWidget) << "Finished streaming response display";
    }
}

QString ChatWidget::renderMarkdown(const QString &content)
{
    QTextDocument doc;
    doc.setMarkdown(content);
    return doc.toHtml();
}

void ChatWidget::clearConversation()
{
    qCInfo(hyniChatWidget) << "Clearing conversation";
    m_conversationDisplay->clear();
    m_currentResponsePosition = -1;
    m_currentResponseText.clear();
}

QString ChatWidget::getInputText()
{
    QString text = m_inputText->toPlainText().trimmed();
    m_inputText->clear();
    return text;
}

bool ChatWidget::isStreamingEnabled() const
{
    return m_streamingCheckbox->isChecked();
}

bool ChatWidget::isMultiTurnEnabled() const
{
    return m_multiTurnCheckbox->isChecked();
}

bool ChatWidget::isMarkdownEnabled() const
{
    return m_markdownCheckbox->isChecked();
}

void ChatWidget::setStreamingEnabled(bool enabled)
{
    m_streamingCheckbox->setEnabled(enabled);
    if (!enabled) {
        m_streamingCheckbox->setChecked(false);
        m_streamingCheckbox->setToolTip("Streaming not supported by this provider");
    } else {
        m_streamingCheckbox->setToolTip("Enable streaming responses");
    }
}

void ChatWidget::setSendEnabled(bool enabled)
{
    m_sendButton->setEnabled(enabled);
    m_sendButton->setText(enabled ? "Send" : "Sending...");
}

void ChatWidget::onMarkdownToggled(int state)
{
    m_useMarkdown = (state == Qt::Checked);
    qCInfo(hyniChatWidget) << "Markdown rendering:" << (m_useMarkdown ? "enabled" : "disabled");
}
