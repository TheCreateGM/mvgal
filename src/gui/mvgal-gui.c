/**
 * @file mvgal-gui.c
 * @brief MVGAL GUI Configuration Tool (GTK-based)
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * Graphical configuration tool
 */

#ifdef HAVE_GTK3
#include <gtk/gtk.h>
#else
#include <gtk-3.0/gtk/gtk.h>
#endif
#include <stdbool.h>

#include "mvgal/mvgal.h"
#include "mvgal/mvgal_gpu.h"

// Main window
static GtkWidget *window = NULL;

// GPU List store
static GtkListStore *gpu_store = NULL;
static GtkTreeView *gpu_treeview = NULL;

// Strategy combo box
static GtkWidget *strategy_combo = NULL;

// Enable/disable check buttons
static GtkWidget *check_cuda = NULL;
static GtkWidget *check_d3d = NULL;
static GtkWidget *check_webgpu = NULL;
static GtkWidget *check_opencl = NULL;
static GtkWidget *check_memory_migration = NULL;
static GtkWidget *check_dmabuf = NULL;

// Stats labels
static GtkWidget *label_total_workloads = NULL;
static GtkWidget *label_load_balance = NULL;
static GtkWidget *label_memory_used = NULL;

// Global context
static mvgal_context_t g_context = NULL;

static void on_set_strategy(GtkWidget *widget, gpointer user_data);

static const char *strategy_name(mvgal_distribution_strategy_t strategy) {
    switch (strategy) {
        case MVGAL_STRATEGY_ROUND_ROBIN: return "Round Robin";
        case MVGAL_STRATEGY_AFR: return "AFR";
        case MVGAL_STRATEGY_SFR: return "SFR";
        case MVGAL_STRATEGY_SINGLE_GPU: return "Single GPU";
        case MVGAL_STRATEGY_HYBRID: return "Hybrid";
        case MVGAL_STRATEGY_TASK: return "Task";
        case MVGAL_STRATEGY_COMPUTE_OFFLOAD: return "Compute Offload";
        case MVGAL_STRATEGY_AUTO: return "Auto";
        case MVGAL_STRATEGY_CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

static const char *vendor_name(mvgal_vendor_t vendor) {
    switch (vendor) {
        case MVGAL_VENDOR_AMD: return "AMD";
        case MVGAL_VENDOR_NVIDIA: return "NVIDIA";
        case MVGAL_VENDOR_INTEL: return "Intel";
        case MVGAL_VENDOR_MOORE_THREADS: return "Moore Threads";
        default: return "Unknown";
    }
}

static void refresh_gpu_list(void) {
    GtkTreeIter iter;
    
    gtk_list_store_clear(gpu_store);
    
    int32_t gpu_count = mvgal_gpu_get_count();
    for (int32_t i = 0; i < gpu_count; i++) {
        mvgal_gpu_descriptor_t desc;
        if (mvgal_gpu_get_descriptor(i, &desc) == MVGAL_SUCCESS) {
            gtk_list_store_append(gpu_store, &iter);
            gtk_list_store_set(gpu_store, &iter,
                               0, desc.id,
                               1, desc.name,
                               2, vendor_name(desc.vendor),
                               3, (int)i,  // Use index as priority for now
                               4, desc.enabled ? "Yes" : "No",
                               5, (double)desc.vram_used / (1024 * 1024),
                               -1);
        }
    }
}

static void refresh_stats(void) {
    if (g_context == NULL) {
        mvgal_context_create(&g_context);
    }
    
    mvgal_stats_t stats;
    if (mvgal_get_stats(g_context, &stats) == MVGAL_SUCCESS) {
        char buf[256];
        
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats.frames_submitted);
        gtk_label_set_text(GTK_LABEL(label_total_workloads), buf);
        
        // Simplified load balance display
        float balance = 100.0f;
        snprintf(buf, sizeof(buf), "%.2f%%", balance);
        gtk_label_set_text(GTK_LABEL(label_load_balance), buf);
        
        snprintf(buf, sizeof(buf), "%.2f MB", (double)stats.bytes_transferred / (1024 * 1024));
        gtk_label_set_text(GTK_LABEL(label_memory_used), buf);
    }
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "MVGAL Configuration");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    
    // Main container
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    // Notebook
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(main_box), notebook, TRUE, TRUE, 0);
    
    // --- Overview Page ---
    GtkWidget *overview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    // GPU List
    GtkWidget *gpu_frame = gtk_frame_new("Detected GPUs");
    gpu_store = gtk_list_store_new(6, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_DOUBLE);
    
    gpu_treeview = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(gpu_store)));
    g_object_unref(gpu_store);
    
    // Add columns
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    
    // ID column
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("ID", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(gpu_treeview, column);
    
    // Name column
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(gpu_treeview, column);
    
    // Vendor column
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Vendor", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(gpu_treeview, column);
    
    // Priority column
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Priority", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(gpu_treeview, column);
    
    // Enabled column
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Enabled", renderer, "text", 4, NULL);
    gtk_tree_view_append_column(gpu_treeview, column);
    
    // Memory Used column
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Memory (MB)", renderer, "text", 5, NULL);
    gtk_tree_view_append_column(gpu_treeview, column);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(gpu_treeview));
    gtk_container_add(GTK_CONTAINER(gpu_frame), scrolled);
    gtk_box_pack_start(GTK_BOX(overview_box), gpu_frame, TRUE, TRUE, 0);
    
    // Refresh button
    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh GPU List");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(refresh_gpu_list), NULL);
    gtk_box_pack_start(GTK_BOX(overview_box), refresh_btn, FALSE, FALSE, 0);
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), overview_box, gtk_label_new("Overview"));
    
    // --- Strategy Page ---
    GtkWidget *strategy_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    GtkWidget *strategy_frame = gtk_frame_new("Workload Distribution Strategy");
    GtkWidget *strategy_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(strategy_frame), strategy_vbox);
    
    GtkWidget *label = gtk_label_new("Select distribution strategy:");
    gtk_box_pack_start(GTK_BOX(strategy_vbox), label, FALSE, FALSE, 0);
    
    strategy_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "round_robin");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "afr");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "sfr");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "single_gpu");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "hybrid");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "task");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "compute_offload");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "auto");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(strategy_combo), "custom");
    gtk_combo_box_set_active(GTK_COMBO_BOX(strategy_combo), 0);
    gtk_box_pack_start(GTK_BOX(strategy_vbox), strategy_combo, FALSE, FALSE, 0);
    
    GtkWidget *set_strategy_btn = gtk_button_new_with_label("Set Strategy");
    g_signal_connect(set_strategy_btn, "clicked", G_CALLBACK(on_set_strategy), NULL);
    gtk_box_pack_start(GTK_BOX(strategy_vbox), set_strategy_btn, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(strategy_box), strategy_frame, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), strategy_box, gtk_label_new("Strategy"));
    
    // --- Features Page ---
    GtkWidget *features_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    GtkWidget *features_frame = gtk_frame_new("Enabled Features");
    GtkWidget *features_grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(features_frame), features_grid);
    
    int row = 0;
    
    check_cuda = gtk_check_button_new_with_label("CUDA API Interception");
    gtk_grid_attach(GTK_GRID(features_grid), check_cuda, 0, row, 2, 1);
    row++;
    
    check_d3d = gtk_check_button_new_with_label("Direct3D API Interception");
    gtk_grid_attach(GTK_GRID(features_grid), check_d3d, 0, row, 2, 1);
    row++;
    
    check_webgpu = gtk_check_button_new_with_label("WebGPU API Interception");
    gtk_grid_attach(GTK_GRID(features_grid), check_webgpu, 0, row, 2, 1);
    row++;
    
    check_opencl = gtk_check_button_new_with_label("OpenCL API Interception");
    gtk_grid_attach(GTK_GRID(features_grid), check_opencl, 0, row, 2, 1);
    row++;
    
    check_memory_migration = gtk_check_button_new_with_label("Cross-GPU Memory Migration");
    gtk_grid_attach(GTK_GRID(features_grid), check_memory_migration, 0, row, 2, 1);
    row++;
    
    check_dmabuf = gtk_check_button_new_with_label("DMA-BUF Support");
    gtk_grid_attach(GTK_GRID(features_grid), check_dmabuf, 0, row, 2, 1);
    row++;
    
    gtk_box_pack_start(GTK_BOX(features_box), features_frame, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), features_box, gtk_label_new("Features"));
    
    // --- Statistics Page ---
    GtkWidget *stats_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    
    GtkWidget *stats_frame = gtk_frame_new("Runtime Statistics");
    GtkWidget *stats_grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(stats_frame), stats_grid);
    
    row = 0;
    
    label = gtk_label_new("Total Workloads:");
    gtk_grid_attach(GTK_GRID(stats_grid), label, 0, row, 1, 1);
    label_total_workloads = gtk_label_new("0");
    gtk_grid_attach(GTK_GRID(stats_grid), label_total_workloads, 1, row, 1, 1);
    row++;
    
    label = gtk_label_new("Load Balance:");
    gtk_grid_attach(GTK_GRID(stats_grid), label, 0, row, 1, 1);
    label_load_balance = gtk_label_new("0.00%");
    gtk_grid_attach(GTK_GRID(stats_grid), label_load_balance, 1, row, 1, 1);
    row++;
    
    label = gtk_label_new("Memory Transferred:");
    gtk_grid_attach(GTK_GRID(stats_grid), label, 0, row, 1, 1);
    label_memory_used = gtk_label_new("0 MB");
    gtk_grid_attach(GTK_GRID(stats_grid), label_memory_used, 1, row, 1, 1);
    row++;
    
    GtkWidget *refresh_stats_btn = gtk_button_new_with_label("Refresh Statistics");
    g_signal_connect(refresh_stats_btn, "clicked", G_CALLBACK(refresh_stats), NULL);
    gtk_grid_attach(GTK_GRID(stats_grid), refresh_stats_btn, 0, row, 2, 1);
    row++;
    
    gtk_box_pack_start(GTK_BOX(stats_box), stats_frame, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), stats_box, gtk_label_new("Statistics"));
    
    // Show all
    gtk_widget_show_all(window);
    
    // Initialize MVGAL
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "Failed to initialize MVGAL: %d", err);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    
    // Create context
    mvgal_context_create(&g_context);
    
    // Refresh data
    refresh_gpu_list();
    refresh_stats();
}

static void on_set_strategy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    
    const char *strategy_text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(strategy_combo));
    mvgal_distribution_strategy_t strategy = MVGAL_STRATEGY_HYBRID;
    
    if (g_strcmp0(strategy_text, "round_robin") == 0) {
        strategy = MVGAL_STRATEGY_ROUND_ROBIN;
    } else if (g_strcmp0(strategy_text, "afr") == 0) {
        strategy = MVGAL_STRATEGY_AFR;
    } else if (g_strcmp0(strategy_text, "sfr") == 0) {
        strategy = MVGAL_STRATEGY_SFR;
    } else if (g_strcmp0(strategy_text, "single_gpu") == 0) {
        strategy = MVGAL_STRATEGY_SINGLE_GPU;
    } else if (g_strcmp0(strategy_text, "hybrid") == 0) {
        strategy = MVGAL_STRATEGY_HYBRID;
    } else if (g_strcmp0(strategy_text, "task") == 0) {
        strategy = MVGAL_STRATEGY_TASK;
    } else if (g_strcmp0(strategy_text, "compute_offload") == 0) {
        strategy = MVGAL_STRATEGY_COMPUTE_OFFLOAD;
    } else if (g_strcmp0(strategy_text, "auto") == 0) {
        strategy = MVGAL_STRATEGY_AUTO;
    } else if (g_strcmp0(strategy_text, "custom") == 0) {
        strategy = MVGAL_STRATEGY_CUSTOM;
    }
    
    if (g_context != NULL) {
        mvgal_set_strategy(g_context, strategy);
    }
}

static void on_shutdown(GtkApplication *app, gpointer user_data) {
    (void)app;
    (void)user_data;
    
    if (g_context != NULL) {
        mvgal_context_destroy(g_context);
        g_context = NULL;
    }
    mvgal_shutdown();
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;
    
    app = gtk_application_new("org.mvgal.MVGAL-GUI", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
    
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return status;
}
