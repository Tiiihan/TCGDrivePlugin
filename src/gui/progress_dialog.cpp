#include "progress_dialog.h"
#include "ui_progress_dialog.h"

#include "../utils/logger.h"

#include <QPushButton>

ProgressDialog::ProgressDialog(const QString& filename, qint64 totalSize, QWidget* parent)
    : QDialog(parent),
      ui(new Ui::ProgressDialog),
      m_totalSize(totalSize) {
    ui->setupUi(this);
    setWindowTitle(tr("Transferring file"));

    ui->filenameLabel->setText(filename);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);
    ui->speedLabel->setText(humanRate(0.0));
    ui->timeLabel->setText(tr("Remaining: %1").arg(QStringLiteral("--:--:--")));

    connect(ui->cancelButton, &QPushButton::clicked, this, &ProgressDialog::onCancelClicked);

    m_clock.start();
}

ProgressDialog::~ProgressDialog() {
    delete ui;
}

void ProgressDialog::updateProgress(qint64 bytesTransferred, qint64 total) {
    if (total > 0) m_totalSize = total;

    int pct = 0;
    if (m_totalSize > 0) {
        pct = static_cast<int>((bytesTransferred * 100) / m_totalSize);
        pct = qBound(0, pct, 100);
    }
    ui->progressBar->setValue(pct);

    const qint64 nowMs = m_clock.elapsed();
    const qint64 dtMs  = nowMs - m_lastSampleMs;
    if (dtMs >= 250) {
        const qint64  dBytes   = bytesTransferred - m_lastBytes;
        const double  instRate = (dtMs > 0)
            ? (static_cast<double>(dBytes) * 1000.0 / static_cast<double>(dtMs))
            : 0.0;
        m_smoothedRate = (m_smoothedRate <= 0.0)
            ? instRate
            : (0.7 * m_smoothedRate + 0.3 * instRate);

        m_lastBytes    = bytesTransferred;
        m_lastSampleMs = nowMs;

        ui->speedLabel->setText(humanRate(m_smoothedRate));

        if (m_smoothedRate > 1.0 && m_totalSize > bytesTransferred) {
            const qint64 remSecs = static_cast<qint64>(
                static_cast<double>(m_totalSize - bytesTransferred) / m_smoothedRate);
            ui->timeLabel->setText(tr("Remaining: %1").arg(humanDuration(remSecs)));
        } else {
            ui->timeLabel->setText(tr("Remaining: %1").arg(QStringLiteral("--:--:--")));
        }
    }

    if (pct >= 100) {
        accept();
    }
}

void ProgressDialog::onCancelClicked() {
    m_cancelled.store(true);
    Logger::instance().info("ProgressDialog: cancellation requested for "
                            + ui->filenameLabel->text().toStdString());
    ui->cancelButton->setEnabled(false);
    ui->cancelButton->setText(tr("Cancelling..."));
}

QString ProgressDialog::humanRate(double bytesPerSec) {
    static const char* const units[] = { "B/s", "KB/s", "MB/s", "GB/s" };
    int    u = 0;
    double v = bytesPerSec < 0.0 ? 0.0 : bytesPerSec;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    return QString::number(v, 'f', u == 0 ? 0 : 1) + QLatin1Char(' ')
         + QLatin1String(units[u]);
}

QString ProgressDialog::humanDuration(qint64 seconds) {
    if (seconds < 0) seconds = 0;
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

#include "moc_progress_dialog.cpp"
