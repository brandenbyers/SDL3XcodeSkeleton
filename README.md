# SDL3 Xcode Skeleton

Multiplatform SDL3 2D grid-based game skeleton in Xcode with Swift Testing for C through bridging headers. This system leverages modern hardware capabilities while incorporating classic game optimization techniques for maximum efficiency. Built on SDL3 with Metal rendering support.

## Project Overview

Designed for grid-based games. It features:

- 64×32 grid with efficient power-of-two dimensions
- Smooth, consistent animation between tiles
- Correct screen wrapping calculations
- Lower FPS when backgrounded
- Cache-friendly memory access patterns
- Minimal CPU and battery usage

## Project Structure

The project is organized into five source files with a single shared header:

```
|- main.h        # Shared header with all declarations
|- main.c        # SDL app callbacks and entry point
|- game.c        # Core game mechanics and logic
|- input.c       # Input handling and mapping
|- render.c      # Rendering and texture management
|- platform.c    # Platform-specific code
```

### File Purposes

- **main.h**: Contains all shared declarations, constants, and structure definitions used throughout the project.
- **main.c**: Implements the SDL application callbacks (Init, Iterate, Event, Quit) and the main event loop. 
- **game.c**: Contains the core game mechanics, grid management, and movement logic.
- **input.c**: Manages keyboard and gamepad input processing.
- **render.c**: Handles all rendering, including texture creation and management.
- **platform.c**: Implements platform-specific functionality like power management.

## Key Design Decisions

### Single Header Approach

The project uses a single header file that's included by all implementation files.

### Power-of-Two Grid Dimensions

The game grid is 64×32 cells, using powers of two to enable efficient bitwise operations:

```c
#define GRID_WIDTH          64      /* Must be power of 2 for bit shifts */
#define GRID_HEIGHT         32      /* Must be power of 2 for bit shifts */
#define GRID_WIDTH_SHIFT    6       /* log2(64) = 6, used for shifting */
#define GRID_HEIGHT_MASK    0x1F    /* 2^5 - 1 = 31, masks lower 5 bits */
#define GRID_WIDTH_MASK     0x3F    /* 2^6 - 1 = 63, masks lower 6 bits */
```

This allows for fast coordinate calculations:
- Using bit shifts (`<<`) instead of multiplication
- Using bit masking (`&`) instead of modulo for wrapping coordinates

### Cache-Friendly Data Structures

The game uses a linear grid array instead of bit-packed cells:

```c
uint8_t grid[GRID_SIZE];  /* One byte per cell for better cache coherence */
```

This trades slightly higher memory usage for significantly better performance on modern CPUs by:
- Ensuring sequential memory access patterns
- Utilizing CPU cache lines efficiently
- Simplifying access operations

### Fixed Time Step Game Loop

The game employs a fixed time step game loop with accumulator:

```c
/* Run fixed time step updates */
while (game->accumulated_time >= LOGIC_TICK_MS) {
    update_game_logic_fixed_step(game);
    game->accumulated_time -= LOGIC_TICK_MS;
}
```

This ensures consistent, predictable game behavior regardless of frame rate.

### Energy-Aware Operation

The engine adjusts operation based on window state:

```c
/* Determine target FPS based on power state */
if (app->is_in_background) {
    app->target_fps = BACKGROUND_FPS;  /* 10 FPS */
} else {
    app->target_fps = LOGIC_TICK_RATE;  /* 60 FPS */
}
```

And implements intelligent sleep to reduce energy use:

```c
/* If we completed the frame early, sleep to save energy */
if (frame_duration < target_frame_time) {
    SDL_Delay((Uint32)(target_frame_time - frame_duration));
}
```

## Optimization Techniques

### 1. Minimal Draw Calls

The rendering system minimizes GPU state changes:

- Background elements (walls, grid lines) are pre-rendered to a texture
- Only dynamic elements (player, items) are drawn each frame
- Textures are only updated when necessary

### 2. Input Throttling

Input processing is throttled to reduce CPU usage:

```c
/* Skip processing repeated key events if frame count is even */
if (event->key.repeat > 0 && game->frame_count % 2 != 0) {
    break;
}
```

This is particularly helpful when a key is held down for continuous movement.

### 3. Efficient Grid Access

Grid access is optimized for modern CPU cache behavior:

```c
CellType get_cell(const GameState* game, int x, int y) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Direct array access - cache friendly */
    return (CellType)game->grid[idx];
}
```

### 4. Metal Rendering

On Apple platforms, the engine explicitly requests the Metal renderer:

```c
SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
```

This ensures maximum GPU efficiency on modern Apple hardware.

## Building the Project

### Required Dependencies

- SDL3

### Building with Xcode

1. Create a new Xcode project
2. Add all source files to the project
3. Make sure SDL3 framework is linked
4. In main.c, ensure these lines appear at the top:
   ```c
   #define SDL_MAIN_USE_CALLBACKS 1
   #include <SDL3/SDL.h>
   #include <SDL3/SDL_main.h>
   #include "main.h"
   ```

### Building with Command Line

```bash
gcc -o game main.c game.c input.c render.c platform.c -lSDL3
```

## Technical Deep Dives

### Movement System

The movement system uses a combination of grid-based and smooth visual representation:

1. Logical position is always integer-based grid coordinates
2. Visual position is calculated using linear interpolation:
   ```c
   *visual_x = start_x + (target_x - start_x) * t;
   *visual_y = start_y + (target_y - start_y) * t;
   ```
3. Proper wrapping is handled for screen edges

### Background Texture Generation

Instead of rendering walls and grid lines every frame, they're pre-rendered to a texture:

1. Texture is created once and only updated when the grid changes
2. Wall positions are determined from the grid
3. Grid lines are drawn at regular intervals
4. This texture is rendered as the background each frame

### Input Buffering for Corners

To allow for smoother gameplay around corners, the engine implements input buffering:

```c
/* Near the end of current movement, can buffer a turn */
if (movement->move_frame >= (FRAMES_PER_TILE - CORNER_BUFFER_FRAMES)) {
    // Buffer direction for next intersection
}
```

This allows players to input the next direction slightly before reaching an intersection.

## Future Enhancements

The following enhancements could further improve the engine:

1. **Sprite Atlas Implementation**: Creating a single texture containing all game graphics would further reduce draw calls
2. **Grid Partitioning**: For larger grids, implementing spatial partitioning could improve performance
3. **Animation System**: Adding a more sophisticated animation system would allow for more complex visuals
4. **Sound System**: Implementing audio would enhance the gameplay experience
5. **Additional Input Options**: Supporting touch controls for iOS would make the game more accessible

## Performance Considerations

The engine is designed with performance as a primary consideration but needs work:

- Current CPU usage: <20% on M1 Mac 
- Energy impact: "High" impact level
- Memory usage: ~40MB

Performance spikes may occur with continuous input, but optimization techniques mitigate this issue.

