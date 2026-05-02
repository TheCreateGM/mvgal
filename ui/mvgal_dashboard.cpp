/**
 * MVGAL Qt Monitoring Dashboard — Implementation
 *
 * Polls the MVGAL REST API (or sysfs directly if the daemon is not running)
 * and displays real-time GPU metrics.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mvgal_dashboard.h"

#include <QApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QProcess>

#include <cstdio>
#include <cstring>

namespace mvgal {

/* =========================================================================
 * GpuWidget
 * ====================================================================== */

GpuWidget::GpuWidget(int gpu_index, QWidget *parent)
    : QGroupBox(QString("GPU %1").arg(gpu_index), parent)
{
    auto *layout = new QVBoxLayout(this);

    m_name_label    = new QLabel("—", this);
    m_util_bar      = new QProgressBar(this);
    m_vram_bar      = new QProgressBar(this);
    m_temp_label    = new QLabel("Temp: —", this);
    m_power_label   = new QLabel("Power: —", this);
    m_clock_label   = new QLabel("Clock: —", this);
    m_workload_label = new QLabel("Workload: idle", this);

    m_util_bar->setRange(0, 100);
    m_util_bar->setFormat("Util: %p%");
    m_vram_bar->setRange(0, 100);
    m_vram_bar->setFormat("VRAM: %p%");

    layout->addWidget(m_name_label);
    layout->addWidget(m_util_bar);
    layout->addWidget(m_vram_bar);

    auto *row = new QHBoxLayout;
    row->addWidget(m_temp_label);
    row->addWidget(m_power_label);
    row->addWidget(m_clock_label);
    layout->addLayout(row);
    layout->addWidget(m_workload_label);
}

void GpuWidget::update(const GpuMetrics &m)
{
    setTitle(QString("GPU %1 — %2").arg(m.index).arg(m.name));
    m_name_label->setText(QString("Vendor: %1").arg(m.vendor));

    m_util_bar->setValue(m.utilization);
    m_util_bar->setFormat(QString("Util: %1%").arg(m.utilization));

    int vram_pct = (m.vram_total_gb > 0)
        ? static_cast<int>(m.vram_used_gb / m.vram_total_gb * 100.0)
        : 0;
    m_vram_bar->setValue(vram_pct);
    m_vram_bar->setFormat(
        QString("VRAM: %1 / %2 GiB (%3%)")
            .arg(m.vram_used_gb, 0, 'f', 1)
            .arg(m.vram_total_gb, 0, 'f', 1)
            .arg(vram_pct));

    m_temp_label->setText(
        m.temperature_c > 0
            ? QString("Temp: %1 °C").arg(m.temperature_c)
            : "Temp: —");
    m_power_label->setText(
        m.power_w > 0
            ? QString("Power: %1 W").arg(m.power_w, 0, 'f', 1)
            : "Power: —");
    m_clock_label->setText(
        m.clock_mhz > 0
            ? QString("Clock: %1 MHz").arg(m.clock_mhz)
            : "Clock: —");
    m_workload_label->setText(
        QString("Workload: %1").arg(m.workload_type.isEmpty() ? "idle" : m.workload_type));
}

/* =========================================================================
 * Dashboard
 * ====================================================================== */

Dashboard::Dashboard(QWidget *parent)
    : QMainWindow(parent)
    , m_refresh_interval_ms(1000)
{
    setWindowTitle("MVGAL — Multi-Vendor GPU Aggregation Layer");
    resize(900, 600);

    setupUi();

    m_refresh_timer = new QTimer(this);
    connect(m_refresh_timer, &QTimer::timeout, this, &Dashboard::refresh);
    m_refresh_timer->start(m_refresh_interval_ms);

    refresh(); /* immediate first update */
}

Dashboard::~Dashboard() = default;

void Dashboard::setupUi()
{
    m_tabs = new QTabWidget(this);
    setCentralWidget(m_tabs);

    m_overview_tab   = new QWidget;
    m_scheduler_tab  = new QWidget;
    m_logs_tab       = new QWidget;
    m_config_tab     = new QWidget;

    m_tabs->addTab(m_overview_tab,  "Overview");
    m_tabs->addTab(m_scheduler_tab, "Scheduler");
    m_tabs->addTab(m_logs_tab,      "Logs");
    m_tabs->addTab(m_config_tab,    "Config");

    setupOverviewTab(m_overview_tab);
    setupSchedulerTab(m_scheduler_tab);
    setupLogsTab(m_logs_tab);
    setupConfigTab(m_config_tab);

    statusBar()->showMessage("MVGAL Dashboard ready");
}

void Dashboard::setupOverviewTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);

    m_aggregate_label = new QLabel("Aggregate: — GPUs", tab);
    m_aggregate_label->setStyleSheet("font-weight: bold; font-size: 14px;");
    layout->addWidget(m_aggregate_label);

    auto *scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    auto *container = new QWidget;
    auto *gpu_layout = new QHBoxLayout(container);
    gpu_layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    scroll->setWidget(container);
    layout->addWidget(scroll);

    /* Pre-create up to 8 GPU widgets; hide unused ones */
    for (int i = 0; i < 8; i++) {
        auto *w = new GpuWidget(i, container);
        w->setVisible(false);
        gpu_layout->addWidget(w);
        m_gpu_widgets.append(w);
    }
}

void Dashboard::setupSchedulerTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);

    auto *mode_group = new QGroupBox("Scheduling Mode", tab);
    auto *mode_layout = new QHBoxLayout(mode_group);
    m_scheduler_combo = new QComboBox(mode_group);
    m_scheduler_combo->addItems({
        "static",
        "dynamic",
        "work-stealing",
        "afr",
        "sfr",
        "hybrid",
        "single"
    });
    mode_layout->addWidget(new QLabel("Mode:", mode_group));
    mode_layout->addWidget(m_scheduler_combo);
    layout->addWidget(mode_group);

    auto *idle_group = new QGroupBox("Idle Optimisation", tab);
    auto *idle_layout = new QHBoxLayout(idle_group);
    m_idle_timeout_spin = new QSpinBox(idle_group);
    m_idle_timeout_spin->setRange(1, 60);
    m_idle_timeout_spin->setValue(5);
    m_idle_timeout_spin->setSuffix(" s");
    idle_layout->addWidget(new QLabel("Idle timeout:", idle_group));
    idle_layout->addWidget(m_idle_timeout_spin);
    layout->addWidget(idle_group);

    m_apply_btn = new QPushButton("Apply", tab);
    connect(m_apply_btn, &QPushButton::clicked, this, &Dashboard::onApplyConfig);
    layout->addWidget(m_apply_btn);

    layout->addStretch();

    connect(m_scheduler_combo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Dashboard::onSchedulerModeChanged);
}

void Dashboard::setupLogsTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    m_log_view = new QTextEdit(tab);
    m_log_view->setReadOnly(true);
    m_log_view->setFontFamily("monospace");
    layout->addWidget(m_log_view);
}

void Dashboard::setupConfigTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->addWidget(new QLabel(
        "Edit /etc/mvgal/mvgal.conf and click Reload to apply changes.", tab));
    auto *reload_btn = new QPushButton("Reload Configuration", tab);
    connect(reload_btn, &QPushButton::clicked, [this]() {
        QProcess::startDetached("mvgal-config", {"reload"});
        statusBar()->showMessage("Configuration reload requested");
    });
    layout->addWidget(reload_btn);
    layout->addStretch();
}

/* -------------------------------------------------------------------------
 * Metrics fetching (sysfs-based fallback)
 * ---------------------------------------------------------------------- */

QVector<GpuMetrics> Dashboard::fetchMetrics()
{
    QVector<GpuMetrics> result;

    /* Try REST API first */
    /* (In a full implementation, use QNetworkAccessManager to GET
     *  http://localhost:7474/api/v1/gpus and parse JSON.)
     * For now, fall back to sysfs. */

    QDir drm("/sys/class/drm");
    QStringList entries = drm.entryList(QStringList() << "card*", QDir::Dirs);
    int idx = 0;

    for (const QString &entry : entries) {
        /* Skip cardN-* connector entries */
        QString num = entry.mid(4);
        bool ok;
        num.toInt(&ok);
        if (!ok) continue;

        QString dev_path = QString("/sys/class/drm/%1/device").arg(entry);
        QFile vendor_file(dev_path + "/vendor");
        if (!vendor_file.open(QIODevice::ReadOnly)) continue;
        QString vendor_str = vendor_file.readAll().trimmed();
        vendor_file.close();

        bool vid_ok;
        uint16_t vid = (uint16_t)vendor_str.toUInt(&vid_ok, 16);
        if (!vid_ok) continue;
        if (vid != 0x1002 && vid != 0x10DE && vid != 0x8086 && vid != 0x1A82) continue;

        GpuMetrics m;
        m.index = idx++;
        m.vendor = (vid == 0x1002) ? "AMD" :
                   (vid == 0x10DE) ? "NVIDIA" :
                   (vid == 0x8086) ? "Intel" : "MooreThreads";
        m.name = QString("%1 GPU").arg(m.vendor);

        /* Utilization */
        QFile util_file(dev_path + "/gpu_busy_percent");
        if (util_file.open(QIODevice::ReadOnly)) {
            m.utilization = util_file.readAll().trimmed().toInt();
            util_file.close();
        } else {
            m.utilization = 0;
        }

        /* VRAM */
        QFile vram_total_file(dev_path + "/mem_info_vram_total");
        QFile vram_used_file(dev_path + "/mem_info_vram_used");
        if (vram_total_file.open(QIODevice::ReadOnly)) {
            m.vram_total_gb = vram_total_file.readAll().trimmed().toLongLong()
                              / (1024.0 * 1024.0 * 1024.0);
            vram_total_file.close();
        }
        if (vram_used_file.open(QIODevice::ReadOnly)) {
            m.vram_used_gb = vram_used_file.readAll().trimmed().toLongLong()
                             / (1024.0 * 1024.0 * 1024.0);
            vram_used_file.close();
        }

        /* Temperature via hwmon */
        QDir hwmon_dir(dev_path + "/hwmon");
        QStringList hwmon_entries = hwmon_dir.entryList(QStringList() << "hwmon*", QDir::Dirs);
        if (!hwmon_entries.isEmpty()) {
            QString hwmon_path = dev_path + "/hwmon/" + hwmon_entries.first();
            QFile temp_file(hwmon_path + "/temp1_input");
            if (temp_file.open(QIODevice::ReadOnly)) {
                m.temperature_c = temp_file.readAll().trimmed().toInt() / 1000;
                temp_file.close();
            }
            QFile power_file(hwmon_path + "/power1_average");
            if (power_file.open(QIODevice::ReadOnly)) {
                m.power_w = power_file.readAll().trimmed().toLongLong() / 1000000.0;
                power_file.close();
            }
        }

        m.clock_mhz = 0;
        m.workload_type = "";
        result.append(m);
    }

    return result;
}

QString Dashboard::fetchLogs()
{
    /* Try to read from journald or the daemon log file */
    QProcess proc;
    proc.start("journalctl", {"-u", "mvgald", "-n", "50", "--no-pager"});
    if (proc.waitForFinished(2000))
        return proc.readAllStandardOutput();

    QFile log_file("/var/log/mvgal/mvgald.log");
    if (log_file.open(QIODevice::ReadOnly))
        return log_file.readAll();

    return "(No log data available — start mvgald to see logs)";
}

/* -------------------------------------------------------------------------
 * Refresh slot
 * ---------------------------------------------------------------------- */

void Dashboard::refresh()
{
    QVector<GpuMetrics> metrics = fetchMetrics();

    /* Update aggregate label */
    m_aggregate_label->setText(
        QString("Aggregate: %1 GPU(s) detected").arg(metrics.size()));

    /* Update GPU widgets */
    for (int i = 0; i < m_gpu_widgets.size(); i++) {
        if (i < metrics.size()) {
            m_gpu_widgets[i]->setVisible(true);
            m_gpu_widgets[i]->update(metrics[i]);
        } else {
            m_gpu_widgets[i]->setVisible(false);
        }
    }

    /* Update logs if that tab is visible */
    if (m_tabs->currentWidget() == m_logs_tab) {
        QString logs = fetchLogs();
        if (m_log_view->toPlainText() != logs) {
            m_log_view->setPlainText(logs);
            m_log_view->moveCursor(QTextCursor::End);
        }
    }

    statusBar()->showMessage(
        QString("Last updated: %1  |  %2 GPU(s)")
            .arg(QTime::currentTime().toString("hh:mm:ss"))
            .arg(metrics.size()));
}

/* -------------------------------------------------------------------------
 * Config slots
 * ---------------------------------------------------------------------- */

void Dashboard::onSchedulerModeChanged(int /*index*/)
{
    /* Preview only; apply on button click */
}

void Dashboard::onIdleTimeoutChanged(int /*value*/)
{
    /* Preview only; apply on button click */
}

void Dashboard::onApplyConfig()
{
    QString mode = m_scheduler_combo->currentText();
    int idle_s   = m_idle_timeout_spin->value();

    QProcess::startDetached("mvgal-config",
        {"set-strategy", mode});
    QProcess::startDetached("mvgal-config",
        {"set-idle-timeout", QString::number(idle_s)});

    statusBar()->showMessage(
        QString("Applied: strategy=%1, idle_timeout=%2s").arg(mode).arg(idle_s));
}

} // namespace mvgal

/* =========================================================================
 * main
 * ====================================================================== */

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MVGAL Dashboard");
    app.setApplicationVersion("0.2.1");
    app.setOrganizationName("MVGAL Project");

    mvgal::Dashboard dashboard;
    dashboard.show();

    return app.exec();
}
