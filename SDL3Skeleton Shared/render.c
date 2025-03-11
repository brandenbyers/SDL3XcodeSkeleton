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
    GameState* game = &app->game;
    InputState* input = &game->input;
    SDL_Renderer* renderer = app->renderer;
    
    /* Only render if touch is active */
    if (!is_touch_active(input)) {
        return;
    }
    
    Direction touch_dir = get_touch_direction(input);
    
    /* Calculate opacity based on time since last touch movement */
    static Uint64 last_touch_time = 0;
    static Uint8 opacity = 220;  /* Initial opacity (0-255) */
    
    /* Update opacity */
    Uint64 current_time = SDL_GetTicks();
    
    /* If touch direction changed, reset opacity timer */
    static Direction last_dir = DIR_NONE;
    if (touch_dir != last_dir) {
        last_touch_time = current_time;
        opacity = 220;  /* Full opacity on direction change */
        last_dir = touch_dir;
    }
    
    /* Fade out gradually when not moving (after 500ms) */
    if (current_time - last_touch_time > 500) {
        /* Decrease opacity by 1 every frame until reaching 120 */
        opacity = (opacity > 120) ? opacity - 1 : 120;
    }
    
    /* D-pad dimensions */
    const float dpad_size = 120.0f;  /* Overall size of D-pad */
    const float button_size = dpad_size / 3.0f;  /* Size of each direction button */
    const float center_size = button_size;  /* Size of center piece */
    
    /* Calculate D-pad center position */
    float center_x = input->touch_start_x;
    float center_y = input->touch_start_y;
    
    /* Constrain to screen boundaries with padding */
    const float padding = dpad_size / 2.0f + 10.0f;
    if (center_x < padding) center_x = padding;
    if (center_x > WINDOW_WIDTH - padding) center_x = WINDOW_WIDTH - padding;
    if (center_y < padding) center_y = padding;
    if (center_y > WINDOW_HEIGHT - padding) center_y = WINDOW_HEIGHT - padding;
    
    /* Set semi-transparent color for D-pad */
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, opacity);
    
    /* Draw D-pad center */
    SDL_FRect center_rect = {
        center_x - center_size/2.0f,
        center_y - center_size/2.0f,
        center_size,
        center_size
    };
    SDL_RenderFillRect(renderer, &center_rect);
    
    /* Draw the four direction buttons */
    SDL_FRect button_rects[4];  /* RIGHT, UP, LEFT, DOWN */
    
    /* Right button */
    button_rects[0] = (SDL_FRect){
        center_x + center_size/2.0f,
        center_y - button_size/2.0f,
        button_size,
        button_size
    };
    
    /* Up button */
    button_rects[1] = (SDL_FRect){
        center_x - button_size/2.0f,
        center_y - center_size/2.0f - button_size,
        button_size,
        button_size
    };
    
    /* Left button */
    button_rects[2] = (SDL_FRect){
        center_x - center_size/2.0f - button_size,
        center_y - button_size/2.0f,
        button_size,
        button_size
    };
    
    /* Down button */
    button_rects[3] = (SDL_FRect){
        center_x - button_size/2.0f,
        center_y + center_size/2.0f,
        button_size,
        button_size
    };
    
    /* Draw D-pad buttons with outlines */
    for (int i = 0; i < 4; i++) {
        /* Draw button outline */
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, opacity);
        SDL_RenderRect(renderer, &button_rects[i]);
        
        /* If this direction is active, fill it */
        if (touch_dir == i) {
            /* Highlight active direction with a fill */
            SDL_SetRenderDrawColor(renderer, 150, 220, 255, opacity);
            SDL_RenderFillRect(renderer, &button_rects[i]);
            
            /* Add an arrow or symbol to indicate direction */
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, opacity);
            
            /* Simple directional indicators */
            float bx = button_rects[i].x;
            float by = button_rects[i].y;
            float bw = button_rects[i].w;
            float bh = button_rects[i].h;
            
            switch (i) {
                case DIR_RIGHT: /* Right triangle */
                    SDL_RenderLine(renderer, bx + bw*0.3f, by + bh*0.3f, bx + bw*0.7f, by + bh*0.5f);
                    SDL_RenderLine(renderer, bx + bw*0.7f, by + bh*0.5f, bx + bw*0.3f, by + bh*0.7f);
                    break;
                case DIR_UP: /* Up triangle */
                    SDL_RenderLine(renderer, bx + bw*0.3f, by + bh*0.7f, bx + bw*0.5f, by + bh*0.3f);
                    SDL_RenderLine(renderer, bx + bw*0.5f, by + bh*0.3f, bx + bw*0.7f, by + bh*0.7f);
                    break;
                case DIR_LEFT: /* Left triangle */
                    SDL_RenderLine(renderer, bx + bw*0.7f, by + bh*0.3f, bx + bw*0.3f, by + bh*0.5f);
                    SDL_RenderLine(renderer, bx + bw*0.3f, by + bh*0.5f, bx + bw*0.7f, by + bh*0.7f);
                    break;
                case DIR_DOWN: /* Down triangle */
                    SDL_RenderLine(renderer, bx + bw*0.3f, by + bh*0.3f, bx + bw*0.5f, by + bh*0.7f);
                    SDL_RenderLine(renderer, bx + bw*0.5f, by + bh*0.7f, bx + bw*0.7f, by + bh*0.3f);
                    break;
            }
        }
    }
    
    /* Optional: Draw the current touch position for feedback */
    if (opacity > 150) {  /* Only show when opacity is high */
        float dx = input->touch_current_x - center_x;
        float dy = input->touch_current_y - center_y;
        
        /* Constrain to maximum distance (tighter control) */
        const float max_distance = dpad_size * 0.6f;
        float distance = sqrtf(dx*dx + dy*dy);
        
        if (distance > max_distance) {
            dx = dx * max_distance / distance;
            dy = dy * max_distance / distance;
        }
        
        /* Draw touch indicator at current position (constrained) */
        SDL_FRect touch_indicator = {
            center_x + dx - 10.0f,
            center_y + dy - 10.0f,
            20.0f, 20.0f
        };
        
        SDL_SetRenderDrawColor(renderer, 255, 220, 100, opacity);
        SDL_RenderFillRect(renderer, &touch_indicator);
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
