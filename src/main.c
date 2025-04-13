#include <X11/Xlib.h>
#include <X11/Xutil.h>
#define Font XFont

#include "raylib.h"
#undef Font
#define Font XFont
#include "rcamera.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

const Vector3 ORIGIN = {0.0f, 0.0f, 0.0f};

typedef enum {
    CameraMovement,
    CursorMovement,
    ScaleWindow,
    MoveWindowZ,
    MoveWindowXY,
} ControlMode;

typedef struct {
    Window window;
    XWindowAttributes *attr;
    Model *model;
    Texture *texture;
    bool visible;
} MyWindow;

// implement dynamic array
#define DA_INIT_CAP 16

#define da_reserve(da, expected_capacity)                                              \
    do {                                                                               \
        if ((expected_capacity) > (da)->capacity) {                                    \
            if ((da)->capacity == 0) {                                                 \
                (da)->capacity = DA_INIT_CAP;                                          \
            }                                                                          \
            while ((expected_capacity) > (da)->capacity) {                             \
                (da)->capacity *= 2;                                                   \
            }                                                                          \
            (da)->items = realloc((da)->items, (da)->capacity * sizeof(*(da)->items)); \
            assert((da)->items != NULL && "Buy more RAM lol");                         \
        }                                                                              \
    } while (0)

// Append an item to a dynamic array
#define da_append(da, item)                  \
    do {                                     \
        da_reserve((da), (da)->count + 1);   \
        (da)->items[(da)->count++] = (item); \
    } while (0)

#define da_free(da) free((da).items)

// Append several items to a dynamic array
#define da_append_many(da, new_items, new_items_count)                                          \
    do {                                                                                        \
        da_reserve((da), (da)->count + (new_items_count));                                      \
        memcpy((da)->items + (da)->count, (new_items), (new_items_count)*sizeof(*(da)->items)); \
        (da)->count += (new_items_count);                                                       \
    } while (0)

#define da_resize(da, new_size)     \
    do {                            \
        da_reserve((da), new_size); \
        (da)->count = (new_size);   \
    } while (0)

#define da_last(da) (da)->items[(assert((da)->count > 0), (da)->count-1)]

#define da_remove_unordered(da, i)                   \
    do {                                             \
        size_t j = (i);                              \
        assert(j < (da)->count);                     \
        (da)->items[j] = (da)->items[--(da)->count]; \
    } while(0)

typedef struct {
    MyWindow *items;
    size_t count;
    size_t capacity;
} DA_window;

#define FOR_EACH_WINDOW(item, array)           \
    for (MyWindow *item = (array).items;       \
         item < (array).items + (array).count; \
         ++item)

// Alternative version that provides the index
#define FOR_EACH_WINDOW_INDEXED(item, index, array)                      \
    for (size_t index = 0;                                               \
         index < (array).count && ((item = &(array).items[index]) || 1); \
         ++index)

typedef struct {
    Display *display;
    Camera camera;
    ControlMode mode;
    DA_window windows;
    MyWindow *selected_window;
    Matrix original_transform;
    Vector2 original_mouse_position;

    Ray ray;
    RayCollision collision;
} GameState;

Color GetModeColor(ControlMode m) {
    switch (m) {
        case CameraMovement: return BLUE;
        case CursorMovement: return GREEN;
        case ScaleWindow:
        case MoveWindowZ:
        case MoveWindowXY: return RED;
        default: return BLACK;
    }
}

const char *GetModeText(ControlMode m) {
    switch (m) {
        case CameraMovement: return "Camera Movement";
        case CursorMovement: return "Cursor Movement";
        case ScaleWindow: return "Scale Window";
        case MoveWindowZ:
        case MoveWindowXY: return "Move Window";
        default: return "Unknown Mode";
    }
}

void MyUpdateCamera(Camera *camera) {
#define CAMERA_MOUSE_MOVE_SENSITIVITY 0.005f
#define CAMERA_MOVE_SPEED 10.0f // Units per second
    bool moveInWorldPlane = true;
    bool rotateAroundTarget = false;
    bool lockView = false;
    bool rotateUp = false;

    // Keyboard support
    float cameraMoveSpeed = CAMERA_MOVE_SPEED * GetFrameTime();
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
        CameraMoveForward(camera, cameraMoveSpeed, moveInWorldPlane);
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
        CameraMoveForward(camera, -cameraMoveSpeed, moveInWorldPlane);

    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
        CameraMoveRight(camera, -cameraMoveSpeed, moveInWorldPlane);
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
        CameraMoveRight(camera, cameraMoveSpeed, moveInWorldPlane);

    if (IsKeyPressed(KEY_F2))
        camera->target.x = camera->target.y = camera->target.z = 0.0f;
    if (IsKeyDown(KEY_F3))
        CameraMoveUp(camera, -cameraMoveSpeed);
    if (IsKeyDown(KEY_F4))
        CameraMoveUp(camera, cameraMoveSpeed);

    // Mouse-based rotation
    Vector2 mousePosition = GetMousePosition();
    Vector2 screenCenter = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    Vector2 mouseDelta = {mousePosition.x - screenCenter.x, mousePosition.y - screenCenter.y};

    float rotationSpeed = CAMERA_MOUSE_MOVE_SENSITIVITY;
    CameraYaw(camera, -mouseDelta.x * rotationSpeed, rotateAroundTarget);
    CameraPitch(camera, -mouseDelta.y * rotationSpeed, lockView, rotateAroundTarget, rotateUp);

    // Reset mouse position to center of the screen
    SetMousePosition(screenCenter.x, screenCenter.y);

    // print camera position
    //  printf("Camera position: (%f, %f, %f)\n", camera->position.x, camera->position.y, camera->position.z);
    //  printf("Camera target: (%f, %f, %f)\n", camera->target.x, camera->target.y, camera->target.z);
}

XImage *XGetRGBImage(Display *display, Window window, int x, int y, unsigned int width, unsigned int height) {
    XImage *image = XGetImage(display, window, x, y, width, height, AllPlanes, ZPixmap);
    if (image == NULL) {
        fprintf(stderr, "Unable to get image\n");
        return NULL;
    }

    unsigned char *data = (unsigned char *)image->data;

    // TODO: maybe use a shader for this
    // Swap BGR to RGB
    unsigned char *pixel = data;
    unsigned char *end = data + (width * height * 4);
    while (pixel < end) {
        unsigned char b = pixel[0];
        pixel[0] = pixel[2];
        pixel[2] = b;
        pixel += 4;
    }

    return image;
}

int MySetTexture(const XImage *image, Texture *texture) {
    if (image == NULL) {
        fprintf(stderr, "Unable to get image\n");
        return 1;
    }

    // Convert rearranged data to Image
    Image rlImg = {
        .data = image->data,
        .width = image->width,
        .height = image->height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, // Raylib does not have a B8R8G8 format
    };

    Texture newTexture = LoadTextureFromImage(rlImg);
    if (newTexture.id == 0) {
        fprintf(stderr, "Unable to load texture\n");
        return 1;
    }
    SetTextureFilter(newTexture, TEXTURE_FILTER_BILINEAR);
    UnloadImage(rlImg); // Properly unload the image to avoid memory leaks

    UnloadTexture(*texture); // Unload the old texture

    *texture = newTexture; // Assign the new texture to the model's material
}

int MyUpdateTexture(const XImage *image, Texture *texture) {
    if (image == NULL) {
        fprintf(stderr, "Unable to get image\n");
        return 1;
    }

    UpdateTexture(*texture, image->data);
}

void DrawWindowBorder(MyWindow *w, Color color) {
    //TODO: maybe use a shader for this
    float *vertices = w->model->meshes[0].vertices;
    Matrix transform = w->model->transform;

    Vector3 v1 = Vector3Transform((Vector3){vertices[0], vertices[1], vertices[2]}, transform);
    Vector3 v2 = Vector3Transform((Vector3){vertices[3], vertices[4], vertices[5]}, transform);
    Vector3 v3 = Vector3Transform((Vector3){vertices[6], vertices[7], vertices[8]}, transform);
    Vector3 v4 = Vector3Transform((Vector3){vertices[9], vertices[10], vertices[11]}, transform);

    DrawSphere(v1, 0.02f, RED);
    DrawSphere(v2, 0.02f, YELLOW);
    DrawSphere(v3, 0.02f, GREEN);
    DrawSphere(v4, 0.02f, BLUE);

    DrawLine3D(v1, v2, color);
    DrawLine3D(v2, v4, color);
    DrawLine3D(v4, v3, color);
    DrawLine3D(v3, v1, color);
}

Vector3 GetWindowNormal(const MyWindow *w) {
    float *vertices = w->model->meshes[0].vertices;
    Matrix transform = w->model->transform;

    // Calculate the normal vector (assuming the window is facing +Y in its local space)
    Vector3 normal = Vector3Transform((Vector3){0, 1, 0}, transform);
    Vector3 transformedVec = Vector3Transform((Vector3){0, 0, 0}, transform);
    normal = Vector3Subtract(normal, transformedVec);
    return Vector3Normalize(normal);
}

Vector3 GetWindowCenter(const MyWindow *w) {
    float *vertices = w->model->meshes[0].vertices;
    Matrix transform = w->model->transform;

    // Calculate the center of the window
    Vector3 v2 = Vector3Transform((Vector3){vertices[3], vertices[4], vertices[5]}, transform);
    Vector3 v3 = Vector3Transform((Vector3){vertices[6], vertices[7], vertices[8]}, transform);
    return Vector3Scale(Vector3Add(v2, v3), 0.5f);
}

void DrawWindowNormal(const MyWindow *w, Color color) {
    Vector3 normal = GetWindowNormal(w);
    Vector3 center = GetWindowCenter(w);

    // Draw the normal vector
    Vector3 endPoint = Vector3Add(center, Vector3Scale(normal, 0.5f)); // Adjust the 0.5f to change the length of the normal
    DrawLine3D(center, endPoint, color);

    // Draw a small sphere at the end of the normal vector
    DrawSphere(endPoint, 0.02f, color);
}

Matrix LookAtTarget(Matrix transform, Vector3 target) {
    Vector3 pos = {transform.m12, transform.m13, transform.m14};

    // Extract scale from original transform
    Vector3 originalX = {transform.m0, transform.m1, transform.m2};
    float scale = Vector3Length(originalX);

    Vector3 direction = Vector3Subtract(target, pos);

    // Define the new Y-axis as the direction to the camera
    Vector3 Y = Vector3Normalize(direction);

    // Define an up vector (world up, unless direction is nearly vertical)
    Vector3 up = {0.0f, 1.0f, 0.0f};
    if (fabsf(Y.y) > 0.999f) { // If direction is nearly vertical, adjust up vector
        up = (Vector3){0.0f, 0.0f, 1.0f};
    }

    // X-axis perpendicular to Y and up
    Vector3 X = Vector3Normalize(Vector3CrossProduct(up, Y));
    // Z-axis completes the orthonormal basis
    Vector3 Z = Vector3CrossProduct(X, Y);

    // Construct rotation matrix (columns are X, Y, Z)
    Matrix newTransform = {
        X.x * scale, Y.x * scale, Z.x * scale, pos.x,
        X.y * scale, Y.y * scale, Z.y * scale, pos.y,
        X.z * scale, Y.z * scale, Z.z * scale, pos.z,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    return newTransform;
}

void GameUpdate(GameState *game) {
    if (game->mode == CameraMovement) {
        MyUpdateCamera(&game->camera);
        if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            game->mode = CursorMovement;
            EnableCursor();
        }
        else if (IsKeyPressed(KEY_H)) {
            FOR_EACH_WINDOW(w, game->windows) {
                w->visible = !w->visible;
            }
        }
        else {
            FOR_EACH_WINDOW(w, game->windows) {
                w->model->transform = LookAtTarget(w->model->transform, game->camera.position);
            }
        }
    }
    else if (game->mode == CursorMovement) {
        if (IsKeyPressed(KEY_SPACE)) {
            game->mode = CameraMovement;
            DisableCursor();
        }
        else if (IsKeyPressed(KEY_H)) {
            FOR_EACH_WINDOW(w, game->windows) {
                w->visible = !w->visible;
            }
        }
        else if (game->selected_window != NULL && IsKeyPressed(KEY_S)) {
            game->mode = ScaleWindow;
            game->original_transform = game->selected_window->model->transform;
        }
        else if (game->selected_window != NULL && IsKeyPressed(KEY_Z)) {
            game->mode = MoveWindowZ;
            game->original_transform = game->selected_window->model->transform;
            game->original_mouse_position = GetMousePosition();
        }
        else if (game->selected_window != NULL && IsKeyPressed(KEY_G)) {
            game->mode = MoveWindowXY;
            game->original_transform = game->selected_window->model->transform;
            game->original_mouse_position = GetMousePosition();
        }
        else {//if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            game->ray = GetScreenToWorldRay(GetMousePosition(), game->camera);

            game->collision.distance = 1000000.0f;
            game->collision.hit = false;
            RayCollision tempCollision = {0};
            // Check collision between ray and box
            FOR_EACH_WINDOW(w, game->windows) {
                tempCollision = GetRayCollisionMesh(game->ray, w->model->meshes[0], w->model->transform);
                if (tempCollision.hit && tempCollision.distance <= game->collision.distance) {
                    game->collision = tempCollision;
                    game->selected_window = w;
                }
            }
        }
    }
    else if (game->mode == ScaleWindow) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            game->mode = CursorMovement;
        }
        else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_CAPS_LOCK)) {
            game->mode = CursorMovement;
            game->selected_window->model->transform = game->original_transform;
        }
        else {
            //TODO: maybe scale based on mouse velocity instead
            //      maybe wrap around the screen

            // scale based on mouse position distance from the center of the screen
            // the closer to the center, the smaller the scale
            // the further from the center, the larger the scale
            // should scale quadratically

            Vector2 mousePosition = GetMousePosition();
            Vector2 screenCenter = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
            Vector2 mouseDelta = {mousePosition.x - screenCenter.x, mousePosition.y - screenCenter.y};
            float scale = Vector2Length(mouseDelta) / (GetScreenWidth() / 2.0f);
            scale *= 5*scale; // scale quadratically
            if (scale < 0.03f) scale = 0.03f; // minimum scale
            if (scale > 10.0f) scale = 10.0f; // maximum scale
            Matrix scaleMat = MatrixScale(scale, scale, scale);
            game->selected_window->model->transform = MatrixMultiply(scaleMat, game->original_transform);
        }
    }
    else if (game->mode == MoveWindowZ) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            game->mode = CursorMovement;
        }
        else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_CAPS_LOCK)) {
            game->mode = CursorMovement;
            game->selected_window->model->transform = game->original_transform;
        }
        else {
            // move window toward the camera when mouse is above center
            // move window away from the camera when mouse is below center

            Vector3 pos = {game->original_transform.m12, game->original_transform.m13, game->original_transform.m14};
            Vector3 moveDirection = Vector3Subtract(pos, game->camera.position);
            float scalar = (game->original_mouse_position.y - GetMouseY()) / 60.0f;
            Vector3 moveVector = Vector3Scale(moveDirection, scalar);

            Matrix m = MatrixTranslate(moveVector.x, moveVector.y, moveVector.z);
            game->selected_window->model->transform = MatrixMultiply(game->original_transform, m);
        }
    }
    else if (game->mode == MoveWindowXY) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            game->mode = CursorMovement;
        }
        else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_CAPS_LOCK)) {
            game->mode = CursorMovement;
            game->selected_window->model->transform = game->original_transform;
        }
        else {
            Vector2 mousePosition = GetMousePosition();
            Matrix orig_t = game->original_transform;
            Vector3 pos = {orig_t.m12, orig_t.m13, orig_t.m14};
            Vector3 directionOfWindow = Vector3Subtract(pos, game->camera.position);

            Vector3 forward = Vector3Normalize(Vector3Subtract(game->camera.target, game->camera.position));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, game->camera.up));

            Vector3 moveDirectionX = Vector3RotateByAxisAngle(directionOfWindow, game->camera.up, (game->original_mouse_position.x - mousePosition.x) / 800.0f);
            Vector3 moveDirection = Vector3RotateByAxisAngle(moveDirectionX, right, (game->original_mouse_position.y - mousePosition.y) / 800.0f);

            Vector3 moveVector = Vector3Subtract(moveDirection, directionOfWindow);

            float scale = Vector3Length((Vector3){orig_t.m0, orig_t.m1, orig_t.m2});
            Vector3 newPos = Vector3Add(pos, moveVector);

            Matrix m = MatrixMultiply(
                MatrixScale(scale, scale, scale),
                MatrixTranslate(newPos.x, newPos.y, newPos.z));
            game->selected_window->model->transform = LookAtTarget(m, game->camera.position);
        }
    }

    if (game->selected_window != NULL) {
        MyWindow w = *game->selected_window;
        XImage *image = XGetRGBImage(game->display, w.window, 0, 0, w.attr->width, w.attr->height);
        MyUpdateTexture(image, w.texture);
        XDestroyImage(image);
    }
}

MyWindow *WindowInit(Display *display, Camera camera, Window id, Vector3 pos) {
    MyWindow *w = malloc(sizeof(MyWindow));

    w->window = id;

    w->attr = (XWindowAttributes *)malloc(sizeof(XWindowAttributes));
    if (XGetWindowAttributes(display, w->window, w->attr) == 0) {
        fprintf(stderr, "Unable to get window attributes\n");
        XCloseDisplay(display);
        return NULL;
    }

    // Create a simple plane mesh
    Mesh plane = GenMeshPlane(w->attr->width / 350.0f, w->attr->height / 350.0f, 1, 1); // Width, length, resX, resZ
    // Mesh cube = GenMeshCube(4.0f, 3.0f, 0.1f); // Width, height, depth

    w->model = (Model *)malloc(sizeof(Model));
    *w->model = LoadModelFromMesh(plane);

    // Set the model's material to use the texture
    w->texture = (Texture *)malloc(sizeof(Texture));

    XImage *image = XGetRGBImage(display, w->window, 0, 0, w->attr->width, w->attr->height);
    MySetTexture(image, w->texture);
    w->model->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *w->texture;
    // XDestroyImage(image);

    // printf("Window %d texture id: %d\n", i, w.texture->id);
    w->model->transform = LookAtTarget(MatrixTranslate(pos.x, pos.y, pos.z), camera.position);

    w->visible = true;
    return w;
}

GameState *GameInit() {
    // Tell the window to use vsync and work on high DPI displays
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);

    // Create the window and OpenGL context
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "3dwm");

    GameState *game = (GameState *)malloc(sizeof(GameState));
    if (game == NULL) {
        fprintf(stderr, "Failed to allocate memory for game state\n");
        return NULL;
    }
    memset(game, 0, sizeof(GameState));

    game->camera.up = (Vector3){0.0f, 1.0f, 0.0f}; // Camera up vector (rotation towards target)
    game->camera.fovy = 45.0f;                     // Camera field-of-view Y
    game->camera.projection = CAMERA_PERSPECTIVE;  // Camera projection type

    game->camera.position = (Vector3){0, 2, 8};
    game->camera.target = (Vector3){0, 0, -3};

    SetTargetFPS(60);

    game->display = XOpenDisplay(NULL);
    if (game->display == NULL) {
        fprintf(stderr, "Unable to open X display\n");
        return NULL;
    }

    MyWindow *w1 = WindowInit(game->display, game->camera, 0x1e0002c, (Vector3){0.0f, 3.25f, -0.8f});
    da_append(&game->windows, *w1);

    MyWindow *w2 = WindowInit(game->display, game->camera, 0x2a00003, (Vector3){2.0f, 2.25f, -1.0f});
    da_append(&game->windows, *w2);

    game->selected_window = &game->windows.items[0];
    game->mode = CursorMovement;

    // disable the escape key
    SetExitKey(-1);

    return game;
}

int main(void) {
    GameState *game = GameInit();
    if (game == NULL) {
        printf("Failed to initialize game\n");
        return 1;
    }

    // game loop
    while (!WindowShouldClose()) {
        GameUpdate(game);

        BeginDrawing();

        ClearBackground(RAYWHITE);

        BeginMode3D(game->camera);

        DrawGrid(10, 1.0f);

        // draw collision box
        if (game->collision.hit) {
            DrawCube(game->collision.point, 0.1f, 0.1f, 0.1f, RED);
        }
        DrawRay(game->ray, GREEN);

        FOR_EACH_WINDOW(w, game->windows) {
            if (!w->visible) break;
            DrawModel(*w->model, ORIGIN, 1.0f, WHITE);

            Color color = game->selected_window != NULL && game->selected_window->window == w->window ? RED : BLACK;
            DrawWindowBorder(w, color);

            if (game->selected_window->window == w->window)
                DrawWindowNormal(w, GREEN);
        }

        EndMode3D();

        // display frame rate on screen
        int screenWidth = GetScreenWidth();
        DrawFPS(screenWidth - 80, 10);

        DrawRectangle(10, 10, 200, 50, Fade(SKYBLUE, 0.5f));
        DrawRectangleLines(10, 10, 200, 50, BLUE);

        const char *modeText = GetModeText(game->mode);
        Color modeColor = GetModeColor(game->mode);

        DrawText(TextFormat("Mode: %s", modeText), 2, 0, 10, modeColor);
        DrawText("- Press [Space] to change modes", 20, 20, 10, DARKGRAY);
        DrawText("- Press [Escape] to exit", 20, 40, 10, DARKGRAY);

        // end the frame and get ready for the next one  (display frame, poll input, etc...)
        EndDrawing();
    }

    // cleanup
    FOR_EACH_WINDOW(w, game->windows) {
        UnloadModel(*w->model);
        XFree(w->attr);
    }
    XCloseDisplay(game->display);
    CloseWindow();
    return 0;
}
