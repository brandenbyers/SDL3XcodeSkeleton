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
    /* Set hints for absolute minimum CPU usage */
    
    /* Core rendering hints */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");               /* Use Metal on Apple */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");                    /* Enable VSync */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");         /* Allow screensaver */
    
    /* Event and render batching */
    SDL_SetHint("SDL_HINT_RENDER_BATCHING", "1");               /* Enable batching */
    SDL_SetHint("SDL_HINT_RENDER_LINE_METHOD", "3");            /* Fastest line method */
    SDL_SetHint("SDL_HINT_EVENT_LOGGING", "0");                 /* Disable logging */
    SDL_SetHint("SDL_HINT_POLL_SENTINEL", "1");                 /* Poll sentinel */
    
    /* Metal specific power saving */
    SDL_SetHint("SDL_METAL_PREFER_LOW_POWER_DEVICE", "1");      /* Prefer integrated GPU */
    SDL_SetHint("SDL_METAL_MINIMIZE_TARGET_CHANGES", "1");      /* Minimize target changes */
    
    /* New: Critical power optimization hints */
    SDL_SetHint("SDL_POWERSTATE_POLLING_INTERVAL", "10000");    /* Check power every 10s */
    SDL_SetHint("SDL_RENDER_DIRECT_MODES", "1");                /* Use direct rendering when available */
    SDL_SetHint("SDL_RENDER_LOGICAL_SIZE_MODE", "0");           /* Disable logical size scaling */
    SDL_SetHint("SDL_FRAMEBUFFER_ACCELERATION", "1");           /* Force acceleration */
    SDL_SetHint("SDL_HINT_RENDER_DRIVER_DISCARD_CLEAR", "1");   /* Optimize clear operation */
    
    /* Initialize SDL with video and gamepad subsystems */
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
    app->app_flags = 0;
    app->power_mode = POWER_MODE_PERFORMANCE;     /* Start in performance mode */
    app->target_fps = LOGIC_TICK_RATE;
    app->is_paused = false;
    app->needs_render = true;                     /* Need initial render */
    app->game_state_changed = true;               /* Force initial update */
    app->last_activity_time = SDL_GetTicks();     /* Initialize activity time */
    app->dirty_region_count = 0;
    
    /* Create window */
    Uint32 window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    window_flags |= SDL_WINDOW_FULLSCREEN;
    app->app_flags |= APP_FULLSCREEN;
#endif
    
    app->window = SDL_CreateWindow("Bit-Twiddled Game Engine", WINDOW_WIDTH, WINDOW_HEIGHT, window_flags);
    if (!app->window) {
        return SDL_APP_FAILURE;
    }
    
    /* Create metal renderer if available, fallback otherwise */
    app->renderer = SDL_CreateRenderer(app->window, "metal");
    if (!app->renderer) {
        app->renderer = SDL_CreateRenderer(app->window, NULL);
        if (!app->renderer) {
            return SDL_APP_FAILURE;
        }
    }
    
    /* Check power state and apply power settings */
    update_power_state(app);
    
    /* Configure rendering */
    configure_rendering(app);
    
    /* Initialize game state first */
    init_game(&app->game);
    
    /* Initialize last_render_time */
    app->last_render_time = SDL_GetTicks();
    
    /* Create textures */
    create_textures(app);
    
    /* Initialize gamepad immediately for better responsiveness */
    initialize_gamepad(app);
    
    /* Initialize FPS counter */
    app->last_fps_time = SDL_GetTicks();
    app->fps_count = 0;
    app->current_fps = 0;
    
    /* Seed random number generator */
    srand((unsigned int)SDL_GetTicks());
    
    /* Log renderer info */
    const char* renderer_name = SDL_GetRendererName(app->renderer);
    SDL_Log("Using renderer: %s with ultra-efficient power management",
            renderer_name ? renderer_name : "Unknown");
    
    return SDL_APP_CONTINUE;
}

/* Process one event with advanced batching and sleep strategy */
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Critical activity events that should wake up the game */
    bool is_activity_event = false;
    
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            is_activity_event = true;
            break;
    }
    
    /* Reset activity timer for any user interaction */
    if (is_activity_event) {
        reset_activity_timer(app);
        
        /* In efficient mode, force immediate render after user input */
        if (app->power_mode == POWER_MODE_EFFICIENT) {
            app->needs_render = true;
            app->power_mode = POWER_MODE_PERFORMANCE;
            configure_rendering(app);
        }
    }
    
    /* Handle specific events */
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
            
        case SDL_EVENT_KEY_DOWN: {
            /* Critical optimization: Skip repeated keys entirely */
            if (event->key.repeat > 0) {
                break;
            }
            
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
                app->needs_render = true;
                SDL_Log("Game %s", app->is_paused ? "Paused" : "Resumed");
            } else {
                /* Only process game input when not paused */
                if (!app->is_paused) {
                    process_key_event(&game->input, event->key.scancode, true);
                    app->game_state_changed = true;
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
            
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            /* Process gamepad button press */
            if (!app->is_paused) {
                process_gamepad_button_event(&game->input, event->gbutton.button, true);
                app->game_state_changed = true;
            }
            break;
            
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            /* Process gamepad button release */
            if (!app->is_paused) {
                process_gamepad_button_event(&game->input, event->gbutton.button, false);
                app->game_state_changed = true;
            }
            break;
            
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            /* Process gamepad axis motion */
            if (!app->is_paused) {
                process_gamepad_axis_event(&game->input, event->gaxis.axis, event->gaxis.value);
                app->game_state_changed = true;
            }
            break;
            
        case SDL_EVENT_WINDOW_RESIZED:
            configure_rendering(app);
            app->needs_render = true;
            break;
            
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            app->is_in_background = false;
            if (app->is_paused && app->was_auto_paused) {
                app->is_paused = false;
                app->was_auto_paused = false;
                app->needs_render = true;
                SDL_Log("Game auto-resumed from background");
            }
            update_power_state(app);
            break;
            
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            app->is_in_background = true;
            if (!app->is_paused) {
                app->is_paused = true;
                app->was_auto_paused = true;
                app->needs_render = true;
                SDL_Log("Game auto-paused (backgrounded)");
            }
            update_power_state(app);
            break;
            
        case SDL_EVENT_GAMEPAD_ADDED:
            /* Connect the gamepad if we don't have one yet */
            if (app->gamepad == NULL) {
                initialize_gamepad(app);
            }
            break;
            
        case SDL_EVENT_GAMEPAD_REMOVED:
            /* Only clean up if it's our gamepad that was removed */
            if (app->gamepad && event->gdevice.which == app->gamepad_id) {
                SDL_Log("Gamepad disconnected: %d", event->gdevice.which);
                SDL_CloseGamepad(app->gamepad);
                app->gamepad = NULL;
                app->gamepad_id = 0;
            }
            break;
            
        default:
            /* Ignore all other events completely */
            break;
    }
    
    return SDL_APP_CONTINUE;
}

/* Main game loop with true Game Boy-like sleep strategy */
SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Record current time */
    uint64_t current_time = SDL_GetTicks();
    
    /* Detect idle state */
    bool is_idle = (current_time - app->last_activity_time > 3000);
    
    /* ========== RADICAL POWER SAVING APPROACH ========== */
    
    /* APPROACH: Completely surrender CPU when possible using two-level scheduling */
    
    if (app->is_paused) {
        /* When paused, we don't need to do ANYTHING - sleep as long as possible */
        SDL_Delay(500); /* Sleep for 500ms - extremely long sleep to release CPU */
        return SDL_APP_CONTINUE;
    }
    
    /*
     * LEVEL 1: Process scheduling - determine if we should even do any work this cycle
     * This dramatically reduces how often we run game logic and even check for rendering
     */
    if (is_idle && !game->player.is_moving) {
        /* When completely idle, check state only 2 times per second (500ms intervals) */
        static uint64_t last_idle_check = 0;
        if (current_time - last_idle_check < 500) {
            SDL_Delay(100); /* Long sleep when idle */
            return SDL_APP_CONTINUE; /* Skip entire iteration - exit immediately */
        }
        last_idle_check = current_time;
        
        /* Perform an immediate input check in case we missed any */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            SDL_AppEvent(appstate, &event);
            
            /* If input occurred, break out and process normally */
            if (event.type == SDL_EVENT_KEY_DOWN ||
                event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                app->last_activity_time = current_time;
                app->power_mode = POWER_MODE_PERFORMANCE;
                app->needs_render = true;
                break;
            }
        }
    }
    
    /*
     * LEVEL 2: Only process game logic if something has actually changed
     * This is our normal processing but with strict gating to prevent unnecessary work
     */
    bool update_needed = game->player.is_moving || app->game_state_changed;
    
    if (update_needed) {
        /* Process game logic only when needed */
        int delta_time = (int)(current_time - game->last_tick_time);
        game->last_tick_time = current_time;
        
        /* Apply time scaling */
        delta_time = (int)(delta_time * get_time_scale(app));
        
        /* Add to accumulator */
        game->accumulated_time += delta_time;
        
        /* Run fixed time step updates */
        if (game->accumulated_time >= LOGIC_TICK_MS) {
            update_game_logic_fixed_step(game);
            game->accumulated_time = 0; /* Just reset to avoid drift */
            app->needs_render = true;
        }
        
        /* Poll the gamepad for input in case we missed some events */
        if (app->gamepad) {
            process_gamepad_state(&game->input, app->gamepad);
        }
    }
    
    /*
     * LEVEL 3: Only render if state has actually changed
     * This prevents the 1 FPS renders when nothing is happening
     */
    if (app->needs_render) {
        /*
         * CRITICAL: Only allow rendering if at least 16ms (60fps) has passed
         * since the last render, or the player is moving for smoothness
         */
        bool should_render =
        (current_time - app->last_render_time >= 16) || /* Frame rate limiter */
        game->player.is_moving;                         /* Smoother movement */
        
        if (should_render) {
            render_game(app);
            app->last_render_time = current_time;
            app->needs_render = false;
            app->game_state_changed = false;
        }
    }
    
    /*
     * LEVEL 4: Adaptive CPU surrender
     * Sleep for the right amount of time based on power mode
     */
    uint32_t sleep_duration;
    
    if (app->power_mode == POWER_MODE_PERFORMANCE && game->player.is_moving) {
        /* When actively moving, use very short sleeps for responsiveness */
        sleep_duration = 1; /* Minimal sleep to allow 60fps */
    }
    else if (app->power_mode == POWER_MODE_BALANCED || !update_needed) {
        /* When in balanced mode or no updates needed, sleep longer */
        sleep_duration = 16; /* ~60fps, but sleep between frames */
    }
    else {
        /* In efficient mode, surrender CPU for longest time */
        sleep_duration = 100; /* Very long sleep */
    }
    
    /* Add a check to explicitly disable VSync when in efficient mode */
    if (app->power_mode == POWER_MODE_EFFICIENT) {
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0"); /* Disable VSync to avoid GPU waiting */
    } else {
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1"); /* Enable VSync for smooth motion */
    }
    
    /* Actually sleep */
    SDL_Delay(sleep_duration);
    
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
