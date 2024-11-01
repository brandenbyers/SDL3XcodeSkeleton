#define SDL_MAIN_USE_CALLBACKS 1 /* Use callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "main.h"

/*
 * Game Configuration
 *
 * These definitions set up the basic parameters of the game,
 * such as the size of each block (cell) in pixels, the dimensions
 * of the game grid, and the window size.
 */
#define STEP_RATE_IN_MILLISECONDS  125 /* Game logic update rate in milliseconds */
#define BLOCK_SIZE_IN_PIXELS       48  /* Size of each block (cell) in pixels */
#define GAME_GRID_WIDTH            24U /* Width of the game grid in blocks */
#define GAME_GRID_HEIGHT           14U /* Height of the game grid in blocks */
#define GAME_MATRIX_SIZE           (GAME_GRID_WIDTH * GAME_GRID_HEIGHT) /* Total number of cells */

/* Window dimensions calculated based on block size and grid size */
#define SDL_WINDOW_WIDTH  (BLOCK_SIZE_IN_PIXELS * GAME_GRID_WIDTH)
#define SDL_WINDOW_HEIGHT (BLOCK_SIZE_IN_PIXELS * GAME_GRID_HEIGHT)

/*
 * Bitmasking Definitions for Cell Storage
 *
 * These definitions are used for efficient storage and retrieval
 * of cell types in the game grid using bit manipulation.
 */
#define CELL_MAX_BITS 3U     /* Maximum bits needed to represent cell types */
#define THREE_BITS    0x7U   /* Bitmask for 3 bits (binary 111) */
#define SHIFT(x, y) (((x) + ((y) * GAME_GRID_WIDTH)) * CELL_MAX_BITS) /* Bit shift amount for cell at (x, y) */

/*
 * Enumeration of Possible Cell Types
 *
 * This enum defines different types of cells that can exist in the game grid.
 * Additional cell types can be added as needed for different game elements.
 */
typedef enum {
    CELL_EMPTY  = 0U, /* Empty cell */
    CELL_PLAYER = 1U, /* Cell occupied by the player */
    CELL_TYPE2  = 2U, /* Placeholder for other cell types */
    CELL_TYPE3  = 3U, /* Placeholder for other cell types */
    CELL_TYPE4  = 4U, /* Placeholder for other cell types */
    CELL_TYPE5  = 5U  /* Placeholder for other cell types */
} CellType;

/*
 * Enumeration of Possible Directions
 *
 * This enum defines the possible directions of movement in the game.
 */
typedef enum {
    DIR_RIGHT,
    DIR_UP,
    DIR_LEFT,
    DIR_DOWN
} Direction;

/*
 * Structure to Hold the Game State
 *
 * This structure contains all the information about the current state of the game,
 * including the game grid and the player's position and movement direction.
 */
typedef struct {
    unsigned char cells[(GAME_MATRIX_SIZE * CELL_MAX_BITS) / 8U]; /* Game grid represented using bitmasking */
    char player_xpos; /* Player's x position on the grid */
    char player_ypos; /* Player's y position on the grid */
    char next_dir;    /* Next direction of movement for the player */
} GameContext;

/*
 * Structure to Hold the Application State
 *
 * This structure contains the SDL window and renderer, as well as the game context.
 */
typedef struct {
    SDL_Window   *window;     /* SDL window */
    SDL_Renderer *renderer;   /* SDL renderer */
    GameContext   game_ctx;   /* Game context/state */
    Uint64        last_step;  /* Time of last game logic update */
} AppState;

/*
 * Function to Get the CellType at a Given Position
 *
 * This function retrieves the type of cell at the specified (x, y) position
 * in the game grid using bit manipulation for efficient storage.
 */
CellType cell_at(const GameContext *ctx, char x, char y) {
    const int shift = SHIFT(x, y);
    unsigned short range;
    SDL_memcpy(&range, ctx->cells + (shift / 8), sizeof(range));
    return (CellType)((range >> (shift % 8)) & THREE_BITS);
}

/*
 * Function to Set the CellType at a Given Position
 *
 * This function sets the type of cell at the specified (x, y) position
 * in the game grid using bit manipulation for efficient storage.
 */
static void put_cell_at_(GameContext *ctx, char x, char y, CellType ct) {
    const int shift = SHIFT(x, y);
    const int adjust = shift % 8;
    unsigned char *const pos = ctx->cells + (shift / 8);
    unsigned short range;
    SDL_memcpy(&range, pos, sizeof(range));
    range &= ~(THREE_BITS << adjust);            /* Clear bits */
    range |= (ct & THREE_BITS) << adjust;        /* Set bits */
    SDL_memcpy(pos, &range, sizeof(range));
}

/*
 * Function to Set the Position of an SDL_FRect Based on Grid Coordinates
 *
 * This function calculates the pixel position of a rectangle (used for rendering)
 * based on grid coordinates and the size of each block.
 */
static void set_rect_xy_(SDL_FRect *r, short x, short y) {
    r->x = (float)(x * BLOCK_SIZE_IN_PIXELS);
    r->y = (float)(y * BLOCK_SIZE_IN_PIXELS);
}

/*
 * Function to Wrap Around Grid Boundaries
 *
 * This function ensures that a value wraps around if it goes beyond the grid boundaries,
 * allowing for continuous movement from one edge of the grid to the opposite edge.
 */
static void wrap_around_(char *val, char max) {
    if (*val < 0) {
        *val = max - 1;
    } else if (*val > max - 1) {
        *val = 0;
    }
}

/*
 * Function to Initialize the Game State
 *
 * This function resets the game state to its initial conditions, placing the player
 * at the center of the grid and clearing all other cells.
 */
void game_initialize(GameContext *ctx) {
    SDL_zeroa(ctx->cells);                     /* Clear the game grid */
    ctx->player_xpos = GAME_GRID_WIDTH / 2;    /* Set player position to center */
    ctx->player_ypos = GAME_GRID_HEIGHT / 2;
    ctx->next_dir = DIR_RIGHT;                 /* Set initial direction */
    put_cell_at_(ctx, ctx->player_xpos, ctx->player_ypos, CELL_PLAYER); /* Place the player on the grid */
}

/*
 * Function to Update the Player's Next Direction Based on Input
 *
 * This function sets the player's next movement direction, which will be used
 * in the game logic update to move the player.
 */
void player_redir(GameContext *ctx, Direction dir) {
    ctx->next_dir = dir;
}

/*
 * Function to Update the Game Logic
 *
 * This function updates the game state, moving the player based on the current direction,
 * wrapping around the grid boundaries, and handling interactions with other cells.
 */
void game_step(GameContext *ctx) {
    CellType ct;
    char prev_xpos = ctx->player_xpos;
    char prev_ypos = ctx->player_ypos;
    
    /* Move player based on next direction */
    switch (ctx->next_dir) {
        case DIR_RIGHT:
            ++ctx->player_xpos;
            break;
        case DIR_UP:
            --ctx->player_ypos;
            break;
        case DIR_LEFT:
            --ctx->player_xpos;
            break;
        case DIR_DOWN:
            ++ctx->player_ypos;
            break;
    }
    
    /* Wrap around the grid boundaries */
    wrap_around_(&ctx->player_xpos, GAME_GRID_WIDTH);
    wrap_around_(&ctx->player_ypos, GAME_GRID_HEIGHT);
    
    /* Check for collisions or interactions */
    ct = cell_at(ctx, ctx->player_xpos, ctx->player_ypos);
    if (ct != CELL_EMPTY) {
        /* Handle collision or interaction (e.g., restart game) */
        game_initialize(ctx);
        return;
    }
    
    /* Update the player's position on the grid */
    put_cell_at_(ctx, prev_xpos, prev_ypos, CELL_EMPTY);        /* Clear previous position */
    put_cell_at_(ctx, ctx->player_xpos, ctx->player_ypos, CELL_PLAYER); /* Set new position */
}

/*
 * Function to Handle Keyboard Input Events
 *
 * This function processes keyboard input and updates the game state accordingly.
 * It handles movement input, restarting the game, and quitting the application.
 */
static int handle_key_event_(GameContext *ctx, SDL_Scancode key_code) {
    switch (key_code) {
            /* Quit application */
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_Q:
            return SDL_APP_SUCCESS;
            /* Restart the game */
        case SDL_SCANCODE_R:
            game_initialize(ctx);
            break;
            /* Change player direction */
        case SDL_SCANCODE_RIGHT:
            player_redir(ctx, DIR_RIGHT);
            break;
        case SDL_SCANCODE_UP:
            player_redir(ctx, DIR_UP);
            break;
        case SDL_SCANCODE_LEFT:
            player_redir(ctx, DIR_LEFT);
            break;
        case SDL_SCANCODE_DOWN:
            player_redir(ctx, DIR_DOWN);
            break;
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

/*
 * Main Game Loop Iteration Function
 *
 * This function is called repeatedly and handles the timing of game logic updates
 * and rendering. It ensures the game logic is updated at the specified rate and
 * renders the game grid to the screen.
 */
SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *as = (AppState *)appstate;
    GameContext *ctx = &as->game_ctx;
    const Uint64 now = SDL_GetTicks();
    SDL_FRect r;
    unsigned i;
    unsigned j;
    CellType ct;
    
    /* Run game logic if it's time to update */
    while ((now - as->last_step) >= STEP_RATE_IN_MILLISECONDS) {
        game_step(ctx);
        as->last_step += STEP_RATE_IN_MILLISECONDS;
    }
    
    /* Rendering */
    r.w = r.h = BLOCK_SIZE_IN_PIXELS;
    SDL_SetRenderDrawColor(as->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE); /* Clear color */
    SDL_RenderClear(as->renderer);
    for (i = 0; i < GAME_GRID_WIDTH; i++) {
        for (j = 0; j < GAME_GRID_HEIGHT; j++) {
            ct = cell_at(ctx, i, j);
            if (ct == CELL_EMPTY) {
                continue;
            }
            set_rect_xy_(&r, i, j);
            if (ct == CELL_PLAYER) {
                SDL_SetRenderDrawColor(as->renderer, 255, 255, 0, SDL_ALPHA_OPAQUE); /* Player color */
            } else {
                SDL_SetRenderDrawColor(as->renderer, 0, 128, 0, SDL_ALPHA_OPAQUE);   /* Other cells */
            }
            SDL_RenderFillRect(as->renderer, &r);
        }
    }
    SDL_RenderPresent(as->renderer);
    return SDL_APP_CONTINUE;
}

/*
 * Application Initialization Function
 *
 * This function initializes the application, sets up the SDL window and renderer,
 * and initializes the game state. It also sets application metadata.
 */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    /* Initialize SDL subsystems */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return SDL_APP_FAILURE;
    }
    
    /* Allocate application state */
    AppState *as = SDL_calloc(1, sizeof(AppState));
    if (!as) {
        return SDL_APP_FAILURE;
    }
    
    *appstate = as;
    
    /* Create SDL window and renderer */
    if (!SDL_CreateWindowAndRenderer("SDL3 Game Template", SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, 0, &as->window, &as->renderer)) {
        return SDL_APP_FAILURE;
    }
    
    /* Initialize game state */
    game_initialize(&as->game_ctx);
    
    as->last_step = SDL_GetTicks();
    
    return SDL_APP_CONTINUE;
}

/*
 * Event Handling Function
 *
 * This function processes SDL events, such as keyboard input and quit events,
 * and updates the application state accordingly.
 */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    GameContext *ctx = &((AppState *)appstate)->game_ctx;
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_KEY_DOWN:
            return handle_key_event_(ctx, event->key.scancode);
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

/*
 * Application Cleanup Function
 *
 * This function is called when the application is exiting and cleans up
 * resources such as the SDL renderer and window.
 */
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    if (appstate != NULL) {
        AppState *as = (AppState *)appstate;
        SDL_DestroyRenderer(as->renderer);
        SDL_DestroyWindow(as->window);
        SDL_free(as);
    }
}

