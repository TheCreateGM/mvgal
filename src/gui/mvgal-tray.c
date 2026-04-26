/**
 * @file mvgal-tray.c
 * @brief MVGAL System Tray Icon
 *
 * Multi-Vendor GPU Aggregation Layer for Linux
 * System tray icon for monitoring and control
 */

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <stdio.h>
#include <stdbool.h>

#include "mvgal/mvgal.h"
#include "mvgal/mvgal_gpu.h"

static GtkStatusIcon *tray_icon = NULL;
static GtkWidget *menu = NULL;
static GtkWidget *menu_item_strategy = NULL;
static GtkWidget *menu_item_gpus = NULL;
static GtkWidget *menu_item_stats = NULL;
static GtkWidget *menu_item_preferences = NULL;
static GtkWidget *menu_item_quit = NULL;

static bool running = true;
static mvgal_context_t g_context = NULL;

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

static void update_menu(void) {
    char buf[256];
    
    // Update strategy
    mvgal_distribution_strategy_t strategy = MVGAL_STRATEGY_HYBRID;
    if (g_context != NULL) {
        strategy = mvgal_get_strategy(g_context);
    }
    snprintf(buf, sizeof(buf), "Strategy: %s", strategy_name(strategy));
    gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item_strategy), buf);
    
    // Update GPU count
    int32_t gpu_count = mvgal_gpu_get_count();
    snprintf(buf, sizeof(buf), "GPUs: %d", gpu_count);
    gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item_gpus), buf);
    
    // Update stats
    if (g_context != NULL) {
        mvgal_stats_t stats;
        if (mvgal_get_stats(g_context, &stats) == MVGAL_SUCCESS) {
            snprintf(buf, sizeof(buf), "Workloads: %lu",
                     (unsigned long)stats.frames_submitted);
            gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item_stats), buf);
        }
    }
}

static void on_activate(GtkStatusIcon *icon, gpointer user_data) {
    (void)icon;
    (void)user_data;
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(icon),
                            GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST,
                            NULL);
}

static void on_menu_preferences(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    // Launch preferences dialog
    system("mvgal-gui &");
}

static void set_strategy(mvgal_distribution_strategy_t strategy) {
    if (g_context != NULL) {
        mvgal_set_strategy(g_context, strategy);
    }
    update_menu();
}

static void on_menu_strategy_round_robin(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    set_strategy(MVGAL_STRATEGY_ROUND_ROBIN);
}

static void on_menu_strategy_afr(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    set_strategy(MVGAL_STRATEGY_AFR);
}

static void on_menu_strategy_sfr(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    set_strategy(MVGAL_STRATEGY_SFR);
}

static void on_menu_strategy_single(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    set_strategy(MVGAL_STRATEGY_SINGLE_GPU);
}

static void on_menu_strategy_hybrid(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    set_strategy(MVGAL_STRATEGY_HYBRID);
}

static void on_menu_quit(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    running = false;
    
    if (g_context != NULL) {
        mvgal_context_destroy(g_context);
        g_context = NULL;
    }
    mvgal_shutdown();
    
    gtk_main_quit();
}

static gboolean on_timeout(gpointer user_data) {
    (void)user_data;
    update_menu();
    return TRUE; // Continue calling
}

static void create_menu(void) {
    menu = gtk_menu_new();
    
    // Strategy submenu
    GtkWidget *strategy_menu = gtk_menu_new();
    
    GtkWidget *item;
    
    item = gtk_menu_item_new_with_label("Strategy");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), strategy_menu);
    menu_item_strategy = item;
    
    item = gtk_menu_item_new_with_label("Round Robin");
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_round_robin), NULL);
    
    item = gtk_menu_item_new_with_label("AFR");
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_afr), NULL);
    
    item = gtk_menu_item_new_with_label("SFR");
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_sfr), NULL);
    
    item = gtk_menu_item_new_with_label("Single GPU");
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_single), NULL);
    
    item = gtk_menu_item_new_with_label("Hybrid");
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_hybrid), NULL);
    
    // GPUs
    item = gtk_menu_item_new_with_label("GPUs: 0");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    menu_item_gpus = item;
    
    // Stats
    item = gtk_menu_item_new_with_label("Stats: 0 workloads");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    menu_item_stats = item;
    
    // Preferences
    item = gtk_menu_item_new_with_label("Preferences");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_preferences), NULL);
    menu_item_preferences = item;
    
    // Separator
    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    
    // Quit
    item = gtk_menu_item_new_with_label("Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_quit), NULL);
    menu_item_quit = item;
    
    gtk_widget_show_all(menu);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    
    // Initialize MVGAL
    mvgal_error_t err = mvgal_init(0);
    if (err != MVGAL_SUCCESS) {
        fprintf(stderr, "Failed to initialize MVGAL: %d\n", err);
        return 1;
    }
    
    // Create context
    mvgal_context_create(&g_context);
    
    // Create tray icon
    tray_icon = gtk_status_icon_new_from_icon_name("video-x-generic");
    if (tray_icon == NULL) {
        // Fallback icon
        tray_icon = gtk_status_icon_new_from_stock(GTK_STOCK_HARD_DISK);
    }
    
    gtk_status_icon_set_tooltip_text(tray_icon, "MVGAL - Multi-Vendor GPU Aggregation Layer");
    
    // Create menu
    create_menu();
    
    // Connect signals
    g_signal_connect(tray_icon, "activate", G_CALLBACK(on_activate), NULL);
    
    // Setup timeout for periodic updates
    g_timeout_add(5000, on_timeout, NULL); // Update every 5 seconds
    
    // Initial update
    update_menu();
    
    // Main loop
    gtk_main();
    
    // Cleanup
    if (tray_icon != NULL) {
        g_object_unref(tray_icon);
    }
    
    if (g_context != NULL) {
        mvgal_context_destroy(g_context);
        g_context = NULL;
    }
    
    mvgal_shutdown();
    
    return 0;
}
