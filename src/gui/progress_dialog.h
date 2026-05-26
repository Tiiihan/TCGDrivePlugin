#pragma once

#include <QDialog>
#include <QElapsedTimer>

#include <atomic>
#include <cstdint>

QT_BEGIN_NAMESPACE
namespace Ui { class ProgressDialog; }
QT_END_NAMESPACE

class ProgressDialog : public QDialog {
    Q_OBJECT

public:
    ProgressDialog(const QString& filename, qint64 totalSize, QWidget* parent = nullptr);
    ~ProgressDialog() override;

    bool isCancelled() const { return m_cancelled.load(); }

public slots:
    void updateProgress(qint64 bytesTransferred, qint64 total);

private slots:
    void onCancelClicked();

private:
    static QString humanRate(double bytesPerSec);
    static QString humanDuration(qint64 seconds);

    Ui::ProgressDialog* ui;

    qint64            m_totalSize;
    std::atomic<bool> m_cancelled{false};

    QElapsedTimer m_clock;
    qint64        m_lastBytes    = 0;
    qint64        m_lastSampleMs = 0;
    double        m_smoothedRate = 0.0;   
};
