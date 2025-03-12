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

/* Update power state with ultra-aggressive power saving */
void update_power_state(AppState* app) {
    /* Check if we're running on battery */
    app->is_on_battery = is_running_on_battery();
    
    /* Adjust based on activity state */
    if (app->is_in_background) {
        /* When in background, use extreme low-power settings */
        app->power_mode = POWER_MODE_EFFICIENT;
        app->target_fps = 0; /* 0 FPS means "only render on demand" */
    }
    else {
        /* Check idle state */
        uint64_t idle_time = SDL_GetTicks() - app->last_activity_time;
        
        if (idle_time > 10000) {
            /* After 10 seconds of inactivity, go to efficient mode */
            app->power_mode = POWER_MODE_EFFICIENT;
            app->target_fps = 0; /* Only render on demand */
            
            /* When ultra idle, actively disable VSync to reduce GPU power */
            SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
        }
        else if (idle_time > 3000) {
            /* After 3 seconds of inactivity, go to balanced mode */
            app->power_mode = POWER_MODE_BALANCED;
            app->target_fps = 30; /* Cap at 30 FPS */
        }
        else {
            /* During active use, maintain performance mode */
            app->power_mode = POWER_MODE_PERFORMANCE;
            app->target_fps = 60; /* Full 60 FPS */
        }
    }
    
    /* Force render after power state changes */
    static uint8_t last_power_mode = 255; /* Invalid initial value */
    if (last_power_mode != app->power_mode) {
        app->needs_render = true;
        last_power_mode = app->power_mode;
        
        /* Reconfigure renderer for new power settings */
        configure_rendering(app);
        
        /* Log power mode changes */
        const char* mode_names[] = {"Performance", "Balanced", "Efficient"};
        SDL_Log("Power mode changed to: %s (Battery: %s)",
                mode_names[app->power_mode],
                app->is_on_battery ? "Yes" : "No");
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

/* Reset activity timer with mode switching */
void reset_activity_timer(AppState* app) {
    uint64_t current_time = SDL_GetTicks();
    
    /* Only register activity if significant time has passed (debouncing) */
    if (current_time - app->last_activity_time > 100) {
        app->last_activity_time = current_time;
        
        /* If we were in efficient mode, switch to performance mode immediately */
        if (app->power_mode == POWER_MODE_EFFICIENT) {
            app->power_mode = POWER_MODE_PERFORMANCE;
            app->needs_render = true;
            
            /* Force immediate re-render after waking up */
            render_game(app);
            app->last_render_time = current_time;
            
            /* Reconfigure rendering for performance */
            configure_rendering(app);
        }
    }
}

/* Advanced platform-specific sleep function */
void platform_optimized_sleep(uint32_t milliseconds) {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IOS && !TARGET_OS_TV
    /* On macOS, use more precise sleep for longer durations */
    if (milliseconds > 100) {
        SDL_Delay(milliseconds);
    } else {
        /* For short durations, use SDL_Delay with minimal time */
        SDL_Delay(1);
    }
#else
    /* Default to SDL_Delay on other platforms */
    SDL_Delay(milliseconds);
#endif
}
