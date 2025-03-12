/*
 * main.c - Entry point and main app callbacks for the Bit-Twiddled Game Engine
 *
 * This file contains the SDL app callbacks and main entry point for the application.
 * AGGRESSIVELY optimized for minimal CPU usage with pause functionality.
 * Uses an event-driven architecture inspired by the Game Boy's interrupt-based design.
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "main.h"

/* Reduced FPS when paused but visible */
#define PAUSED_FPS 5

/*
 * SDL App Callbacks
 */

/* Initialize the application */
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    /* Set hints for optimal performance */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");  /* Use Metal on Apple platforms */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");       /* Enable VSync */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1"); /* Allow screensaver for energy saving */
    
    /* Additional hints to reduce CPU usage */
    SDL_SetHint("SDL_HINT_RENDER_BATCHING", "1");  /* Enable render batching */
    SDL_SetHint("SDL_HINT_RENDER_LINE_METHOD", "3"); /* Fastest line drawing method */
    SDL_SetHint("SDL_HINT_EVENT_LOGGING", "0");    /* Disable event logging */
    SDL_SetHint("SDL_HINT_POLL_SENTINEL", "1");    /* Use poll sentinel if available */
    
    /* Energy efficiency hints */
    SDL_SetHint("SDL_POWERSTATE_POLLING_INTERVAL", "5000"); /* Check power state every 5 seconds */
    
    /* New: Metal-specific optimizations when available */
    SDL_SetHint("SDL_METAL_PREFER_LOW_POWER_DEVICE", "1"); /* Prefer integrated GPU on laptops */
    
    /* Initialize SDL with only what we need */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    
    /* Allocate application state */
    AppState* app = SDL_calloc(1, sizeof(AppState));
    if (!app) {
        return SDL_APP_FAILURE;
    }
    
    *appstate = app;
    app->app_flags = 0;  /* Not fullscreen, normal time scale */
    app->power_mode = 0; /* Start in performance mode, will adjust as needed */
    app->target_fps = LOGIC_TICK_RATE;  /* Start with standard frame rate */
    app->is_paused = false;  /* Start unpaused */
    app->needs_render = true; /* Need initial render */
    app->game_state_changed = true; /* Force initial update */
    app->last_activity_time = SDL_GetTicks(); /* Initialize last activity time */
    app->dirty_region_count = 0; /* No dirty regions yet */
    
    /* Create window and renderer with better defaults */
    Uint32 window_flags = 0;
    
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    window_flags = SDL_WINDOW_FULLSCREEN;
    app->app_flags |= APP_FULLSCREEN;
#endif
    
    /* Add SDL_WINDOW_HIGH_PIXEL_DENSITY which improves performance on Retina displays */
    window_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    
    app->window = SDL_CreateWindow("Bit-Twiddled Game Engine", WINDOW_WIDTH, WINDOW_HEIGHT, window_flags);
    if (!app->window) {
        return SDL_APP_FAILURE;
    }
    
    /* Create renderer - try metal first */
    app->renderer = SDL_CreateRenderer(app->window, "metal");
    if (!app->renderer) {
        /* Fall back to default renderer if Metal isn't available */
        app->renderer = SDL_CreateRenderer(app->window, NULL);
        if (!app->renderer) {
            return SDL_APP_FAILURE;
        }
    }
    
    /* Check power state */
    update_power_state(app);
    
    /* Configure rendering */
    configure_rendering(app);
    
    /* Initialize game state first */
    init_game(&app->game);
    
    /* Initialize last_render_time */
    app->last_render_time = SDL_GetTicks();
    
    /* Create textures (needs initialized game state) */
    create_textures(app);
    
    /* Initialize gamepad */
    initialize_gamepad(app);
    
    /* Initialize FPS counter */
    app->last_fps_time = (Uint32)SDL_GetTicks();
    app->fps_count = 0;
    app->current_fps = 0;
    
    /* Seed random number generator */
    srand((unsigned int)SDL_GetTicks());
    
    /* Log renderer information */
    const char* renderer_name = SDL_GetRendererName(app->renderer);
    SDL_Log("Using renderer: %s", renderer_name ? renderer_name : "Unknown");
    
    /* Enable power-saving behaviors in Metal renderer */
    if (renderer_name && SDL_strstr(renderer_name, "metal")) {
        SDL_Log("Applying Metal-specific power optimizations");
    }
    
    return SDL_APP_CONTINUE;
}

/* Process one event with advanced batching and sleep strategy */
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Update last activity time for any meaningful event */
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            app->last_activity_time = SDL_GetTicks();
            app->needs_render = true;  /* Force render after user input */
            break;
    }
    
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
            
        case SDL_EVENT_KEY_DOWN: {
            /* Handle keyboard input */
            if (event->key.scancode == SDL_SCANCODE_ESCAPE || event->key.scancode == SDL_SCANCODE_Q) {
                return SDL_APP_SUCCESS;
            } else if (event->key.scancode == SDL_SCANCODE_F) {
                toggle_fullscreen(app);
                app->needs_render = true;
            } else if (event->key.scancode == SDL_SCANCODE_T) {
                cycle_time_scale(app);
            } else if (event->key.scancode == SDL_SCANCODE_P) {
                /* Toggle pause state */
                app->is_paused = !app->is_paused;
                SDL_Log("Game %s", app->is_paused ? "Paused" : "Resumed");
                app->needs_render = true;
            } else {
                /* Ignore repeated key events */
                if (event->key.repeat > 0) {
                    break;
                }
                
                /* Only process game input when not paused */
                if (!app->is_paused) {
                    process_key_event(&game->input, event->key.scancode, true);
                    
                    /* Immediately mark as needing update */
                    app->game_state_changed = true;
                    
                    /* Wake up from low-power mode */
                    app->power_mode = 0;
                }
            }
            break;
        }
            
        case SDL_EVENT_KEY_UP:
            /* Only process game input when not paused */
            if (!app->is_paused) {
                process_key_event(&game->input, event->key.scancode, false);
                app->game_state_changed = true;
            }
            break;
            
        case SDL_EVENT_WINDOW_RESIZED:
            configure_rendering(app);
            app->needs_render = true;
            break;
            
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            app->is_in_background = false;
            /* Only auto-unpause if it was auto-paused due to backgrounding */
            if (app->is_paused && app->was_auto_paused) {
                app->is_paused = false;
                app->was_auto_paused = false;
                SDL_Log("Game auto-resumed from background");
                app->needs_render = true;
            }
            update_power_state(app);
            break;
            
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            app->is_in_background = true;
            /* Auto-pause when focus is lost */
            if (!app->is_paused) {
                app->is_paused = true;
                app->was_auto_paused = true;
                SDL_Log("Game auto-paused (backgrounded)");
                app->needs_render = true;
            }
            update_power_state(app);
            break;
            
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            /* Re-initialize gamepad if connection changes */
            if (app->gamepad) {
                SDL_CloseGamepad(app->gamepad);
                app->gamepad = NULL;
            }
            initialize_gamepad(app);
            break;
            
        default:
            break;
    }
    
    return SDL_APP_CONTINUE;
}

/* Main game loop with event waiting for maximum efficiency */
SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Record current time */
    uint64_t current_time = SDL_GetTicks();
    
    /* Detect idle state - if no activity for 3 seconds, enter low-power mode */
    bool is_idle = (current_time - app->last_activity_time > 3000);
    
    /* Use Game Boy style approach - maintain frame rate but only do work when needed */
    
    /* Determine appropriate update strategy based on state */
    if (app->is_paused) {
        /* When paused, we can use very long sleeps (200ms between checks) */
        app->power_mode = 2; /* Efficient mode */
        SDL_Delay(200);
        return SDL_APP_CONTINUE; /* Early exit - nothing to do when paused */
    }
    else if (is_idle && !game->player.is_moving) {
        /* When idle and not moving, preserve 60 FPS capacity but check less often */
        app->power_mode = 2; /* Efficient mode */
        
        /* Check for updates 6 times per second, but be ready to jump to 60 FPS instantly */
        static Uint64 last_idle_check = 0;
        if (current_time - last_idle_check < 166) { /* ~6 Hz check rate */
            SDL_Delay(1); /* Minimal sleep, surrender CPU slice */
            return SDL_APP_CONTINUE; /* Early exit - nothing has changed */
        }
        last_idle_check = current_time;
    }
    else if (!game->player.is_moving && !app->game_state_changed) {
        /* When not moving but still interactive, check more frequently */
        app->power_mode = 1; /* Balanced mode */
        
        /* Check for updates 20 times per second while maintaining 60 FPS capacity */
        static Uint64 last_standby_check = 0;
        if (current_time - last_standby_check < 50) { /* ~20 Hz check rate */
            SDL_Delay(1); /* Minimal sleep, surrender CPU slice */
            return SDL_APP_CONTINUE; /* Early exit - nothing has changed */
        }
        last_standby_check = current_time;
    }
    else {
        /* During active gameplay, maintain full 60 FPS with efficient processing */
        app->power_mode = 0; /* Performance mode */
    }
    
    /* Check power state periodically (every 5 seconds) */
    static Uint64 last_power_check = 0;
    if (current_time - last_power_check > 5000) {
        update_power_state(app);
        last_power_check = current_time;
    }
    
    /* Get elapsed time since last update */
    int delta_time = (int)(current_time - game->last_tick_time);
    
    /* Only process game logic if state has changed or time for an update */
    bool update_needed = app->game_state_changed ||
    game->player.is_moving ||
    (delta_time >= LOGIC_TICK_MS);
    
    if (!app->is_paused && update_needed) {
        /* Only process gamepad when we need an update */
        if (app->gamepad) {
            process_gamepad_state(&game->input, app->gamepad);
        }
        
        /* Reset last tick time */
        game->last_tick_time = current_time;
        
        /* Apply time scaling */
        delta_time = (int)(delta_time * get_time_scale(app));
        
        /* Add to accumulator */
        game->accumulated_time += delta_time;
        
        /* Run fixed time step updates with limit to prevent spiral of death */
        int max_steps = 3;
        while (game->accumulated_time >= LOGIC_TICK_MS && max_steps > 0) {
            update_game_logic_fixed_step(game);
            game->accumulated_time -= LOGIC_TICK_MS;
            max_steps--;
            
            /* Set flag to indicate game state changed */
            app->game_state_changed = true;
            app->needs_render = true;
        }
        
        /* If severely behind, reset accumulator to avoid time debt */
        if (game->accumulated_time > LOGIC_TICK_MS * 5) {
            game->accumulated_time = 0;
        }
    }
    
    /* Only render if needed */
    if (app->needs_render ||
        (app->is_paused && (current_time - app->last_render_time >= 200))) {
        
        render_game(app);
        app->last_render_time = current_time;
        app->needs_render = false;
        app->game_state_changed = false;
    }
    
    /* Calculate frame duration to determine sleep time */
    uint64_t frame_end_time = SDL_GetTicks();
    uint64_t frame_duration = frame_end_time - current_time;
    uint64_t target_frame_time = 1000 / app->target_fps;
    
    /* Sleep for the remainder of the frame if we have time left */
    if (frame_duration < target_frame_time) {
        SDL_Delay((uint32_t)(target_frame_time - frame_duration));
    } else {
        /* Yield CPU if we're behind schedule */
        SDL_Delay(1);
    }
    
    return SDL_APP_CONTINUE;
}

/* Clean up on exit */
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    /* Avoid unused parameter warning */
    (void)result;
    
    if (appstate) {
        AppState* app = (AppState*)appstate;
        
        if (app->gamepad) {
            SDL_CloseGamepad(app->gamepad);
        }
        
        destroy_textures(app);
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        SDL_free(app);
    }
    
    SDL_Quit();
}

/*
 * Helper Functions - Moved to platform.c
 */

/*
 * tvOS Specific Main Function
 */
#if defined(__APPLE__) && TARGET_OS_TV
int main(int argc, char* argv[]) {
    /* Setup custom signal handlers for tvOS */
    signal(SIGTERM, SIG_IGN);  /* Ignore SIGTERM signal */
    
    /* Use SDL_RunApp with explicit cleanup handling */
    int result = SDL_RunApp(argc, argv, SDL_main, NULL);
    
    /* Additional cleanup for tvOS */
    SDL_Quit();
    
    return result;
}
#endif
