/**
 * mvgal-steam-setup — Configure Steam launch options for MVGAL
 *
 * Helps users set up MVGAL environment variables in Steam launch
 * options for optimal multi-GPU gaming performance.
 *
 * Usage:
 *   mvgal-steam-setup [--add] [--remove] [--list] [app_id]
 *   mvgal-steam-setup --add "Cyberpunk 2077" --strategy afr
 *   mvgal-steam-setup --list
 *   mvgal-steam-setup --auto  # detect and configure all supported games
 *   mvgal-steam-setup --check  # verify current Steam MVGAL setup
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <errno.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

#define STEAM_CONFIG_PATH  ".steam/steam/steamapps"
#define STEAM_LIBRARY_FILE "libraryfolders.vdf"
#define STEAM_APP_MANIFEST "appmanifest_%u.acf"
#define STEAM_COMPAT_TOOL_DIR ".steam/steam/compatibilitytools.d"

#define MAX_LINE 4096
#define MAX_GAMES 512

/* ============================================================================
 * Data Types
 * ============================================================================ */

typedef struct {
    unsigned int app_id;
    char         name[256];
    char         install_dir[512];
    char         compat_tool[64];
    char         launch_options[2048];
    bool         has_mvgal;
} steam_game_t;

typedef struct {
    const char *name_pattern;
    const char *recommended_strategy;
    const char *notes;
    const char *extra_env;
} game_profile_t;

/* Known game profiles */
static const game_profile_t g_game_profiles[] = {
    { "cyberpunk 2077",   "afr",    "Ray tracing may not scale; disable for best perf",     "MVGAL_FRAME_PACING=1" },
    { "doom",             "afr",    "Vulkan renderer; AFR works well",                       NULL },
    { "doom eternal",     "afr",    "Vulkan renderer; AFR tested",                           NULL },
    { "quake",            "afr",    "Vulkan renderer",                                       NULL },
    { "dota 2",           "sfr",    "SFR recommended for UI-heavy scenes",                   NULL },
    { "cs2",              "afr",    "Native Vulkan; AFR tested",                             "MVGAL_FRAME_PACING=1" },
    { "csgo",             "afr",    "Vulkan via DXVK",                                       NULL },
    { "elden ring",       "single", "Frame pacing sensitive; single GPU recommended",        "MVGAL_FRAME_PACING=1" },
    { "red dead",         "sfr",    "Memory-intensive; SFR recommended",                     NULL },
    { "witcher 3",        "hybrid", "Hybrid works well",                                     NULL },
    { "horizon",          "sfr",    "SFR for DX12 via VKD3D",                                NULL },
    { "control",          "hybrid", "RTX-heavy; hybrid adapts well",                         NULL },
    { "minecraft",        "single", "OpenGL via Zink; single GPU fallback",                  NULL },
    { NULL, NULL, NULL, NULL }
};

/* ============================================================================
 * Helpers
 * ============================================================================ */

static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/root";
    }
    return home;
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *build_path(const char *base, const char *suffix) {
    size_t len = strlen(base) + strlen(suffix) + 2;
    char *path = malloc(len);
    if (path) snprintf(path, len, "%s/%s", base, suffix);
    return path;
}

static void str_lower(char *dst, const char *src, size_t max) {
    size_t i;
    for (i = 0; src[i] && i < max - 1; i++) {
        dst[i] = (char)(src[i] >= 'A' && src[i] <= 'Z' ? src[i] + 32 : src[i]);
    }
    dst[i] = '\0';
}

static bool str_contains_ic(const char *haystack, const char *needle) {
    char hay_lower[256], needle_lower[256];
    str_lower(hay_lower, haystack, sizeof(hay_lower));
    str_lower(needle_lower, needle, sizeof(needle_lower));
    return strstr(hay_lower, needle_lower) != NULL;
}

/* ============================================================================
 * Steam Library Detection
 * ============================================================================ */

static int find_steam_libraries(const char *home, char libraries[][512], int max) {
    int count = 0;

    /* Default library location */
    char default_path[1024];
    snprintf(default_path, sizeof(default_path), "%s/%s", home, STEAM_CONFIG_PATH);
    if (dir_exists(default_path) && count < max) {
        strncpy(libraries[count], default_path, 511);
        count++;
    }

    /* Parse libraryfolders.vdf for additional libraries */
    char vdf_path[1024];
    snprintf(vdf_path, sizeof(vdf_path), "%s/%s/%s", home,
             STEAM_CONFIG_PATH, STEAM_LIBRARY_FILE);

    if (file_exists(vdf_path)) {
        FILE *f = fopen(vdf_path, "r");
        if (f) {
            char line[MAX_LINE];
            while (fgets(line, sizeof(line), f) && count < max) {
                /* Match: "1" "/path/to/steamapps" */
                const char *q = strchr(line, '"');
                if (q) q = strchr(q + 1, '"');
                if (q) q = strchr(q + 1, '"');
                if (q) {
                    const char *end = strchr(q + 1, '"');
                    if (end) {
                        size_t len = (size_t)(end - q - 1);
                        if (len > 0 && len < 511) {
                            strncpy(libraries[count], q + 1, len);
                            libraries[count][len] = '\0';
                            /* Append /steamapps if not already there */
                            if (!strstr(libraries[count], "steamapps")) {
                                strncat(libraries[count], "/steamapps",
                                        511 - strlen(libraries[count]) - 1);
                            }
                            count++;
                        }
                    }
                }
            }
            fclose(f);
        }
    }

    return count;
}

/* ============================================================================
 * Game Discovery
 * ============================================================================ */

static int discover_games(const char *libraries[], int lib_count,
                          steam_game_t *games, int max_games)
{
    int game_count = 0;

    for (int l = 0; l < lib_count && game_count < max_games; l++) {
        DIR *dir = opendir(libraries[l]);
        if (!dir) continue;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && game_count < max_games) {
            unsigned int app_id;
            if (sscanf(entry->d_name, "appmanifest_%u.acf", &app_id) != 1)
                continue;

            char manifest_path[1024];
            snprintf(manifest_path, sizeof(manifest_path), "%s/%s",
                     libraries[l], entry->d_name);

            FILE *f = fopen(manifest_path, "r");
            if (!f) continue;

            steam_game_t *game = &games[game_count];
            memset(game, 0, sizeof(steam_game_t));
            game->app_id = app_id;

            char line[MAX_LINE];
            bool has_name = false, has_install = false, has_launch = false;

            while (fgets(line, sizeof(line), f)) {
                char value[512];

                if (sscanf(line, "\t\"name\"\t\t\"%[^\"]\"", value) == 1) {
                    strncpy(game->name, value, sizeof(game->name) - 1);
                    has_name = true;
                }
                if (sscanf(line, "\t\"installdir\"\t\t\"%[^\"]\"", value) == 1) {
                    strncpy(game->install_dir, value, sizeof(game->install_dir) - 1);
                    has_install = true;
                }
                if (sscanf(line, "\t\"LaunchOptions\"\t\t\"%[^\"]\"", value) == 1) {
                    strncpy(game->launch_options, value, sizeof(game->launch_options) - 1);
                    has_launch = true;
                }
                if (sscanf(line, "\t\"compattool\"\t\t\"%[^\"]\"", value) == 1) {
                    strncpy(game->compat_tool, value, sizeof(game->compat_tool) - 1);
                }
            }
            fclose(f);

            /* Check if MVGAL env is already present */
            if (strstr(game->launch_options, "ENABLE_MVGAL") ||
                strstr(game->launch_options, "MVGAL_STRATEGY")) {
                game->has_mvgal = true;
            }

            if (has_name && has_install) {
                game_count++;
            }
        }
        closedir(dir);
    }

    return game_count;
}

/* ============================================================================
 * Strategy Recommendation
 * ============================================================================ */

static const game_profile_t *find_profile(const char *game_name) {
    char lower[256];
    str_lower(lower, game_name, sizeof(lower));

    for (const game_profile_t *p = g_game_profiles; p->name_pattern; p++) {
        if (strstr(lower, p->name_pattern)) {
            return p;
        }
    }
    return NULL;
}

/* ============================================================================
 * Commands
 * ============================================================================ */

static int cmd_list_games(void) {
    const char *home = get_home_dir();
    char libraries_buf[16][512];
    const char *libraries[16];
    int lib_count = find_steam_libraries(home, libraries_buf, 16);
    for (int i = 0; i < lib_count; i++) libraries[i] = libraries_buf[i];

    steam_game_t games[MAX_GAMES];
    int count = discover_games(libraries, lib_count, games, MAX_GAMES);

    printf("Steam Games Detected: %d\n", count);
    printf("%-8s %-40s %-12s %s\n", "AppID", "Name", "MVGAL", "Strategy");
    printf("-------- %-40s %-12s %s\n", "----------------------------------------",
           "------------", "--------");

    for (int i = 0; i < count; i++) {
        const game_profile_t *profile = find_profile(games[i].name);
        const char *strategy = profile ? profile->recommended_strategy : "auto";
        printf("%-8u %-40s %-12s %s\n",
               games[i].app_id,
               games[i].name,
               games[i].has_mvgal ? "yes" : "no",
               strategy);
    }

    printf("\nHint: Use --add <app_id|name> to configure a game\n");
    printf("      Use --add --all to configure all supported games\n");
    return 0;
}

static int cmd_add_game(const char *target, const char *strategy,
                        bool frame_pacing, const char *gpu_mask)
{
    const char *home = get_home_dir();
    char libraries_buf[16][512];
    const char *libraries[16];
    int lib_count = find_steam_libraries(home, libraries_buf, 16);
    for (int i = 0; i < lib_count; i++) libraries[i] = libraries_buf[i];

    steam_game_t games[MAX_GAMES];
    int count = discover_games(libraries, lib_count, games, MAX_GAMES);

    unsigned int target_id = 0;
    if (target) {
        if (sscanf(target, "%u", &target_id) != 1)
            target_id = 0;
    }

    int found = 0;
    for (int i = 0; i < count; i++) {
        bool match = false;
        if (target_id > 0 && games[i].app_id == target_id)
            match = true;
        if (target_id == 0 && str_contains_ic(games[i].name, target))
            match = true;

        if (!match) continue;

        /* Determine strategy */
        const char *use_strategy = strategy;
        if (!use_strategy) {
            const game_profile_t *p = find_profile(games[i].name);
            use_strategy = p ? p->recommended_strategy : "auto";
        }

        /* Build MVGAL launch options */
        char mvgal_opts[2048];
        snprintf(mvgal_opts, sizeof(mvgal_opts),
                 "ENABLE_MVGAL=1 MVGAL_STRATEGY=%s",
                 use_strategy);

        if (frame_pacing)
            strncat(mvgal_opts, " MVGAL_FRAME_PACING=1",
                    sizeof(mvgal_opts) - strlen(mvgal_opts) - 1);

        if (gpu_mask) {
            char mask_line[64];
            snprintf(mask_line, sizeof(mask_line), " MVGAL_GPU_MASK=%s", gpu_mask);
            strncat(mvgal_opts, mask_line,
                    sizeof(mvgal_opts) - strlen(mvgal_opts) - 1);
        }

        /* Add profile extra env */
        const game_profile_t *p = find_profile(games[i].name);
        if (p && p->extra_env) {
            strncat(mvgal_opts, " ", sizeof(mvgal_opts) - strlen(mvgal_opts) - 1);
            strncat(mvgal_opts, p->extra_env,
                    sizeof(mvgal_opts) - strlen(mvgal_opts) - 1);
        }

        printf("Configured: %s (AppID %u)\n", games[i].name, games[i].app_id);
        printf("  Launch options: %s %%command%%\n\n", mvgal_opts);
        found++;

        /* Find the manifest to propose actual editing */
        for (int l = 0; l < lib_count; l++) {
            char manifest_path[1024];
            snprintf(manifest_path, sizeof(manifest_path), "%s/appmanifest_%u.acf",
                     libraries[l], games[i].app_id);
            if (file_exists(manifest_path)) {
                printf("  Edit this file to apply:\n");
                printf("    %s\n", manifest_path);
                printf("  In the \"AppState\" block, add or change:\n");
                printf("    \"LaunchOptions\" \"%s %%command%%\"\n\n",
                       mvgal_opts);
                break;
            }
        }
    }

    if (found == 0) {
        printf("No games matched '%s'\n", target ? target : "(all)");
        return 1;
    }

    return 0;
}

static int cmd_remove_mvgal(const char *target) {
    printf("To remove MVGAL from Steam launch options:\n");
    printf("  1. Edit the appmanifest_<appid>.acf file\n");
    printf("  2. Remove or comment the LaunchOptions line\n");
    printf("  3. Or launch the game without MVGAL: ENABLE_MVGAL=0\n");
    printf("\nAlternative: Set MVGAL_STRATEGY=single to use only one GPU\n");
    return 0;
}

static int cmd_check_setup(void) {
    const char *home = get_home_dir();

    printf("MVGAL Steam Integration Check\n");
    printf("==============================\n\n");

    /* Check MVGAL execution support */
    printf("[1] MVGAL runtime: ");
    if (access("/usr/lib/libmvgal.so", F_OK) == 0 ||
        access("/usr/local/lib/libmvgal.so", F_OK) == 0 ||
        getenv("MVGAL_ROOT")) {
        printf("found\n");
    } else {
        printf("not found (libmvgal not detected)\n");
    }

    /* Check Vulkan layer */
    printf("[2] VK_LAYER_MVGAL: ");
    const char *vk_layer_path = getenv("VK_LAYER_PATH");
    if (vk_layer_path && strstr(vk_layer_path, "mvgal")) {
        printf("registered\n");
    } else if (access("/usr/share/vulkan/implicit_layer.d/MVGAL.json", F_OK) == 0 ||
               access("/etc/vulkan/implicit_layer.d/MVGAL.json", F_OK) == 0) {
        printf("registered (system)\n");
    } else {
        printf("not registered (install MVGAL Vulkan layer)\n");
    }

    /* Check Steam directories */
    printf("[3] Steam installation: ");
    char steam_path[1024];
    snprintf(steam_path, sizeof(steam_path), "%s/.steam/steam", home);
    if (dir_exists(steam_path)) {
        printf("found at %s\n", steam_path);
    } else {
        printf("not found\n");
    }

    /* Check compatibility tool directory */
    printf("[4] MVGAL compat tool: ");
    char compat_path[1024];
    snprintf(compat_path, sizeof(compat_path), "%s/%s/mvgal",
             home, STEAM_COMPAT_TOOL_DIR);
    if (dir_exists(compat_path)) {
        printf("installed\n");
    } else {
        printf("not installed (optional)\n");
    }

    /* Enumerate games */
    char libraries_buf[16][512];
    const char *libraries[16];
    int lib_count = find_steam_libraries(home, libraries_buf, 16);
    for (int i = 0; i < lib_count; i++) libraries[i] = libraries_buf[i];

    steam_game_t games[MAX_GAMES];
    int count = discover_games(libraries, lib_count, games, MAX_GAMES);

    int mvgal_enabled = 0;
    for (int i = 0; i < count; i++) {
        if (games[i].has_mvgal) mvgal_enabled++;
    }

    printf("[5] Games with MVGAL: %d / %d\n\n", mvgal_enabled, count);

    if (mvgal_enabled == 0) {
        printf("No games currently configured with MVGAL.\n");
        printf("Run 'mvgal-steam-setup --list' to see available games.\n");
    }

    return 0;
}

static void cmd_auto_all(void) {
    const char *home = get_home_dir();
    char libraries_buf[16][512];
    const char *libraries[16];
    int lib_count = find_steam_libraries(home, libraries_buf, 16);
    for (int i = 0; i < lib_count; i++) libraries[i] = libraries_buf[i];

    steam_game_t games[MAX_GAMES];
    int count = discover_games(libraries, lib_count, games, MAX_GAMES);

    int configured = 0;
    for (int i = 0; i < count; i++) {
        const game_profile_t *p = find_profile(games[i].name);
        if (!p) continue;

        printf("Configuring '%s' with strategy '%s'%s\n",
               games[i].name, p->recommended_strategy,
               games[i].has_mvgal ? " (already configured)" : "");
        configured++;

        if (!games[i].has_mvgal) {
            printf("  -> %s %%command%%\n",
                   p->extra_env ? p->extra_env : "");
        }
    }

    printf("\nConfigured %d supported game(s).\n", configured);
    printf("Edit the appmanifest files manually to apply the changes.\n");
}

/* ============================================================================
 * Main
 * ============================================================================ */

static void print_usage(const char *prog) {
    printf("MVGAL Steam Launch Options Helper\n");
    printf("Usage:\n");
    printf("  %s --list                  List Steam games and MVGAL status\n", prog);
    printf("  %s --add <name|id>         Add MVGAL to a game\n", prog);
    printf("  %s --add --all             Configure all supported games\n", prog);
    printf("  %s --remove <name|id>      Remove MVGAL config from a game\n", prog);
    printf("  %s --check                 Check Steam/MVGAL setup\n", prog);
    printf("  %s --auto                  Auto-detect and configure\n", prog);
    printf("\nOptions:\n");
    printf("  --strategy <mode>    Scheduling strategy (afr, sfr, hybrid, single)\n");
    printf("  --frame-pacing       Enable frame pacing\n");
    printf("  --gpu-mask <mask>    GPU bitmask (e.g. 0x3)\n");
    printf("\nExamples:\n");
    printf("  %s --add \"Cyberpunk 2077\" --strategy afr\n", prog);
    printf("  %s --add 1091500 --strategy sfr --frame-pacing\n", prog);
    printf("  %s --list\n", prog);
    printf("  %s --check\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    const char *target = NULL;
    const char *strategy = NULL;
    bool frame_pacing = false;
    const char *gpu_mask = NULL;
    bool all_games = false;

    /* Parse arguments */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc)
            strategy = argv[++i];
        else if (strcmp(argv[i], "--frame-pacing") == 0)
            frame_pacing = true;
        else if (strcmp(argv[i], "--gpu-mask") == 0 && i + 1 < argc)
            gpu_mask = argv[++i];
        else if (strcmp(argv[i], "--all") == 0)
            all_games = true;
        else if (!target)
            target = argv[i];
    }

    if (strcmp(cmd, "--list") == 0 || strcmp(cmd, "-l") == 0)
        return cmd_list_games();
    else if (strcmp(cmd, "--add") == 0 || strcmp(cmd, "-a") == 0) {
        if (all_games) {
            cmd_auto_all();
            return 0;
        }
        if (!target) {
            fprintf(stderr, "Error: --add requires a game name or AppID\n");
            return 1;
        }
        return cmd_add_game(target, strategy, frame_pacing, gpu_mask);
    }
    else if (strcmp(cmd, "--remove") == 0 || strcmp(cmd, "-r") == 0) {
        return cmd_remove_mvgal(target);
    }
    else if (strcmp(cmd, "--check") == 0 || strcmp(cmd, "-c") == 0)
        return cmd_check_setup();
    else if (strcmp(cmd, "--auto") == 0) {
        cmd_auto_all();
        return 0;
    }
    else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
