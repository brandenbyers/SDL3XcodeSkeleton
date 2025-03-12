/*
 * main.c - Entry point and main app callbacks for the Bit-Twiddled Game Engine
 *
 * This file contains the SDL app callbacks and main entry point for the application.
 * Optimized for minimal CPU usage with pause functionality.
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "main.h"

/*
 * SDL App Callbacks
 */

/* Initialize the application */
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    /* Set hints for optimal performance */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");  /* Use Metal on Apple platforms */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");       /* Enable VSync */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1"); /* Allow screensaver for energy saving */
    
    SDL_SetHint("SDL_POWERSTATE_POLLING_INTERVAL", "5000"); /* Check power state every 5 seconds */
    
    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK) < 0) {
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
    app->target_fps = LOGIC_TICK_RATE;  /* Start with standard frame rate */
    app->is_paused = false;  /* Start unpaused */
    
    /* Create window and renderer with better defaults */
    Uint32 window_flags = 0;
    
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    window_flags = SDL_WINDOW_FULLSCREEN;
    app->app_flags |= APP_FULLSCREEN;
#endif
    
    app->window = SDL_CreateWindow("Bit-Twiddled Game Engine", WINDOW_WIDTH, WINDOW_HEIGHT, window_flags);
    if (!app->window) {
        return SDL_APP_FAILURE;
    }
    
    /* In SDL3, we just specify the renderer name (Metal for Apple platforms) */
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
    
    return SDL_APP_CONTINUE;
}

/* Main game loop iteration with fixed time step and sleep optimization */
SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* If paused and backgrounded, do minimal work to save power */
    if (app->is_paused && app->is_in_background) {
        SDL_Delay(100);  /* Sleep for 100ms (10 FPS max) to save CPU */
        return SDL_APP_CONTINUE;
    }
    
    /* Record start time for frame timing */
    Uint64 frame_start_time = SDL_GetTicks();
    
    /* Check power state periodically (every 5 seconds) */
    static Uint64 last_power_check = 0;
    if (frame_start_time - last_power_check > 5000) {
        update_power_state(app);
        last_power_check = frame_start_time;
    }
    
    /* Skip game logic updates when paused, but still render */
    if (!app->is_paused) {
        /* Calculate elapsed time */
        Uint64 current_time = frame_start_time;
        int delta_time = (int)(current_time - game->last_tick_time);
        game->last_tick_time = current_time;
        
        /* Apply time scaling */
        delta_time = (int)(delta_time * get_time_scale(app));
        
        /* Check gamepad state every frame */
        if (app->gamepad) {
            process_gamepad_state(&game->input, app->gamepad);
        }
        
        /* Add to accumulator */
        game->accumulated_time += delta_time;
        
        /* Run fixed time step updates */
        int max_steps = 3;  /* Limit to avoid spiral of death if severely behind */
        while (game->accumulated_time >= LOGIC_TICK_MS && max_steps > 0) {
            update_game_logic_fixed_step(game);
            game->accumulated_time -= LOGIC_TICK_MS;
            max_steps--;
        }
        
        /* If we're severely behind, reset the accumulator */
        if (game->accumulated_time > LOGIC_TICK_MS * 5) {
            game->accumulated_time = 0;
        }
    }
    
    /* Render the game (even when paused, to show the pause overlay) */
    render_game(app);
    
    /* Calculate frame time and sleep if ahead of schedule */
    Uint64 frame_end_time = SDL_GetTicks();
    Uint64 frame_duration = frame_end_time - frame_start_time;
    
    /* Target frame time in milliseconds */
    Uint64 target_frame_time = 1000 / app->target_fps;
    
    /* If we completed the frame early, sleep to save energy */
    if (frame_duration < target_frame_time) {
        SDL_Delay((Uint32)(target_frame_time - frame_duration));
    }
    
    return SDL_APP_CONTINUE;
}

/* Process SDL events */
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
            
        case SDL_EVENT_KEY_DOWN: {
            /* Handle keyboard input */
            if (event->key.scancode == SDL_SCANCODE_ESCAPE || event->key.scancode == SDL_SCANCODE_Q) {
                return SDL_APP_SUCCESS;
            } else if (event->key.scancode == SDL_SCANCODE_F) {
                toggle_fullscreen(app);
            } else if (event->key.scancode == SDL_SCANCODE_T) {
                cycle_time_scale(app);
            } else if (event->key.scancode == SDL_SCANCODE_P) {
                /* Toggle pause state */
                app->is_paused = !app->is_paused;
                SDL_Log("Game %s", app->is_paused ? "Paused" : "Resumed");
            } else {
                /* Skip processing repeated key events if frame count is even to reduce CPU load */
                if (event->key.repeat > 0 && game->frame_count % 2 != 0) {
                    break;
                }
                
                /* Only process game input when not paused */
                if (!app->is_paused) {
                    process_key_event(&game->input, event->key.scancode, true);
                }
            }
            break;
        }
            
        case SDL_EVENT_KEY_UP:
            /* Only process game input when not paused */
            if (!app->is_paused) {
                process_key_event(&game->input, event->key.scancode, false);
            }
            break;
            
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            /* For individual button press events, maintain compatibility */
            if (event->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                return SDL_APP_SUCCESS;
            }
            break;
            
        case SDL_EVENT_WINDOW_RESIZED:
            configure_rendering(app);
            break;
            
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            app->is_in_background = false;
            /* Only auto-unpause if it was auto-paused due to backgrounding */
            if (app->is_paused && app->is_in_background) {
                app->is_paused = false;
                SDL_Log("Game auto-resumed from background");
            }
            update_power_state(app);
            break;
            
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            app->is_in_background = true;
            /* Auto-pause when focus is lost */
            if (!app->is_paused) {
                app->is_paused = true;
                SDL_Log("Game auto-paused (backgrounded)");
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
