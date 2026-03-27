#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QFile>
#include <QElapsedTimer>
#include <QMap>
#include <QPair>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void connectSerial();
    void readSerialData();
    void chooseLogFile();
    void saveHtmlLog();

private:
    QSerialPort *serial;
    QTimer *speedTimer;
    QElapsedTimer readLagTimer;

    QComboBox *portComboBox;
    QPushButton *connectButton;
    QPushButton *saveButton;
    QTextEdit *logTextEdit;

    QFile logFile;

    QByteArray serialBuffer;
    qint64 totalBytesReceived = 0;
    qint64 maxBytesAvailableSeen = 0;
    qint64 overflowThreshold = 60 * 1024; // 60 KB

    // Test case Pass/Fail tracking
    QMap<QString, QPair<int,int>> testResults;

    void processLine(const QString &line);
};

#endif // MAINWINDOW_H
