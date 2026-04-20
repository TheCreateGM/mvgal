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

#include "../src/userspace/core/mvgal.h"

static GtkStatusIcon *tray_icon = NULL;
static GtkWidget *menu = NULL;
static GtkWidget *menu_item_strategy = NULL;
static GtkWidget *menu_item_gpus = NULL;
static GtkWidget *menu_item_stats = NULL;
static GtkWidget *menu_item_preferences = NULL;
static GtkWidget *menu_item_quit = NULL;

static bool running = true;

static void update_menu(void) {
    char buf[256];
    
    // Update strategy
    mvgal_strategy_t strategy = mvgal_get_strategy();
    const char *strategy_name = "Unknown";
    switch (strategy) {
        case MVGAL_STRATEGY_ROUND_ROBIN: strategy_name = "Round Robin"; break;
        case MVGAL_STRATEGY_AFR: strategy_name = "AFR"; break;
        case MVGAL_STRATEGY_SFR: strategy_name = "SFR"; break;
        case MVGAL_STRATEGY_SINGLE: strategy_name = "Single GPU"; break;
        case MVGAL_STRATEGY_HYBRID: strategy_name = "Hybrid"; break;
        case MVGAL_STRATEGY_CUSTOM: strategy_name = "Custom"; break;
    }
    snprintf(buf, sizeof(buf), "Strategy: %s", strategy_name);
    gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item_strategy), buf);
    
    // Update GPU count
    int gpu_count = mvgal_get_gpu_count();
    snprintf(buf, sizeof(buf), "GPUs: %d", gpu_count);
    gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item_gpus), buf);
    
    // Update stats
    mvgal_stats_t stats;
    if (mvgal_get_stats(&stats) == 0) {
        snprintf(buf, sizeof(buf), "Workloads: %lu, Balance: %.1f%%",
                 (unsigned long)stats.total_workloads, stats.load_balance);
        gtk_menu_item_set_label(GTK_MENU_ITEM(menu_item_stats), buf);
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

static void on_menu_strategy_round_robin(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    mvgal_set_strategy(MVGAL_STRATEGY_ROUND_ROBIN);
    update_menu();
}

static void on_menu_strategy_afr(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    mvgal_set_strategy(MVGAL_STRATEGY_AFR);
    update_menu();
}

static void on_menu_strategy_sfr(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    mvgal_set_strategy(MVGAL_STRATEGY_SFR);
    update_menu();
}

static void on_menu_strategy_single(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    mvgal_set_strategy(MVGAL_STRATEGY_SINGLE);
    update_menu();
}

static void on_menu_strategy_hybrid(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    mvgal_set_strategy(MVGAL_STRATEGY_HYBRID);
    update_menu();
}

static void on_menu_quit(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    (void)user_data;
    running = false;
    gtk_main_quit();
}

static gboolean on_timeout(gpointer user_data) {
    (void)user_data;
    update_menu();
    return TRUE; // Continue calling
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    
    // Initialize MVGAL
    if (mvgal_init(NULL) != 0) {
        fprintf(stderr, "Failed to initialize MVGAL\n");
        return 1;
    }
    
    // Create tray icon
    tray_icon = gtk_status_icon_new_from_icon_name("video-x-generic");
    if (!tray_icon) {
        // Fallback icon
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file("/usr/share/icons/hicolor/256x256/apps/mvgal.svg", NULL);
        if (pixbuf) {
            tray_icon = gtk_status_icon_new_from_pixbuf(pixbuf);
            g_object_unref(pixbuf);
        }
    }
    
    if (!tray_icon) {
        fprintf(stderr, "Failed to create tray icon\n");
        mvgal_shutdown();
        return 1;
    }
    
    gtk_status_icon_set_tooltip_text(tray_icon, "MVGAL - Multi-Vendor GPU Aggregation Layer");
    g_signal_connect(tray_icon, "activate", G_CALLBACK(on_activate), NULL);
    
    // Create menu
    menu = gtk_menu_new();
    
    // Strategy submenu
    GtkWidget *strategy_menu = gtk_menu_new();
    
    GtkWidget *item = gtk_menu_item_new_with_label("Round Robin");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_round_robin), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    
    item = gtk_menu_item_new_with_label("AFR");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_afr), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    
    item = gtk_menu_item_new_with_label("SFR");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_sfr), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    
    item = gtk_menu_item_new_with_label("Single GPU");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_single), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    
    item = gtk_menu_item_new_with_label("Hybrid");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_strategy_hybrid), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(strategy_menu), item);
    
    menu_item_strategy = gtk_menu_item_new_with_label("Strategy: Unknown");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item_strategy), strategy_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_strategy);
    
    // GPUs info
    menu_item_gpus = gtk_menu_item_new_with_label("GPUs: 0");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_gpus);
    
    // Stats info
    menu_item_stats = gtk_menu_item_new_with_label("Workloads: 0, Balance: 0.0%");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_stats);
    
    // Separator
    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    
    // Preferences
    item = gtk_menu_item_new_with_label("Preferences...");
    g_signal_connect(item, "activate", G_CALLBACK(on_menu_preferences), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    
    // Separator
    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    
    // Quit
    menu_item_quit = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(menu_item_quit, "activate", G_CALLBACK(on_menu_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item_quit);
    
    gtk_widget_show_all(menu);
    
    // Initial update
    update_menu();
    
    // Setup timeout for periodic updates
    g_timeout_add_seconds(1, on_timeout, NULL);
    
    // Main loop
    gtk_main();
    
    // Cleanup
    mvgal_shutdown();
    if (tray_icon) {
        g_object_unref(tray_icon);
    }
    
    return 0;
}
