/**
 * MVGAL Qt Monitoring Dashboard
 * SPDX-License-Identifier: MIT
 */

#ifndef MVGAL_DASHBOARD_H
#define MVGAL_DASHBOARD_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QProgressBar>
#include <QComboBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVector>
#include <QString>

namespace mvgal {

/* -------------------------------------------------------------------------
 * GPU metrics data structure
 * ---------------------------------------------------------------------- */

struct GpuMetrics {
    int     index;
    QString name;
    QString vendor;
    int     utilization;    /* 0–100 % */
    double  vram_used_gb;
    double  vram_total_gb;
    int     temperature_c;
    double  power_w;
    int     clock_mhz;
    QString workload_type;
};

/* -------------------------------------------------------------------------
 * Per-GPU widget
 * ---------------------------------------------------------------------- */

class GpuWidget : public QGroupBox {
    Q_OBJECT
public:
    explicit GpuWidget(int gpu_index, QWidget *parent = nullptr);
    void update(const GpuMetrics &m);

private:
    QLabel       *m_name_label;
    QProgressBar *m_util_bar;
    QProgressBar *m_vram_bar;
    QLabel       *m_temp_label;
    QLabel       *m_power_label;
    QLabel       *m_clock_label;
    QLabel       *m_workload_label;
};

/* -------------------------------------------------------------------------
 * Main dashboard window
 * ---------------------------------------------------------------------- */

class Dashboard : public QMainWindow {
    Q_OBJECT
public:
    explicit Dashboard(QWidget *parent = nullptr);
    ~Dashboard() override;

private slots:
    void refresh();
    void onSchedulerModeChanged(int index);
    void onIdleTimeoutChanged(int value);
    void onApplyConfig();

private:
    void setupUi();
    void setupOverviewTab(QWidget *tab);
    void setupSchedulerTab(QWidget *tab);
    void setupLogsTab(QWidget *tab);
    void setupConfigTab(QWidget *tab);

    QVector<GpuMetrics> fetchMetrics();
    QString             fetchLogs();
    void                applySchedulerMode(const QString &mode);

    QTabWidget          *m_tabs;
    QWidget             *m_overview_tab;
    QWidget             *m_scheduler_tab;
    QWidget             *m_logs_tab;
    QWidget             *m_config_tab;

    QVector<GpuWidget *> m_gpu_widgets;
    QLabel              *m_aggregate_label;
    QComboBox           *m_scheduler_combo;
    QSpinBox            *m_idle_timeout_spin;
    QTextEdit           *m_log_view;
    QPushButton         *m_apply_btn;

    QTimer              *m_refresh_timer;
    int                  m_refresh_interval_ms;
};

} // namespace mvgal

#endif // MVGAL_DASHBOARD_H
