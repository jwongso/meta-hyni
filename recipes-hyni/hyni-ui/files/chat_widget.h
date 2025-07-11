#pragma once

#include <QWidget>
#include <QString>

QT_BEGIN_NAMESPACE
class QTextBrowser;
class QTextEdit;
class QPushButton;
class QCheckBox;
QT_END_NAMESPACE

class ChatWidget : public QWidget
{
    Q_OBJECT

public:
    enum MessageRole {
        User,
        Assistant,
        System,
        Error
    };

    explicit ChatWidget(QWidget *parent = nullptr);

    void appendMessage(MessageRole role, const QString &content,
                       const QString &modelName = QString());
    void appendStreamingChunk(const QString &chunk, const QString &modelName = QString());
    void finishStreamingResponse();
    void clearConversation();

    QString getInputText();
    bool isStreamingEnabled() const;
    bool isMultiTurnEnabled() const;
    bool isMarkdownEnabled() const;
    void setStreamingEnabled(bool enabled);
    void setSendEnabled(bool enabled);

signals:
    void sendRequested();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onMarkdownToggled(int state);

private:
    void initUI();
    QString renderMarkdown(const QString &content);

private:
    QTextBrowser *m_conversationDisplay;
    QTextEdit *m_inputText;
    QPushButton *m_sendButton;
    QCheckBox *m_streamingCheckbox;
    QCheckBox *m_multiTurnCheckbox;
    QCheckBox *m_markdownCheckbox;

    int m_currentResponsePosition = -1;
    QString m_currentResponseText;
    bool m_useMarkdown = true;
};
