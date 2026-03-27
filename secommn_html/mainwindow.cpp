#include "mainwindow.h"

#include <QVBoxLayout>
#include <QTextCursor>
#include <QFileDialog>
#include <QDateTime>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      serial(new QSerialPort(this))
{
    // ---------- UI ----------
    portComboBox = new QComboBox(this);
    connectButton = new QPushButton("Connect", this);
    saveButton = new QPushButton("Save Log", this);
    QPushButton *htmlLogButton = new QPushButton("Save HTML Log", this);
    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);

    layout->addWidget(portComboBox);
    layout->addWidget(connectButton);
    layout->addWidget(saveButton);
    layout->addWidget(htmlLogButton);
    layout->addWidget(logTextEdit);

    setCentralWidget(centralWidget);
    setWindowTitle("Embedded Log Viewer");

    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        portComboBox->addItem(info.portName());
    }

    // ---------- Connections ----------
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectSerial);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::chooseLogFile);
    connect(htmlLogButton, &QPushButton::clicked, this, &MainWindow::saveHtmlLog);
    connect(serial, &QSerialPort::readyRead, this, &MainWindow::readSerialData);
    connect(serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error != QSerialPort::NoError) {
            logTextEdit->append(QString("<span style='color:red'>[SERIAL ERROR] %1</span>").arg(serial->errorString()));
        }
    });

    // ---------- Speed Timer ----------
    speedTimer = new QTimer(this);
    speedTimer->setInterval(1000);
    connect(speedTimer, &QTimer::timeout, this, [this]() {
        double kbps = totalBytesReceived / 1024.0;
        logTextEdit->append(QString("[Speed] Receiving: %1 KB/s").arg(kbps, 0, 'f', 2));
        totalBytesReceived = 0;
    });
    speedTimer->start();

    // ---------- Stall Timer ----------
    readLagTimer.start();
    QTimer *stallTimer = new QTimer(this);
    stallTimer->setInterval(1000);
    connect(stallTimer, &QTimer::timeout, this, [this]() {
        if (readLagTimer.elapsed() > 2000) {
            logTextEdit->append("<span style='color:orange'>[STALL WARNING] No serial read for >2 seconds</span>");
        }
    });
    stallTimer->start();
}

MainWindow::~MainWindow()
{
    if (serial->isOpen())
        serial->close();

    if (logFile.isOpen())
        logFile.close();
}

void MainWindow::connectSerial()
{
    if (serial->isOpen()) {
        serial->close();
        connectButton->setText("Connect");
        logTextEdit->append("Disconnected.");
        return;
    }

    serial->setPortName(portComboBox->currentText());
    serial->setBaudRate(QSerialPort::Baud115200);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);
    serial->setReadBufferSize(64 * 1024);

    if (serial->open(QIODevice::ReadOnly)) {
        connectButton->setText("Disconnect");
        logTextEdit->append("Connected.");
    } else {
        logTextEdit->append("Failed to connect.");
    }
}

void MainWindow::chooseLogFile()
{
    QString timestamp = QDateTime::currentDateTime().toString("dd-MM-yyyy_HH-mm-ss");
    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Logs",
        "Gazzle_logs_" + timestamp + ".txt",
        "Text Files (*.txt);;All Files (*)");

    if (fileName.isEmpty())
        return;

    logFile.setFileName(fileName);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        logTextEdit->append("Failed to open log file.");
        return;
    }

    logTextEdit->append("Logging to file: " + fileName);
}

void MainWindow::readSerialData()
{
    readLagTimer.restart();

    qint64 available = serial->bytesAvailable();
    if (available > maxBytesAvailableSeen)
        maxBytesAvailableSeen = available;

    if (available > overflowThreshold) {
        logTextEdit->append(QString("<span style='color:orange'>[OVERFLOW WARNING] QSerialPort buffer = %1 bytes</span>").arg(available));
    }

    QByteArray data = serial->readAll();
    totalBytesReceived += data.size();

    serialBuffer += QString::fromUtf8(data);

    if (serialBuffer.size() > 100 * 1024) {
        logTextEdit->append(QString("<span style='color:orange'>[OVERFLOW WARNING] serialBuffer size = %1 bytes</span>").arg(serialBuffer.size()));
    }

    int index;
    while ((index = serialBuffer.indexOf('\n')) != -1) {
        QString line = serialBuffer.left(index).trimmed();
        serialBuffer.remove(0, index + 1);

        if (line.isEmpty())
            continue;

        processLine(line);
    }

    if (logFile.isOpen())
        logFile.flush();
}

void MainWindow::processLine(const QString &line)
{
    // ---------- Log to UI ----------
    QString logLine = QDateTime::currentDateTime().toString("[yyyy-MM-dd HH:mm:ss.zzz] ") + line + "\n";
    logTextEdit->moveCursor(QTextCursor::End);
    logTextEdit->insertPlainText(logLine);

    if (logFile.isOpen())
        logFile.write(logLine.toUtf8());

    // ---------- Pass/Fail tracking ----------
    QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        QString testName = parts[0];
        QString result = parts[1].toUpper();

        QPair<int,int> counts = testResults.value(testName, qMakePair(0,0));
        if (result == "PASS") counts.first++;
        else if (result == "FAIL") counts.second++;

        testResults[testName] = counts;
    }
}

//void MainWindow::saveHtmlLog()
//{
//    QString timestamp = QDateTime::currentDateTime().toString("dd-MM-yyyy_HH-mm-ss");
//    QString fileName = QFileDialog::getSaveFileName(this,
//        "Save HTML Log",
//        "Gazzle_log_" + timestamp + ".html",
//        "HTML Files (*.html);;All Files (*)");

//    if (fileName.isEmpty())
//        return;

//    QFile htmlFile(fileName);
//    if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
//        logTextEdit->append("Failed to create HTML log file.");
//        return;
//    }

//    QTextStream out(&htmlFile);

//    // ---------- HTML Header ----------
//    out << "<!DOCTYPE html>\n<html>\n<head>\n";
//    out << "<meta charset='UTF-8'>\n";
//    out << "<title>Embedded Log</title>\n";
//    out << "<style>\n";
//    out << "body { font-family: monospace; white-space: pre; }\n";
//    out << ".error { color: red; }\n";
//    out << ".warning { color: orange; }\n";
//    out << ".info { color: black; }\n";
//    out << "table {border-collapse: collapse; margin-top: 20px;}\n";
//    out << "th, td {border: 1px solid black; padding: 5px; text-align: center;}\n";
//    out << "th {background-color: #f2f2f2;}\n";
//    out << "</style>\n</head>\n<body>\n";

//    // ---------- Log Content ----------
//    QString allLogs = logTextEdit->toPlainText();
//    QStringList lines = allLogs.split('\n');
//    for (const QString &line : lines) {
//        QString escapedLine = line.toHtmlEscaped();
//        if (line.contains("[SERIAL ERROR]")) out << "<span class='error'>" << escapedLine << "</span><br>\n";
//        else if (line.contains("[STALL WARNING]") || line.contains("[OVERFLOW WARNING]")) out << "<span class='warning'>" << escapedLine << "</span><br>\n";
//        else out << "<span class='info'>" << escapedLine << "</span><br>\n";
//    }

//    // ---------- Pass/Fail Summary Table ----------
//    if (!testResults.isEmpty()) {
//        out << "<h2>Test Summary</h2>\n<table>\n<tr><th>Test Case</th><th>Pass</th><th>Fail</th></tr>\n";
//        for (auto it = testResults.begin(); it != testResults.end(); ++it) {
//            out << "<tr><td>" << it.key() << "</td><td>" << it.value().first << "</td><td>" << it.value().second << "</td></tr>\n";
//        }
//        out << "</table>\n";
//    }

//    out << "</body>\n</html>\n";
//    htmlFile.close();

//    logTextEdit->append("HTML log saved: " + fileName);
//}


void MainWindow::saveHtmlLog()
{
    QString timestamp = QDateTime::currentDateTime().toString("dd-MM-yyyy_HH-mm-ss");
    QString fileName = QFileDialog::getSaveFileName(this,
        "Save HTML Log",
        "Gazzle_log_" + timestamp + ".html",
        "HTML Files (*.html);;All Files (*)");

    if (fileName.isEmpty())
        return;

    QFile htmlFile(fileName);
    if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logTextEdit->append("Failed to create HTML log file.");
        return;
    }

    QTextStream out(&htmlFile);

    out << "<!DOCTYPE html>\n<html>\n<head>\n";
    out << "<meta charset='UTF-8'>\n";
    out << "<title>Embedded Log - Gazzle</title>\n";
    out << "<style>\n";
    out << "body { font-family: Arial, sans-serif; margin: 20px; }\n";
    out << "h1 { text-align: center; }\n";
    out << ".summary-table { width: 100%; border-collapse: collapse; margin-bottom: 40px; }\n";
    out << ".summary-table th { background-color: #2c3e50; color: white; padding: 10px; text-align: left; }\n";
    out << ".summary-table td { padding: 8px; border: 1px solid #ddd; }\n";
    out << ".summary-table tr:nth-child(even) { background-color: #f9f9f9; }\n";
    out << ".failed { color: red; font-weight: bold; }\n";
    out << ".log { font-family: monospace; white-space: pre-wrap; line-height: 1.4; }\n";
    out << ".error { color: red; }\n";
    out << ".warning { color: orange; }\n";
    out << "</style>\n</head>\n<body>\n";

    // ---------- Unit Test Results Summary Table (at the top) ----------
    if (!testResults.isEmpty()) {
        out << "<h1>Unit Test Results Summary</h1>\n";
        out << "<table class='summary-table'>\n";
        out << "<tr><th>Unit Test</th><th>Passed</th><th>Failed</th></tr>\n";

        // Sort test names alphabetically
        QStringList sortedKeys = testResults.keys();
        std::sort(sortedKeys.begin(), sortedKeys.end());

        int totalPassed = 0;
        int totalFailed = 0;

        for (const QString &key : sortedKeys) {
            int pass = testResults[key].first;
            int fail = testResults[key].second;
            totalPassed += pass;
            totalFailed += fail;

            QString failClass = (fail > 0) ? " class='failed'" : "";
            out << "<tr><td>" << key.toHtmlEscaped() << "</td>"
                << "<td style='text-align:center;'>" << pass << "</td>"
                << "<td style='text-align:center;" << failClass << "'>" << fail << "</td></tr>\n";
        }

        // Total row
        QString totalFailClass = (totalFailed > 0) ? " class='failed'" : "";
        out << "<tr style='font-weight:bold; background-color:#eef;'>"
            << "<td>Total</td>"
            << "<td style='text-align:center;'>" << totalPassed << "</td>"
            << "<td style='text-align:center;" << totalFailClass << "'>" << totalFailed << "</td></tr>\n";

        out << "</table>\n";
    }

    // ---------- Full Log Content ----------
    out << "<h2>Full Log</h2>\n";
    out << "<div class='log'>\n";

    QString allLogs = logTextEdit->toPlainText();
    QStringList lines = allLogs.split('\n');
    for (const QString &line : lines) {
        if (line.isEmpty()) {
            out << "<br>\n";
            continue;
        }
        QString escapedLine = line.toHtmlEscaped();
        if (line.contains("[SERIAL ERROR]"))
            out << "<span class='error'>" << escapedLine << "</span><br>\n";
        else if (line.contains("[STALL WARNING]") || line.contains("[OVERFLOW WARNING]"))
            out << "<span class='warning'>" << escapedLine << "</span><br>\n";
        else
            out << escapedLine << "<br>\n";
    }

    out << "</div>\n";

    out << "</body>\n</html>\n";
    htmlFile.close();

    logTextEdit->append("HTML log saved: " + fileName);
}
