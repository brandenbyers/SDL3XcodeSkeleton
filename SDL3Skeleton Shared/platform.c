/*
 * platform.c - Platform-specific functions for the Bit-Twiddled Game Engine
 *
 * This file contains functions that interact with the operating system or
 * platform-specific features, such as:
 * - Power management
 * - Fullscreen handling
 * - Time scaling
 */

#include "main.h"

/*
 * Power Management Functions
 */

/* Update power state and adjust settings accordingly */
void update_power_state(AppState* app) {
    /* When in background, reduce FPS to save power */
    if (app->is_in_background) {
        app->target_fps = BACKGROUND_FPS;  /* 10 FPS (for UI responsiveness) */
        
        /* We don't force pause here - that's handled in the event system */
    } else {
        /* When in foreground, use normal framerate */
        app->target_fps = LOGIC_TICK_RATE;
    }
}

/*
 * Time and Display Functions
 */

/* Get time scale factor */
float get_time_scale(const AppState* app) {
    static const float time_scales[] = {1.0f, 0.5f, 0.25f, 2.0f};
    return time_scales[(app->app_flags & APP_TIME_SCALE) >> APP_TS_SHIFT];
}

/* Set time scale */
void set_time_scale(AppState* app, uint8_t scale_index) {
    app->app_flags = (app->app_flags & ~APP_TIME_SCALE) | ((scale_index & 0x3) << APP_TS_SHIFT);
}

/* Toggle fullscreen mode */
void toggle_fullscreen(AppState* app) {
    app->app_flags ^= APP_FULLSCREEN;  /* Toggle fullscreen bit */
    bool fullscreen = (app->app_flags & APP_FULLSCREEN) != 0;
    
    SDL_SetWindowFullscreen(app->window, fullscreen);
    if (!fullscreen) {
        SDL_SetWindowSize(app->window, WINDOW_WIDTH, WINDOW_HEIGHT);
    }
    
    configure_rendering(app);
}

/* Cycle time scale for debugging */
void cycle_time_scale(AppState* app) {
    uint8_t current = (app->app_flags & APP_TIME_SCALE) >> APP_TS_SHIFT;
    uint8_t next = (current + 1) & 0x3;  /* Cycle through 0-3 */
    set_time_scale(app, next);
    
    static const char* scale_names[] = {"normal (1x)", "slow (0.5x)", "very slow (0.25x)", "fast (2x)"};
    SDL_Log("Time scale: %s", scale_names[next]);
}
