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
    
    /* Present the rendered frame */
    SDL_RenderPresent(renderer);
    
    /* Update FPS counter */
    update_fps(app);
    
    /* Check if we need to update background texture due to grid changes */
    if (game->grid_state.cells_changed) {
        create_background_texture(app);
    }
}
