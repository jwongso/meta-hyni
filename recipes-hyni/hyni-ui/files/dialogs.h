#pragma once

#include <QDialog>
#include <QString>
#include <QClipboard>
#include <QGuiApplication>

QT_BEGIN_NAMESPACE
class QPlainTextEdit;
class QTextBrowser;
QT_END_NAMESPACE

class SystemMessageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SystemMessageDialog(const QString &currentMessage = QString(),
                                 QWidget *parent = nullptr);

    QString getSystemMessage() const;

private:
    QPlainTextEdit *m_textEdit;
};

class DebugDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DebugDialog(QWidget *parent = nullptr);

    void setDebugInfo(const QString &info);

signals:
    void refreshRequested();

private:
    QTextBrowser *m_textBrowser;
};
