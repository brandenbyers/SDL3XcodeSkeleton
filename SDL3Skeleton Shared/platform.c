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

/* Check if running on battery power */
bool is_running_on_battery(void) {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IOS && !TARGET_OS_TV
    CFTypeRef power_sources = IOPSCopyPowerSourcesInfo();
    if (!power_sources) return false;
    
    CFArrayRef power_source_list = IOPSCopyPowerSourcesList(power_sources);
    if (!power_source_list) {
        CFRelease(power_sources);
        return false;
    }
    
    bool on_battery = false;
    int power_source_count = (int)CFArrayGetCount(power_source_list);
    
    for (int i = 0; i < power_source_count; i++) {
        CFTypeRef power_source = CFArrayGetValueAtIndex(power_source_list, i);
        CFDictionaryRef description = IOPSGetPowerSourceDescription(power_sources, power_source);
        
        if (description) {
            CFStringRef power_source_state = CFDictionaryGetValue(description, CFSTR(kIOPSPowerSourceStateKey));
            if (power_source_state && CFEqual(power_source_state, CFSTR(kIOPSBatteryPowerValue))) {
                on_battery = true;
                break;
            }
        }
    }
    
    CFRelease(power_source_list);
    CFRelease(power_sources);
    return on_battery;
#else
    /* For iOS/tvOS, assume always on battery */
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    return true;
#else
    return false;
#endif
#endif
}

/* Check if in low power mode */
bool is_in_low_power_mode(void) {
#if defined(__APPLE__) && TARGET_OS_IOS
    // For iOS, we could check NSProcessInfo.processInfo.lowPowerModeEnabled
    // But since we're in C, we'll simplify and just check if on battery
    return true;  // Assume low power mode on iOS
#else
    return false;
#endif
}

/* Update power state and adjust settings accordingly */
void update_power_state(AppState* app) {
    bool prev_battery_state = app->is_on_battery;
    
    app->is_on_battery = is_running_on_battery();
    app->is_low_power_mode = is_in_low_power_mode();
    
    /* Determine target FPS based on power state */
    if (app->is_in_background) {
        app->target_fps = BACKGROUND_FPS;
    } else if (app->is_on_battery || app->is_low_power_mode) {
        app->target_fps = BATTERY_SAVER_FPS;
    } else {
        app->target_fps = LOGIC_TICK_RATE;
    }
    
    /* If power state changed, update textures to match brightness */
    if (prev_battery_state != app->is_on_battery) {
        /* Regenerate textures with appropriate colors */
        create_background_texture(app);
        SDL_Log("Power state changed: %s", app->is_on_battery ? "On Battery" : "On AC Power");
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
