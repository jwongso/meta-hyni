#include "dialogs.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QFont>
#include <QPushButton>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(hyniDialogs, "hyni.gui.dialogs")

// SystemMessageDialog implementation
SystemMessageDialog::SystemMessageDialog(const QString &currentMessage, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Set System Message");
    setModal(true);
    resize(600, 400);

    auto *layout = new QVBoxLayout(this);

    // Instructions
    auto *instructions = new QLabel("Enter system message (instructions for the AI):");
    layout->addWidget(instructions);

    // Text editor
    m_textEdit = new QPlainTextEdit();
    m_textEdit->setPlainText(currentMessage);
    m_textEdit->setFont(QFont("Arial", 10));
    layout->addWidget(m_textEdit);

    // Example text
    auto *exampleLabel = new QLabel("<i>Example: You are a helpful assistant. "
                                    "Please provide clear and concise answers.</i>");
    exampleLabel->setWordWrap(true);
    exampleLabel->setStyleSheet("color: #666666; font-size: 9pt;");
    layout->addWidget(exampleLabel);

    // Buttons
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Add clear button
    auto *clearButton = buttons->addButton("Clear", QDialogButtonBox::ActionRole);
    connect(clearButton, &QPushButton::clicked, [this]() {
        m_textEdit->clear();
    });

    layout->addWidget(buttons);

    qCDebug(hyniDialogs) << "SystemMessageDialog created with message length:"
                         << currentMessage.length();
}

QString SystemMessageDialog::getSystemMessage() const
{
    return m_textEdit->toPlainText().trimmed();
}

// DebugDialog implementation
DebugDialog::DebugDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Debug Information");
    setModal(true);
    resize(700, 500);

    auto *layout = new QVBoxLayout(this);

    // Header
    auto *header = new QLabel("<b>Hyni Debug Information</b>");
    header->setAlignment(Qt::AlignCenter);
    layout->addWidget(header);

    // Text browser for debug info
    m_textBrowser = new QTextBrowser();
    m_textBrowser->setFont(QFont("Courier", 9));
    m_textBrowser->setLineWrapMode(QTextBrowser::NoWrap);
    layout->addWidget(m_textBrowser);

    // Buttons
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    // Add copy button
    auto *copyButton = buttons->addButton("Copy to Clipboard", QDialogButtonBox::ActionRole);
    connect(copyButton, &QPushButton::clicked, [this]() {
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(m_textBrowser->toPlainText());
        qCInfo(hyniDialogs) << "Debug info copied to clipboard";
    });

    // Add refresh button
    auto *refreshButton = buttons->addButton("Refresh", QDialogButtonBox::ActionRole);
    connect(refreshButton, &QPushButton::clicked, [this]() {
        emit refreshRequested();
    });

    layout->addWidget(buttons);

    qCDebug(hyniDialogs) << "DebugDialog created";
}

void DebugDialog::setDebugInfo(const QString &info)
{
    m_textBrowser->setPlainText(info);
    qCDebug(hyniDialogs) << "Debug info set, length:" << info.length();
}
