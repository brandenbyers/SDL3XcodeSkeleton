/*
 * render.c - Rendering functions for the Bit-Twiddled Game Engine
 *
 * This file contains all rendering-related code, including:
 * - Texture management
 * - Scene drawing
 * - FPS counter
 * - Rendering configuration
 */

#include "main.h"

/* Power state colors */
const SDL_Color CELL_COLORS[CELL_MAX] = {
    { 0,   0,   0,   255 },  /* CELL_EMPTY: black */
    { 64,  64,  192, 255 },  /* CELL_WALL: blue */
    { 255, 255, 0,   255 },  /* CELL_ITEM: yellow */
};
const SDL_Color PLAYER_COLOR = { 0, 255, 0, 255 };  /* Player: green */
const SDL_Color GRID_LINE_COLOR = { 32, 32, 32, 255 }; /* Grid lines: dark gray */
const SDL_Color TOUCH_UI_COLOR = { 255, 255, 255, 128 }; /* Touch UI: semi-transparent white */

/*
 * Texture Creation Functions
 */

/* Create background texture containing walls and grid lines */
void create_background_texture(AppState* app) {
    GameState* game = &app->game;
    
    if (app->background_texture) {
        SDL_DestroyTexture(app->background_texture);
    }
    
    /* Create texture for walls and grid lines (static elements) */
    app->background_texture = SDL_CreateTexture(
                                                app->renderer,
                                                SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_TARGET,
                                                WINDOW_WIDTH,
                                                WINDOW_HEIGHT
                                                );
    
    /* Set render target to background texture */
    SDL_SetRenderTarget(app->renderer, app->background_texture);
    
    /* Clear with black background */
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    
    /* Render walls */
    SDL_FRect rect = { 0, 0, PIXEL_SCALE, PIXEL_SCALE };
    
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            CellType cell = get_cell(game, x, y);
            if (cell == CELL_WALL) {
                rect.x = x * PIXEL_SCALE;
                rect.y = y * PIXEL_SCALE;
                
                SDL_SetRenderDrawColor(app->renderer,
                                       CELL_COLORS[CELL_WALL].r,
                                       CELL_COLORS[CELL_WALL].g,
                                       CELL_COLORS[CELL_WALL].b,
                                       CELL_COLORS[CELL_WALL].a);
                SDL_RenderFillRect(app->renderer, &rect);
            }
        }
    }
    
    /* Draw grid lines */
    SDL_SetRenderDrawColor(app->renderer,
                           GRID_LINE_COLOR.r,
                           GRID_LINE_COLOR.g,
                           GRID_LINE_COLOR.b,
                           GRID_LINE_COLOR.a);
    
    /* Vertical lines */
    for (int i = 0; i <= GRID_WIDTH; i++) {
        SDL_RenderLine(app->renderer, i * PIXEL_SCALE, 0, i * PIXEL_SCALE, WINDOW_HEIGHT);
    }
    
    /* Horizontal lines */
    for (int i = 0; i <= GRID_HEIGHT; i++) {
        SDL_RenderLine(app->renderer, 0, i * PIXEL_SCALE, WINDOW_WIDTH, i * PIXEL_SCALE);
    }
    
    /* Reset render target */
    SDL_SetRenderTarget(app->renderer, NULL);
    
    /* Mark grid as updated */
    game->grid_state.cells_changed = false;
    game->grid_state.last_frame_updated = game->frame_count;
}

/* Create all textures */
void create_textures(AppState* app) {
    /* Create background texture (walls and grid lines) */
    create_background_texture(app);
}

/* Destroy all textures */
void destroy_textures(AppState* app) {
    if (app->background_texture) {
        SDL_DestroyTexture(app->background_texture);
        app->background_texture = NULL;
    }
}

/*
 * Rendering Functions
 */

/* Configure renderer for proper scaling */
void configure_rendering(AppState* app) {
    SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    SDL_SetRenderLogicalPresentation(app->renderer, WINDOW_WIDTH, WINDOW_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

/* Calculate and display FPS */
void update_fps(AppState* app) {
    app->fps_count++;
    
    uint64_t current_time = SDL_GetTicks();
    if (current_time - app->last_fps_time >= 1000) {
        app->current_fps = app->fps_count;
        app->fps_count = 0;
        app->last_fps_time = current_time;
        
        char title[64];
        snprintf(title, sizeof(title), "Bit-Twiddled Game Engine - FPS: %d", app->current_fps);
        SDL_SetWindowTitle(app->window, title);
    }
}

/* Render touch controls visualization */
void render_touch_controls(AppState* app) {
    InputState* input = &app->game.input;
    
    /* Only render if touch is active */
    if (!is_touch_active(input)) {
        return;
    }
    
    SDL_Renderer* renderer = app->renderer;
    
    /* Set color for touch UI elements */
    SDL_SetRenderDrawColor(renderer,
                           TOUCH_UI_COLOR.r,
                           TOUCH_UI_COLOR.g,
                           TOUCH_UI_COLOR.b,
                           TOUCH_UI_COLOR.a);
    
    /* Draw joystick center (starting position) */
    SDL_FRect center_rect = {
        input->touch_start_x - 15.0f,
        input->touch_start_y - 15.0f,
        30.0f, 30.0f
    };
    SDL_RenderFillRect(renderer, &center_rect);
    
    /* Draw current touch position */
    SDL_FRect touch_rect = {
        input->touch_current_x - 20.0f,
        input->touch_current_y - 20.0f,
        40.0f, 40.0f
    };
    SDL_RenderRect(renderer, &touch_rect);
    
    /* Draw line connecting them */
    SDL_RenderLine(renderer,
                   input->touch_start_x, input->touch_start_y,
                   input->touch_current_x, input->touch_current_y);
    
    /* Draw direction indicator */
    Direction touch_dir = get_touch_direction(input);
    
    if (touch_dir != DIR_NONE) {
        /* Calculate position for direction label */
        float mid_x = (input->touch_start_x + input->touch_current_x) / 2;
        float mid_y = (input->touch_start_y + input->touch_current_y) / 2 - 30;
        
        /* We would draw text here if we had text rendering capabilities */
        /* For now, just draw a circle at midpoint to indicate active direction */
        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 192);
        
        const float radius = 10.0f;
        for (float angle = 0; angle < 6.28f; angle += 0.2f) {
            float x1 = mid_x + sinf(angle) * radius;
            float y1 = mid_y + cosf(angle) * radius;
            float x2 = mid_x + sinf(angle + 0.2f) * radius;
            float y2 = mid_y + cosf(angle + 0.2f) * radius;
            SDL_RenderLine(renderer, x1, y1, x2, y2);
        }
    }
}

/* Simplified render function with minimal draw calls */
void render_game(AppState* app) {
    GameState* game = &app->game;
    SDL_Renderer* renderer = app->renderer;
    
    /* Clear the screen */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    /* 1. Render the background (walls and grid lines) */
    if (app->background_texture) {
        SDL_RenderTexture(renderer, app->background_texture, NULL, NULL);
    }
    
    /* 2. Render items */
    SDL_FRect rect = { 0, 0, PIXEL_SCALE, PIXEL_SCALE };
    SDL_SetRenderDrawColor(renderer,
                           CELL_COLORS[CELL_ITEM].r,
                           CELL_COLORS[CELL_ITEM].g,
                           CELL_COLORS[CELL_ITEM].b,
                           CELL_COLORS[CELL_ITEM].a);
    
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (get_cell(game, x, y) == CELL_ITEM) {
                /* Create smaller rectangle for item */
                rect.x = x * PIXEL_SCALE + PIXEL_SCALE * 0.25f;
                rect.y = y * PIXEL_SCALE + PIXEL_SCALE * 0.25f;
                rect.w = rect.h = PIXEL_SCALE * 0.5f;
                
                SDL_RenderFillRect(renderer, &rect);
                
                /* Reset rectangle size */
                rect.w = rect.h = PIXEL_SCALE;
            }
        }
    }
    
    /* 3. Render player */
    float visual_x, visual_y;
    get_visual_position(&game->player, &visual_x, &visual_y);
    
    rect.x = visual_x * PIXEL_SCALE;
    rect.y = visual_y * PIXEL_SCALE;
    rect.w = rect.h = PIXEL_SCALE;
    
    SDL_SetRenderDrawColor(renderer,
                           PLAYER_COLOR.r,
                           PLAYER_COLOR.g,
                           PLAYER_COLOR.b,
                           PLAYER_COLOR.a);
    SDL_RenderFillRect(renderer, &rect);
    
    /* 4. Render touch controls visualization (iOS) */
#if defined(__APPLE__) && (TARGET_OS_IOS)
    /* On iOS, always render touch controls */
    render_touch_controls(app);
#else
    /* On other platforms, only render when touch is active */
    if (is_touch_active(&game->input)) {
        render_touch_controls(app);
    }
#endif
    
    /* Present the rendered frame */
    SDL_RenderPresent(renderer);
    
    /* Update FPS counter */
    update_fps(app);
    
    /* Check if we need to update background texture due to grid changes */
    if (game->grid_state.cells_changed) {
        create_background_texture(app);
    }
}
