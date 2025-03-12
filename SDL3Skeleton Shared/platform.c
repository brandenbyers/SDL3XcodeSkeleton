/*
 * platform.c - Platform-specific functions for the Bit-Twiddled Game Engine
 *
 * This file contains functions that interact with the operating system or
 * platform-specific features, such as:
 * - Power management
 * - Fullscreen handling
 * - Time scaling
 * - Adaptive performance based on system state
 */

#include "main.h"

/*
 * Power Management Functions
 */

/* Check if running on battery power (macOS implementation) */
bool is_running_on_battery(void) {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IOS && !TARGET_OS_TV
    /* Use IOKit to check power source on macOS */
    CFTypeRef power_sources = IOPSCopyPowerSourcesInfo();
    CFArrayRef power_source_list = IOPSCopyPowerSourcesList(power_sources);
    
    bool on_battery = true; /* Default to battery if we can't determine */
    
    if (power_source_list != NULL) {
        CFIndex count = CFArrayGetCount(power_source_list);
        if (count > 0) {
            /* Just check the first power source */
            CFDictionaryRef power_source = IOPSGetPowerSourceDescription(power_sources, CFArrayGetValueAtIndex(power_source_list, 0));
            if (power_source != NULL) {
                CFStringRef power_state = CFDictionaryGetValue(power_source, CFSTR(kIOPSPowerSourceStateKey));
                on_battery = !CFEqual(power_state, CFSTR(kIOPSACPowerValue));
            }
        }
        
        CFRelease(power_source_list);
    }
    
    CFRelease(power_sources);
    return on_battery;
#else
    /* For other platforms, assume we're always on battery */
    return true;
#endif
}

/* Update power state and adjust settings accordingly */
void update_power_state(AppState* app) {
    /* Check if we're running on battery */
    app->is_on_battery = is_running_on_battery();
    
    /* Frame rate and process scheduling based on activity state */
    if (app->is_in_background) {
        app->target_fps = BACKGROUND_FPS;  /* Very low FPS when completely hidden */
    }
    else {
        /* Always maintain 60 FPS for active gameplay (matches Game Boy approach) */
        app->target_fps = LOGIC_TICK_RATE;  /* Always target 60 FPS for smoothness */
        
        /* Instead of reducing FPS, we adjust how often we process work */
        if (app->is_on_battery) {
            /* On battery, be more aggressive with sleep scheduling between frames */
            if (app->power_mode == 2) { /* Efficient/idle mode */
                /* Use longer sleeps between render checks - don't reduce FPS */
                SDL_SetHint("SDL_METAL_FORCE_DEPTH_STENCIL_SHARED", "1"); /* Further optimize Metal */
            }
        }
    }
    
    /* Configure renderer based on power state */
    configure_rendering(app);
    
    /* Log power state changes */
    static bool was_on_battery = false;
    if (was_on_battery != app->is_on_battery) {
        was_on_battery = app->is_on_battery;
        SDL_Log("Power source changed: %s", app->is_on_battery ? "Battery" : "AC Power");
        
        /* Reset activity timer to ensure we're in the right power mode */
        app->last_activity_time = SDL_GetTicks();
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
    
    /* Force a render after changing time scale */
    app->needs_render = true;
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
    
    /* Force a render after changing window mode */
    app->needs_render = true;
}

/* Cycle time scale for debugging */
void cycle_time_scale(AppState* app) {
    uint8_t current = (app->app_flags & APP_TIME_SCALE) >> APP_TS_SHIFT;
    uint8_t next = (current + 1) & 0x3;  /* Cycle through 0-3 */
    set_time_scale(app, next);
    
    static const char* scale_names[] = {"normal (1x)", "slow (0.5x)", "very slow (0.25x)", "fast (2x)"};
    SDL_Log("Time scale: %s", scale_names[next]);
}

/* Reset activity timer to mark user interaction */
void reset_activity_timer(AppState* app) {
    app->last_activity_time = SDL_GetTicks();
    
    /* Return to performance mode when user interacts */
    if (app->power_mode > POWER_MODE_PERFORMANCE) {
        app->power_mode = POWER_MODE_PERFORMANCE;
        update_power_state(app);
    }
    
    /* Mark as needing render */
    app->needs_render = true;
}
