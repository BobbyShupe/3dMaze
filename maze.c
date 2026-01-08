#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define WALL 0
#define PATH 1
#define EXIT_TILE 2
#define MAP_PIECE 3

#define MAP_W 101
#define MAP_H 101

#define SCREEN_W 1280
#define SCREEN_H 720

#define FOV (M_PI / 3.0)

#define NUM_ROOMS 44
#define MIN_ROOM_SIZE 4
#define MAX_ROOM_SIZE 23

#define MINIMAP_SIZE 300
#define MINIMAP_MIN_ZOOM 1
#define MINIMAP_MAX_ZOOM 300
#define NUM_MAP_PIECES 3
#define MAP_REVEAL_RADIUS 55  // Radius of revealed patch (centered elsewhere)

typedef struct {
    int x, y, w, h;
} Room;

typedef struct {
    int **grid;
    int **visited;
    int w, h;
} Maze;

typedef struct {
    double x, y;
    double dir;
    double dir_x, dir_y;
    double plane_x, plane_y;
} Player;

int minimap_zoom = 12;

void init_maze_with_rooms(Maze *maze, Room *rooms, int *num_rooms);
void free_maze(Maze *maze);
void generate_maze(Maze *maze, int cx, int cy);
void draw_minimap(SDL_Renderer *ren, Maze *maze, Player *player, int map_size);
void raycast_and_draw(SDL_Renderer *ren, Maze *maze, Player *player, int show_map);
void regenerate_maze(Maze *maze, Room *rooms, Player *player);
void reveal_random_distant_patch(Maze *maze, int piece_x, int piece_y);

void init_maze_with_rooms(Maze *maze, Room *rooms, int *num_rooms) {
    maze->w = MAP_W;
    maze->h = MAP_H;
    maze->grid = malloc(maze->h * sizeof(int*));
    maze->visited = malloc(maze->h * sizeof(int*));
    for (int y = 0; y < maze->h; y++) {
        maze->grid[y] = malloc(maze->w * sizeof(int));
        maze->visited[y] = malloc(maze->w * sizeof(int));
        for (int x = 0; x < maze->w; x++) {
            maze->grid[y][x] = WALL;
            maze->visited[y][x] = 0;
        }
    }

    *num_rooms = 0;

    for (int attempts = 0; attempts < 500 && *num_rooms < NUM_ROOMS; attempts++) {
        int w = MIN_ROOM_SIZE + rand() % (MAX_ROOM_SIZE - MIN_ROOM_SIZE + 1);
        int h = MIN_ROOM_SIZE + rand() % (MAX_ROOM_SIZE - MIN_ROOM_SIZE + 1);
        if (w % 2 == 0) w++;
        if (h % 2 == 0) h++;

        int x = rand() % (maze->w - w - 6) + 3;
        int y = rand() % (maze->h - h - 6) + 3;

        int overlap = 0;
        for (int j = 0; j < *num_rooms; j++) {
            Room r = rooms[j];
            if (!(x + w + 3 <= r.x || x >= r.x + r.w + 3 ||
                  y + h + 3 <= r.y || y >= r.y + r.h + 3)) {
                overlap = 1;
                break;
            }
        }
        if (overlap) continue;

        for (int ry = y; ry < y + h; ry++) {
            for (int rx = x; rx < x + w; rx++) {
                maze->grid[ry][rx] = PATH;
            }
        }

        rooms[(*num_rooms)++] = (Room){x, y, w, h};
    }
}

void free_maze(Maze *maze) {
    for (int y = 0; y < maze->h; y++) {
        free(maze->grid[y]);
        free(maze->visited[y]);
    }
    free(maze->grid);
    free(maze->visited);
}

void generate_maze(Maze *maze, int cx, int cy) {
    maze->grid[cy][cx] = PATH;

    int dirs[4][2] = {{0,-2},{2,0},{0,2},{-2,0}};
    for (int i = 3; i > 0; i--) {
        int j = rand() % (i + 1);
        int t0 = dirs[i][0], t1 = dirs[i][1];
        dirs[i][0] = dirs[j][0]; dirs[i][1] = dirs[j][1];
        dirs[j][0] = t0; dirs[j][1] = t1;
    }

    for (int i = 0; i < 4; i++) {
        int nx = cx + dirs[i][0];
        int ny = cy + dirs[i][1];
        if (nx > 0 && nx < maze->w - 1 && ny > 0 && ny < maze->h - 1 && maze->grid[ny][nx] == WALL) {
            maze->grid[cy + dirs[i][1]/2][cx + dirs[i][0]/2] = PATH;
            generate_maze(maze, nx, ny);
        }
    }
}

// New: Reveal a circular patch centered at a random location far from the map piece
void reveal_random_distant_patch(Maze *maze, int piece_x, int piece_y) {
    int cx, cy;
    int attempts = 0;
    do {
        // Pick a random center somewhere on the map
        cx = rand() % maze->w;
        cy = rand() % maze->h;
        attempts++;
    } while ((abs(cx - piece_x) < 20 && abs(cy - piece_y) < 20) && attempts < 100);

    // If couldn't find distant spot, just pick any
    if (attempts >= 100) {
        cx = rand() % maze->w;
        cy = rand() % maze->h;
    }

    // Reveal circular area around that distant point
    for (int dy = -MAP_REVEAL_RADIUS; dy <= MAP_REVEAL_RADIUS; dy++) {
        for (int dx = -MAP_REVEAL_RADIUS; dx <= MAP_REVEAL_RADIUS; dx++) {
            int x = cx + dx;
            int y = cy + dy;
            if (x >= 0 && x < maze->w && y >= 0 && y < maze->h &&
                dx*dx + dy*dy <= MAP_REVEAL_RADIUS * MAP_REVEAL_RADIUS) {
                maze->visited[y][x] = 1;
            }
        }
    }
}

void regenerate_maze(Maze *maze, Room *rooms, Player *player) {
    // Reset fog and grid
    for (int y = 0; y < maze->h; y++) {
        for (int x = 0; x < maze->w; x++) {
            maze->visited[y][x] = 0;
            maze->grid[y][x] = WALL;
        }
    }

    // 1. Place rooms first (carving open PATH areas)
    int num_rooms = 0;
    init_maze_with_rooms(maze, rooms, &num_rooms);

    // 2. Guarantee a safe starting zone if no rooms were placed
    if (num_rooms == 0) {
        for (int y = 1; y < 8; y++) {
            for (int x = 1; x < 8; x++) {
                maze->grid[y][x] = PATH;
            }
        }
        num_rooms = 1;
        rooms[0] = (Room){1, 1, 7, 7};
    }

    // 3. Temporarily fill room interiors with WALL so backtracker can carve richly
    for (int i = 0; i < num_rooms; i++) {
        Room r = rooms[i];
        // Protect a small zone around (1,1) from being filled
        for (int ry = r.y + 1; ry < r.y + r.h - 1; ry++) {
            for (int rx = r.x + 1; rx < r.x + r.w - 1; rx++) {
                if (ry < 8 && rx < 8) continue;  // preserve starting area
                maze->grid[ry][rx] = WALL;
            }
        }
    }

    // 4. Start maze generation from first room center (guaranteed to exist)
    int start_cx = rooms[0].x + rooms[0].w / 2;
    int start_cy = rooms[0].y + rooms[0].h / 2;
    generate_maze(maze, start_cx, start_cy);

    // 5. Restore room interiors to open PATH
    for (int i = 0; i < num_rooms; i++) {
        Room r = rooms[i];
        for (int ry = r.y + 1; ry < r.y + r.h - 1; ry++) {
            for (int rx = r.x + 1; rx < r.x + r.w - 1; rx++) {
                maze->grid[ry][rx] = PATH;
            }
        }
    }

    // 6. Connect all rooms with corridors
    if (num_rooms > 1) {
        // Shuffle for random connections
        for (int i = num_rooms - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            Room temp = rooms[i];
            rooms[i] = rooms[j];
            rooms[j] = temp;
        }

        for (int i = 1; i < num_rooms; i++) {
            Room a = rooms[i];
            Room b = rooms[rand() % i];
            int x1 = a.x + a.w / 2;
            int y1 = a.y + a.h / 2;
            int x2 = b.x + b.w / 2;
            int y2 = b.y + b.h / 2;

            int x = x1, y = y1;
            while (x != x2) {
                maze->grid[y][x] = PATH;
                x += (x < x2) ? 1 : -1;
            }
            while (y != y2) {
                maze->grid[y][x] = PATH;
                y += (y < y2) ? 1 : -1;
            }
        }
    }

    // 7. Run extra backtracker from every room center for rich maze feel
    for (int i = 0; i < num_rooms; i++) {
        int cx = rooms[i].x + rooms[i].w / 2;
        int cy = rooms[i].y + rooms[i].h / 2;
        generate_maze(maze, cx, cy);
    }

    // 8. Ensure entrance is open
    maze->grid[1][1] = PATH;

    // 9. Place 3 MAP PIECES in random PATH cells (not too close to start)
    for (int i = 0; i < NUM_MAP_PIECES; i++) {
        int attempts = 0;
        int mx, my;
        do {
            mx = 8 + rand() % (maze->w - 16);
            my = 8 + rand() % (maze->h - 16);
            attempts++;
        } while ((maze->grid[my][mx] != PATH || 
                  (abs(mx - 1) < 12 && abs(my - 1) < 12)) && 
                 attempts < 1000);
        
        if (attempts < 1000) {
            maze->grid[my][mx] = MAP_PIECE;
        }
    }

    // 10. Place exit on a random PATH cell, not too close to start
    int exit_x, exit_y;
    int attempts = 0;
    do {
        exit_x = 5 + rand() % (maze->w - 10);
        exit_y = 5 + rand() % (maze->h - 10);
        attempts++;
    } while (maze->grid[exit_y][exit_x] != PATH && attempts < 1000);

    if (attempts >= 1000 || abs(exit_x - 1) < 10) {
        exit_x = maze->w - 10;
        exit_y = maze->h - 10;
    }
    maze->grid[exit_y][exit_x] = EXIT_TILE;

    // 11. FINAL SAFE START: Force open 5x5 area + corridor
    const int spawn_x = 4;
    const int spawn_y = 4;
    const int safe_size = 5;

    for (int dy = -safe_size/2; dy <= safe_size/2; dy++) {
        for (int dx = -safe_size/2; dx <= safe_size/2; dx++) {
            int x = spawn_x + dx;
            int y = spawn_y + dy;
            if (x >= 0 && x < maze->w && y >= 0 && y < maze->h) {
                maze->grid[y][x] = PATH;
            }
        }
    }

    // Guarantee an exit path to the right
    for (int x = spawn_x; x < spawn_x + 10 && x < maze->w; x++) {
        maze->grid[spawn_y][x] = PATH;
    }

    // Player spawns in center of safe zone
    player->x = spawn_x + 0.5;
    player->y = spawn_y + 0.5;
    player->dir = M_PI / 2.0;  // facing right toward open corridor
}

void draw_minimap(SDL_Renderer *ren, Maze *maze, Player *player, int map_size) {
    int margin = 20;
    int map_x = SCREEN_W - map_size - margin;
    int map_y = margin;

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
    SDL_Rect bg = {map_x - 5, map_y - 5, map_size + 10, map_size + 10};
    SDL_RenderFillRect(ren, &bg);

    float cell_size = (float)map_size / minimap_zoom;

    int center_cell_x = (int)player->x;
    int center_cell_y = (int)player->y;
    int half_view = minimap_zoom / 2;

    int start_x = center_cell_x - half_view;
    int start_y = center_cell_y - half_view;
    int end_x_view = center_cell_x + half_view;
    int end_y_view = center_cell_y + half_view;

    // Draw revealed walls
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    for (int y = start_y; y < end_y_view; y++) {
        for (int x = start_x; x < end_x_view; x++) {
            if (x < 0 || x >= maze->w || y < 0 || y >= maze->h) continue;
            if (maze->visited[y][x] && maze->grid[y][x] == WALL) {
                SDL_Rect cell = {
                    map_x + (int)((x - start_x) * cell_size),
                    map_y + (int)((y - start_y) * cell_size),
                    (int)cell_size + 1,
                    (int)cell_size + 1
                };
                SDL_RenderFillRect(ren, &cell);
            }
        }
    }

    // Draw MAP PIECES (bright blue glowing diamonds) - only if still present
    for (int y = start_y; y < end_y_view; y++) {
        for (int x = start_x; x < end_x_view; x++) {
            if (x >= 0 && x < maze->w && y >= 0 && y < maze->h && 
                maze->grid[y][x] == MAP_PIECE && maze->visited[y][x]) {
                int sx = map_x + (int)((x - start_x) * cell_size);
                int sy = map_y + (int)((y - start_y) * cell_size);
                
                SDL_SetRenderDrawColor(ren, 100, 150, 255, 255);
                SDL_Rect glow = {sx - 2, sy - 2, (int)cell_size + 4, (int)cell_size + 4};
                SDL_RenderFillRect(ren, &glow);
                
                SDL_SetRenderDrawColor(ren, 0, 100, 255, 255);
                SDL_Rect core = {sx, sy, (int)cell_size, (int)cell_size};
                SDL_RenderFillRect(ren, &core);
            }
        }
    }

    // Draw visible exit (glowing green)
    for (int y = start_y; y < end_y_view; y++) {
        for (int x = start_x; x < end_x_view; x++) {
            if (x >= 0 && x < maze->w && y >= 0 && y < maze->h && 
                maze->grid[y][x] == EXIT_TILE && maze->visited[y][x]) {
                int sx = map_x + (int)((x - start_x) * cell_size);
                int sy = map_y + (int)((y - start_y) * cell_size);
                
                SDL_SetRenderDrawColor(ren, 0, 255, 0, 255);
                SDL_Rect glow = {sx - 2, sy - 2, (int)cell_size + 4, (int)cell_size + 4};
                SDL_RenderFillRect(ren, &glow);

                SDL_SetRenderDrawColor(ren, 0, 180, 0, 255);
                SDL_Rect core = {sx, sy, (int)cell_size, (int)cell_size};
                SDL_RenderFillRect(ren, &core);
            }
        }
    }

    // Player dot and direction
    int px = map_x + map_size / 2;
    int py = map_y + map_size / 2;

    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++)
            if (dx*dx + dy*dy <= 16)
                SDL_RenderDrawPoint(ren, px + dx, py + dy);

    int dir_len = (int)(cell_size * 5);
    int dir_end_x = px + (int)(cos(player->dir) * dir_len);
    int dir_end_y = py + (int)(sin(player->dir) * dir_len);
    SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    SDL_RenderDrawLine(ren, px, py, dir_end_x, dir_end_y);
}

void raycast_and_draw(SDL_Renderer *ren, Maze *maze, Player *player, int show_map) {
    // Reveal cells via line-of-sight
    for (int x = 0; x < SCREEN_W; x++) {
        double cameraX = 2.0 * x / SCREEN_W - 1.0;
        double rayDirX = player->dir_x + player->plane_x * cameraX;
        double rayDirY = player->dir_y + player->plane_y * cameraX;

        int mapX = (int)player->x;
        int mapY = (int)player->y;

        double deltaDistX = fabs(1.0 / rayDirX);
        double deltaDistY = fabs(1.0 / rayDirY);

        int stepX = (rayDirX < 0) ? -1 : 1;
        int stepY = (rayDirY < 0) ? -1 : 1;
        double sideDistX = (rayDirX < 0) ? (player->x - mapX) * deltaDistX : (mapX + 1.0 - player->x) * deltaDistX;
        double sideDistY = (rayDirY < 0) ? (player->y - mapY) * deltaDistY : (mapY + 1.0 - player->y) * deltaDistY;

        int hit = 0;
        while (!hit) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
            }
            if (mapX < 0 || mapX >= maze->w || mapY < 0 || mapY >= maze->h ||
                maze->grid[mapY][mapX] == WALL || maze->grid[mapY][mapX] == EXIT_TILE || 
                maze->grid[mapY][mapX] == MAP_PIECE) {
                hit = 1;
            }
            if (mapX >= 0 && mapX < maze->w && mapY >= 0 && mapY < maze->h) {
                maze->visited[mapY][mapX] = 1;
            }
        }
    }

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    SDL_SetRenderDrawColor(ren, 60, 60, 100, 255);
    SDL_Rect ceiling = {0, 0, SCREEN_W, SCREEN_H / 2};
    SDL_RenderFillRect(ren, &ceiling);

    SDL_SetRenderDrawColor(ren, 40, 40, 60, 255);
    SDL_Rect floor = {0, SCREEN_H / 2, SCREEN_W, SCREEN_H / 2};
    SDL_RenderFillRect(ren, &floor);

    for (int x = 0; x < SCREEN_W; x++) {
        double cameraX = 2.0 * x / SCREEN_W - 1.0;
        double rayDirX = player->dir_x + player->plane_x * cameraX;
        double rayDirY = player->dir_y + player->plane_y * cameraX;

        int mapX = (int)player->x;
        int mapY = (int)player->y;

        double deltaDistX = fabs(1.0 / rayDirX);
        double deltaDistY = fabs(1.0 / rayDirY);

        int stepX = (rayDirX < 0) ? -1 : 1;
        int stepY = (rayDirY < 0) ? -1 : 1;
        double sideDistX = (rayDirX < 0) ? (player->x - mapX) * deltaDistX : (mapX + 1.0 - player->x) * deltaDistX;
        double sideDistY = (rayDirY < 0) ? (player->y - mapY) * deltaDistY : (mapY + 1.0 - player->y) * deltaDistY;

        int side = 0;
        int hit = 0;
        while (!hit) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }
            if (mapX < 0 || mapX >= maze->w || mapY < 0 || mapY >= maze->h ||
                maze->grid[mapY][mapX] == WALL || maze->grid[mapY][mapX] == EXIT_TILE ||
                maze->grid[mapY][mapX] == MAP_PIECE)
                hit = 1;
        }

        double perpWallDist = (side == 0)
            ? (mapX - player->x + (1 - stepX)/2.0) / rayDirX
            : (mapY - player->y + (1 - stepY)/2.0) / rayDirY;
        if (perpWallDist < 0.1) perpWallDist = 0.1;

        int lineHeight = (int)(SCREEN_H / perpWallDist);
        int drawStart = SCREEN_H / 2 - lineHeight / 2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = SCREEN_H / 2 + lineHeight / 2;
        if (drawEnd >= SCREEN_H) drawEnd = SCREEN_H - 1;

        if (maze->grid[mapY][mapX] == EXIT_TILE) {
            SDL_SetRenderDrawColor(ren, 0, 255, 100, 255);
        } else if (maze->grid[mapY][mapX] == MAP_PIECE) {
            SDL_SetRenderDrawColor(ren, 100, 150, 255, 255);
        } else {
            Uint8 brightness = side ? 140 : 220;
            SDL_SetRenderDrawColor(ren, brightness, brightness, brightness, 255);
        }
        SDL_RenderDrawLine(ren, x, drawStart, x, drawEnd);
    }

    if (show_map) draw_minimap(ren, maze, player, MINIMAP_SIZE);
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("3D Maze - Distant Map Reveals",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRelativeMouseMode(SDL_TRUE);

    Maze maze;
    Room rooms[NUM_ROOMS];

    Player player = { .x = 3.5, .y = 3.5, .dir = M_PI / 2.0 };

    regenerate_maze(&maze, rooms, &player);

    double fov_half_tan = tan(FOV / 2.0);
    player.dir_x = cos(player.dir);
    player.dir_y = sin(player.dir);
    player.plane_x = -player.dir_y * fov_half_tan;
    player.plane_y = player.dir_x * fov_half_tan;

    const double moveSpeed = 3.0;  // Slightly increased for better feel
    const double rotSpeed = 1.8;

    int show_map = 0;
    int tab_pressed = 0;
    int quit = 0;
    SDL_Event e;

    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                quit = 1;

            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_TAB && !tab_pressed) {
                show_map = !show_map;
                tab_pressed = 1;
            }
            if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_TAB)
                tab_pressed = 0;

            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_MINUS || e.key.keysym.sym == SDLK_KP_MINUS) {
                    minimap_zoom += 2;
                    if (minimap_zoom > MINIMAP_MAX_ZOOM) minimap_zoom = MINIMAP_MAX_ZOOM;
                } else if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS || e.key.keysym.sym == SDLK_KP_PLUS) {
                    minimap_zoom -= 2;
                    if (minimap_zoom < MINIMAP_MIN_ZOOM) minimap_zoom = MINIMAP_MIN_ZOOM;
                }
            }

            if (e.type == SDL_MOUSEMOTION)
                player.dir += e.motion.xrel * 0.002f * rotSpeed;
        }

        // Check for exit and map piece collision (use a small radius for more forgiveness)
        int px = (int)(player.x);
        int py = (int)(player.y);
        if (px >= 0 && px < maze.w && py >= 0 && py < maze.h) {
            if (maze.grid[py][px] == EXIT_TILE) {
                printf("EXIT FOUND! Generating new maze...\n");
                regenerate_maze(&maze, rooms, &player);
                continue;
            }
            if (maze.grid[py][px] == MAP_PIECE) {
                printf("MAP PIECE FOUND! Revealing distant area...\n");
                reveal_random_distant_patch(&maze, px, py);
                maze.grid[py][px] = PATH;
            }
        }

const Uint8 *keys = SDL_GetKeyboardState(NULL);

// Rotation with arrows (optional fallback)
if (keys[SDL_SCANCODE_LEFT])  player.dir -= rotSpeed * 0.05;
if (keys[SDL_SCANCODE_RIGHT]) player.dir += rotSpeed * 0.05;

// Movement
double dx = 0.0, dy = 0.0;

if (keys[SDL_SCANCODE_W]) {
    dx += player.dir_x * moveSpeed * 0.05;
    dy += player.dir_y * moveSpeed * 0.05;
}
if (keys[SDL_SCANCODE_S]) {
    dx -= player.dir_x * moveSpeed * 0.05;
    dy -= player.dir_y * moveSpeed * 0.05;
}
if (keys[SDL_SCANCODE_D]) {
    dx -= player.dir_y * moveSpeed * 0.05;   // left strafe
    dy += player.dir_x * moveSpeed * 0.05;
}
if (keys[SDL_SCANCODE_A]) {
    dx += player.dir_y * moveSpeed * 0.05;   // right strafe
    dy -= player.dir_x * moveSpeed * 0.05;
}

// Apply movement with separate axis collision
if (dx != 0.0 || dy != 0.0) {
    double new_x = player.x + dx;
    double new_y = player.y + dy;

    // Try X movement
    if ((int)new_x >= 0 && (int)new_x < maze.w &&
        maze.grid[(int)player.y][(int)new_x] != WALL) {
        player.x = new_x;
    }

    // Try Y movement
    if ((int)new_y >= 0 && (int)new_y < maze.h &&
        maze.grid[(int)new_y][(int)player.x] != WALL) {
        player.y = new_y;
    }
}
        // Update direction vectors for raycasting
        player.dir_x = cos(player.dir);
        player.dir_y = sin(player.dir);
        player.plane_x = -player.dir_y * fov_half_tan;
        player.plane_y = player.dir_x * fov_half_tan;

        raycast_and_draw(ren, &maze, &player, show_map);
        SDL_RenderPresent(ren);
    }

    SDL_SetRelativeMouseMode(SDL_FALSE);
    free_maze(&maze);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
