/*
 * main.c - Entry point and main app callbacks for the Bit-Twiddled Game Engine
 *
 * This file contains the SDL app callbacks and main entry point for the application.
 * Implements a consistent 60 FPS frame-based game loop with CPU optimizations.
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>

#include "game.h"
#include "input.h"
#include "entity.h"
#include "collision.h"
#include "viewport.h"
#include "render.h"
#include "level.h"

/* Reduced FPS when paused but visible */
#define PAUSED_FPS 5

/* Target frame time for 60 FPS */
#define TARGET_FRAME_TIME 16.667

/* Pre-computed lookup tables for movement */
const int8_t DIR_OFFSET_X[4] = {1, 0, -1, 0};   /* RIGHT, UP, LEFT, DOWN */
const int8_t DIR_OFFSET_Y[4] = {0, -1, 0, 1};   /* RIGHT, UP, LEFT, DOWN */

/*
 * Memory Allocation for Game State Components
 */
static MovementState s_player;
static InputState s_input;
static GridState s_grid_state;
static ViewportState s_viewport;
static EntitySystem s_entities;
static CollisionSystem s_collision;

/*
 * Initialize the Game State with Components
 */
static void setup_game_state(GameState* game) {
    /* Set up pointers to component structures */
    game->player = &s_player;
    game->input = &s_input;
    game->grid_state = &s_grid_state;
    game->viewport = &s_viewport;
    game->entities = &s_entities;
    game->collision = &s_collision;
}

/*
 * SDL App Callbacks
 */

/* Initialize the application */
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    /* Set hints for optimal rendering */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");                    /* Enable VSync */
    SDL_SetHint("SDL_RENDER_BATCHING", "1");                     /* Enable batching */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");         /* Allow screensaver */
    
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
    
    /* Set up game state components */
    setup_game_state(&app->game);
    
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
    
    /* Create renderer */
    app->renderer = SDL_CreateRenderer(app->window, NULL);
    if (!app->renderer) {
        return SDL_APP_FAILURE;
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
    SDL_Log("Using renderer: %s with frame-based game loop",
            renderer_name ? renderer_name : "Unknown");
    
    return SDL_APP_CONTINUE;
}

/* Process one event */
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Detect user interaction events */
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
    }
    
    /* Handle specific events */
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
            
        case SDL_EVENT_KEY_DOWN: {
            /* Skip repeated keys */
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
                    process_key_event(game->input, event->key.scancode, true);
                    app->game_state_changed = true;
                }
            }
            break;
        }
            
        case SDL_EVENT_KEY_UP:
            /* Only process game input when not paused */
            if (!app->is_paused) {
                process_key_event(game->input, event->key.scancode, false);
                app->game_state_changed = true;
            }
            break;
            
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            /* Process gamepad button press */
            if (!app->is_paused) {
                process_gamepad_button_event(game->input, event->gbutton.button, true);
                app->game_state_changed = true;
            }
            break;
            
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            /* Process gamepad button release */
            if (!app->is_paused) {
                process_gamepad_button_event(game->input, event->gbutton.button, false);
                app->game_state_changed = true;
            }
            break;
            
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            /* Process gamepad axis motion */
            if (!app->is_paused) {
                process_gamepad_axis_event(game->input, event->gaxis.axis, event->gaxis.value);
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
    }
    
    return SDL_APP_CONTINUE;
}

/* Main game loop - consistently runs at 60 FPS */
SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Record current time */
    uint64_t current_time = SDL_GetTicks();
    uint64_t frame_time = current_time - app->last_render_time;
    
    /* If paused, just render occasionally and sleep */
    if (app->is_paused) {
        /* When paused, render at a reduced rate */
        if (frame_time >= (1000 / PAUSED_FPS)) {
            render_game(app);
            app->last_render_time = current_time;
        }
        
        /* Sleep to free up CPU */
        SDL_Delay(16);
        return SDL_APP_CONTINUE;
    }
    
    /* FRAME-BASED APPROACH: Always update at a consistent 60 FPS */
    if (frame_time >= TARGET_FRAME_TIME) {
        /* Poll the gamepad for input */
        if (app->gamepad) {
            process_gamepad_state(game->input, app->gamepad);
        }
        
        /* Update game logic at a fixed time step */
        update_game_logic_fixed_step(game);
        
        /* Only update the last tick time after logic update */
        game->last_tick_time = current_time;
        
        /* Update window title with level info */
        const LevelInfo* level_info = get_current_level_info(game);
        char title[128];
        snprintf(title, sizeof(title), "Bit-Twiddled Game Engine - Level %d: %s - FPS: %d",
                 get_current_level(game) + 1, level_info->name, app->current_fps);
        SDL_SetWindowTitle(app->window, title);
        
        /* Render the current game state */
        render_game(app);
        
        /* Record when this frame was rendered */
        app->last_render_time = current_time;
        app->needs_render = false;
    } else {
        /* We still have time before the next frame - sleep a bit to save CPU */
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
