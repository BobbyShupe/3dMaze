#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define WALL 0
#define PATH 1

#define MAP_W 81
#define MAP_H 81

#define SCREEN_W 1280
#define SCREEN_H 720

#define FOV (M_PI / 3.0)

#define NUM_ROOMS 10
#define MIN_ROOM_SIZE 7
#define MAX_ROOM_SIZE 15

typedef struct {
    int x, y, w, h;
} Room;

typedef struct {
    int **grid;
    int w, h;
} Maze;

typedef struct {
    double x, y;
    double dir;
    double dir_x, dir_y;
    double plane_x, plane_y;
} Player;

// Prototypes
void init_maze_with_rooms(Maze *maze, Room *rooms, int *num_rooms);
void free_maze(Maze *maze);
void generate_maze(Maze *maze, int cx, int cy);
void draw_minimap(SDL_Renderer *ren, Maze *maze, Player *player, int map_size);
void raycast_and_draw(SDL_Renderer *ren, Maze *maze, Player *player, int show_map);

void init_maze_with_rooms(Maze *maze, Room *rooms, int *num_rooms) {
    maze->w = MAP_W;
    maze->h = MAP_H;
    maze->grid = malloc(maze->h * sizeof(int*));
    for (int y = 0; y < maze->h; y++) {
        maze->grid[y] = malloc(maze->w * sizeof(int));
        for (int x = 0; x < maze->w; x++) {
            maze->grid[y][x] = WALL;
        }
    }

    *num_rooms = 0;

    for (int attempts = 0; attempts < 200 && *num_rooms < NUM_ROOMS; attempts++) {
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

        // Carve full room as PATH
        for (int ry = y; ry < y + h; ry++) {
            for (int rx = x; rx < x + w; rx++) {
                maze->grid[ry][rx] = PATH;
            }
        }

        rooms[(*num_rooms)++] = (Room){x, y, w, h};
    }
}

void free_maze(Maze *maze) {
    for (int y = 0; y < maze->h; y++) free(maze->grid[y]);
    free(maze->grid);
}

void generate_maze(Maze *maze, int cx, int cy) {
    maze->grid[cy][cx] = PATH;

    int dirs[4][2] = {{0,-2},{2,0},{0,2},{-2,0}};
    // Shuffle directions
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

void draw_minimap(SDL_Renderer *ren, Maze *maze, Player *player, int map_size) {
    int margin = 20;
    int map_x = SCREEN_W - map_size - margin;
    int map_y = margin;
    float cell_size = (float)map_size / maze->w;

    SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
    SDL_Rect bg = {map_x - 5, map_y - 5, map_size + 10, map_size + 10};
    SDL_RenderFillRect(ren, &bg);

    for (int y = 0; y < maze->h; y++) {
        for (int x = 0; x < maze->w; x++) {
            if (maze->grid[y][x] == WALL) {
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                SDL_Rect cell = {map_x + (int)(x * cell_size), map_y + (int)(y * cell_size),
                                 (int)cell_size + 1, (int)cell_size + 1};
                SDL_RenderFillRect(ren, &cell);
            }
        }
    }

    int px = map_x + (int)(player->x * cell_size);
    int py = map_y + (int)(player->y * cell_size);
    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++)
            if (dx*dx + dy*dy <= 9)
                SDL_RenderDrawPoint(ren, px + dx, py + dy);

    int dir_len = (int)(cell_size * 10);
    int end_x = px + (int)(cos(player->dir) * dir_len);
    int end_y = py + (int)(sin(player->dir) * dir_len);
    SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    SDL_RenderDrawLine(ren, px, py, end_x, end_y);
}

void raycast_and_draw(SDL_Renderer *ren, Maze *maze, Player *player, int show_map) {
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    // Ceiling
    SDL_SetRenderDrawColor(ren, 60, 60, 100, 255);
    SDL_Rect ceiling = {0, 0, SCREEN_W, SCREEN_H / 2};
    SDL_RenderFillRect(ren, &ceiling);

    // Floor
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

        int stepX, stepY;
        double sideDistX, sideDistY;
        int side = 0;

        if (rayDirX < 0) { stepX = -1; sideDistX = (player->x - mapX) * deltaDistX; }
        else             { stepX =  1; sideDistX = (mapX + 1.0 - player->x) * deltaDistX; }
        if (rayDirY < 0) { stepY = -1; sideDistY = (player->y - mapY) * deltaDistY; }
        else             { stepY =  1; sideDistY = (mapY + 1.0 - player->y) * deltaDistY; }

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
            if (mapX < 0 || mapX >= maze->w || mapY < 0 || mapY >= maze->h || maze->grid[mapY][mapX] == WALL)
                hit = 1;
        }

        double perpWallDist = (side == 0)
            ? (mapX - player->x + (1 - stepX) / 2.0) / rayDirX
            : (mapY - player->y + (1 - stepY) / 2.0) / rayDirY;

        if (perpWallDist < 0.1) perpWallDist = 0.1;

        int lineHeight = (int)(SCREEN_H / perpWallDist);
        int drawStart = SCREEN_H / 2 - lineHeight / 2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = SCREEN_H / 2 + lineHeight / 2;
        if (drawEnd >= SCREEN_H) drawEnd = SCREEN_H - 1;

        Uint8 brightness = side ? 140 : 220;
        SDL_SetRenderDrawColor(ren, brightness, brightness, brightness, 255);
        SDL_RenderDrawLine(ren, x, drawStart, x, drawEnd);
    }

    if (show_map) draw_minimap(ren, maze, player, 220);
}

int main(int argc, char *argv[]) {
    srand(time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("3D Maze - Connected Rooms with Rich Maze",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    SDL_SetRelativeMouseMode(SDL_TRUE);

    Maze maze;
    Room rooms[NUM_ROOMS];
    int num_rooms = 0;

    init_maze_with_rooms(&maze, rooms, &num_rooms);

    // Temporarily fill room interiors with WALL so recursive backtracker can carve into them
    for (int i = 0; i < num_rooms; i++) {
        Room r = rooms[i];
        for (int ry = r.y + 1; ry < r.y + r.h - 1; ry++) {
            for (int rx = r.x + 1; rx < r.x + r.w - 1; rx++) {
                maze.grid[ry][rx] = WALL;
            }
        }
    }

    // Start main maze generation from first room center
    int start_cx = 1, start_cy = 1;
    if (num_rooms > 0) {
        start_cx = rooms[0].x + rooms[0].w / 2;
        start_cy = rooms[0].y + rooms[0].h / 2;
    }
    generate_maze(&maze, start_cx, start_cy);

    // Restore room interiors to open PATH
    for (int i = 0; i < num_rooms; i++) {
        Room r = rooms[i];
        for (int ry = r.y + 1; ry < r.y + r.h - 1; ry++) {
            for (int rx = r.x + 1; rx < r.x + r.w - 1; rx++) {
                maze.grid[ry][rx] = PATH;
            }
        }
    }

    // === NEW: Connect all rooms with corridors (spanning tree) ===
    if (num_rooms > 1) {
        // Shuffle room order for randomness
        for (int i = num_rooms - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            Room temp = rooms[i];
            rooms[i] = rooms[j];
            rooms[j] = temp;
        }

        // Connect each room to a random previous room
        for (int i = 1; i < num_rooms; i++) {
            Room a = rooms[i];
            Room b = rooms[rand() % i];  // connect to one of the previous rooms

            int x1 = a.x + a.w / 2;
            int y1 = a.y + a.h / 2;
            int x2 = b.x + b.w / 2;
            int y2 = b.y + b.h / 2;

            // Horizontal then vertical corridor (L-shaped)
            int x = x1;
            int y = y1;
            while (x != x2) {
                maze.grid[y][x] = PATH;
                x += (x < x2) ? 1 : -1;
            }
            while (y != y2) {
                maze.grid[y][x] = PATH;
                y += (y < y2) ? 1 : -1;
            }
        }
    }

    // Run additional maze generation from every room center to fill surrounding areas richly
    for (int i = 0; i < num_rooms; i++) {
        int cx = rooms[i].x + rooms[i].w / 2;
        int cy = rooms[i].y + rooms[i].h / 2;
        generate_maze(&maze, cx, cy);
    }

    // Entrance (top) and exit (bottom-right)
    maze.grid[1][1] = PATH;                    // entrance near top-left
    maze.grid[maze.h - 2][maze.w - 2] = PATH;   // exit near bottom-right

    // Player starts in first room (or near entrance if no rooms)
    double start_x = (num_rooms > 0) ? rooms[0].x + rooms[0].w / 2.0 : 1.5;
    double start_y = (num_rooms > 0) ? rooms[0].y + rooms[0].h / 2.0 : 1.5;

    Player player = { .x = start_x, .y = start_y, .dir = M_PI / 2.0 };

    double fov_half_tan = tan(FOV / 2.0);
    player.dir_x = cos(player.dir);
    player.dir_y = sin(player.dir);
    player.plane_x = -player.dir_y * fov_half_tan;
    player.plane_y = player.dir_x * fov_half_tan;

    const double moveSpeed = 2.5;
    const double rotSpeed = 1.5;

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

            if (e.type == SDL_MOUSEMOTION)
                player.dir += e.motion.xrel * 0.002f * rotSpeed;
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);

        double dx = 0.0, dy = 0.0;

        if (keys[SDL_SCANCODE_LEFT])  player.dir -= rotSpeed * 0.05;
        if (keys[SDL_SCANCODE_RIGHT]) player.dir += rotSpeed * 0.05;

        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])   { dx += player.dir_x * moveSpeed * 0.05; dy += player.dir_y * moveSpeed * 0.05; }
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) { dx -= player.dir_x * moveSpeed * 0.05; dy -= player.dir_y * moveSpeed * 0.05; }
        if (keys[SDL_SCANCODE_A]) { dx += player.dir_y * moveSpeed * 0.05; dy -= player.dir_x * moveSpeed * 0.05; }
        if (keys[SDL_SCANCODE_D]) { dx -= player.dir_y * moveSpeed * 0.05; dy += player.dir_x * moveSpeed * 0.05; }

        if (dx != 0.0 || dy != 0.0) {
            double new_x = player.x + dx;
            double new_y = player.y + dy;

            // Simple collision: check X and Y separately
            if ((int)new_x >= 0 && (int)new_x < maze.w && maze.grid[(int)player.y][(int)new_x] == PATH)
                player.x = new_x;
            if ((int)new_y >= 0 && (int)new_y < maze.h && maze.grid[(int)new_y][(int)player.x] == PATH)
                player.y = new_y;
        }

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
