/*
 * main.c - Entry point and main app callbacks for the Bit-Twiddled Game Engine
 *
 * This file contains the SDL app callbacks and main entry point for the application.
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
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
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
    app->last_fps_time = SDL_GetTicks();
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
    
    /* Record start time for frame timing */
    Uint64 frame_start_time = SDL_GetTicks();
    
    /* Check power state periodically (every 5 seconds) */
    static Uint64 last_power_check = 0;
    if (frame_start_time - last_power_check > 5000) {
        update_power_state(app);
        last_power_check = frame_start_time;
    }
    
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
    while (game->accumulated_time >= LOGIC_TICK_MS) {
        update_game_logic_fixed_step(game);
        game->accumulated_time -= LOGIC_TICK_MS;
    }
    
    /* Render the game */
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
            } else {
                /* Skip processing repeated key events if frame count is even to reduce CPU load */
                if (event->key.repeat > 0 && game->frame_count % 2 != 0) {
                    break;
                }
                process_key_event(&game->input, event->key.scancode, true);
            }
            break;
        }
            
        case SDL_EVENT_KEY_UP:
            /* Handle key release */
            process_key_event(&game->input, event->key.scancode, false);
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
            update_power_state(app);
            break;
            
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            app->is_in_background = true;
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
