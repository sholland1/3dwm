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

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define WIN_COUNT 2

const Vector3 ORIGIN = {0.0f, 0.0f, 0.0f};

typedef enum ControlMode {
    CameraMovement,
    CursorMovement,
    ScaleObject,
} ControlMode;

ControlMode mode = CursorMovement;

typedef struct MyWindow {
    Window window;
    XWindowAttributes *attr;
    Model *model;
    Texture *texture;
    float originalScale;
    float scale;
    Vector3 position;
    Vector3 rotation;
    float rotationAngle;
} MyWindow;

void MyUpdateCamera(Camera *camera) {
#define CAMERA_MOUSE_MOVE_SENSITIVITY 0.005f
#define CAMERA_MOVE_SPEED 10.0f // Units per second
    bool moveInWorldPlane = true;
    bool rotateAroundTarget = false;
    bool lockView = false;
    bool rotateUp = false;

    // Keyboard support
    float cameraMoveSpeed = CAMERA_MOVE_SPEED * GetFrameTime();
    if (IsKeyDown(KEY_UP))
        CameraMoveForward(camera, cameraMoveSpeed, moveInWorldPlane);
    if (IsKeyDown(KEY_DOWN))
        CameraMoveForward(camera, -cameraMoveSpeed, moveInWorldPlane);

    if (IsKeyDown(KEY_LEFT))
        CameraMoveRight(camera, -cameraMoveSpeed, moveInWorldPlane);
    if (IsKeyDown(KEY_RIGHT))
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

BoundingBox GetWindowBoundingBox(MyWindow *w) {
    // Since we know there's only one mesh (plane), we can simplify
    Mesh mesh = w->model->meshes[0];
    int vertexCount = mesh.vertexCount;

    // Initialize with first vertex instead of arbitrary large value
    Vector3 firstVertex = {
        mesh.vertices[0],      // x
        mesh.vertices[1],      // y
        mesh.vertices[2]       // z
    };
    BoundingBox bbox = {
        .min = firstVertex,
        .max = firstVertex
    };

    // Start from second vertex (i=1) since we used first for initialization
    // Unroll the vertex access to reduce multiplications
    float *vertices = mesh.vertices;
    for (int i = 1; i < vertexCount; i++) {
        int base = i * 3;
        float x = vertices[base];
        float y = vertices[base + 1];
        float z = vertices[base + 2];

        // Use compound assignment to potentially improve branch prediction
        bbox.min.x = (x < bbox.min.x) ? x : bbox.min.x;
        bbox.min.y = (y < bbox.min.y) ? y : bbox.min.y;
        bbox.min.z = (z < bbox.min.z) ? z : bbox.min.z;

        bbox.max.x = (x > bbox.max.x) ? x : bbox.max.x;
        bbox.max.y = (y > bbox.max.y) ? y : bbox.max.y;
        bbox.max.z = (z > bbox.max.z) ? z : bbox.max.z;
    }

    // Transform with pre-computed values
    Vector3 position = w->position;
    float scale = w->scale;  // Single scale value since it's uniform

    // Inline the transformation to avoid function calls
    bbox.min.x = bbox.min.x * scale + position.x;
    bbox.min.y = bbox.min.y * scale + position.y;
    bbox.min.z = bbox.min.z * scale + position.z;

    bbox.max.x = bbox.max.x * scale + position.x;
    bbox.max.y = bbox.max.y * scale + position.y;
    bbox.max.z = bbox.max.z * scale + position.z;

    return bbox;
}

int main(void) {
    // Tell the window to use vsync and work on high DPI displays
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);

    // Create the window and OpenGL context
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "3dwm");

    Camera3D camera = {0};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f}; // Camera up vector (rotation towards target)
    camera.fovy = 45.0f;                     // Camera field-of-view Y
    camera.projection = CAMERA_PERSPECTIVE;  // Camera projection type

    camera.position = (Vector3){0, 2, 8};
    camera.target = (Vector3){0, 0, -3};

    SetTargetFPS(60);

    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Unable to open X display\n");
        return 1;
    }

    MyWindow windows[WIN_COUNT] = {0};
    windows[0].window = 0x1e0002c;
    windows[0].position = (Vector3){0, 3.25f, -1};
    windows[0].rotation = (Vector3){1, 0, 0};
    windows[0].rotationAngle = 90;
    windows[1].window = 0x2a00003;
    windows[1].position = (Vector3){2, 2.25f, -0.8};;
    windows[1].rotation = (Vector3){1, 0, 0};
    windows[1].rotationAngle = 90;

    for (int i = 0; i < WIN_COUNT; i++) {
        MyWindow w = windows[i];
        w.attr = (XWindowAttributes *)malloc(sizeof(XWindowAttributes));
        if (XGetWindowAttributes(display, w.window, w.attr) == 0) {
            fprintf(stderr, "Unable to get window attributes\n");
            XCloseDisplay(display);
            return 1;
        }

        // Create a simple plane mesh
        Mesh plane = GenMeshPlane(w.attr->width / 350.0f, w.attr->height / 350.0f, 1, 1); // Width, length, resX, resZ
        // Mesh cube = GenMeshCube(4.0f, 3.0f, 0.1f); // Width, height, depth

        w.model = (Model *)malloc(sizeof(Model));
        *w.model = LoadModelFromMesh(plane);

        // Set the model's material to use the texture
        w.texture = (Texture *)malloc(sizeof(Texture));

        XImage *image = XGetRGBImage(display, w.window, 0, 0, w.attr->width, w.attr->height);
        MySetTexture(image, w.texture);
        w.model->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = *w.texture;
        // XDestroyImage(image);

        // printf("Window %d texture id: %d\n", i, w.texture->id);

        w.scale = 1;
        w.originalScale = 1;

        windows[i] = w;
    }

    MyWindow *selected_window = &windows[0];

    // disable the escape key
    SetExitKey(-1);

    Ray ray = {0};
    RayCollision collision = { 0 };

    // game loop
    while (!WindowShouldClose()) {
        if (mode == CameraMovement) {
            MyUpdateCamera(&camera);
            if (IsKeyPressed(KEY_SPACE)) {
                mode = CursorMovement;
                EnableCursor();
            }
        }
        else if (mode == CursorMovement) {
            if (IsKeyPressed(KEY_SPACE)) {
                mode = CameraMovement;
                DisableCursor();
            }
            else if (selected_window != NULL && IsKeyPressed(KEY_S)) {
                mode = ScaleObject;
                selected_window->originalScale = selected_window->scale;
            }
            else {//if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                // rlPushMatrix();
                // rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);
                ray = GetScreenToWorldRay(GetMousePosition(), camera);

                // Check collision between ray and box
                for (int i = 0; i < WIN_COUNT; i++) {
                    MyWindow w = windows[i];
                    // printf("Window %d: %d\n", i, w.window);
                    // printf("Window %d: %d\n", i, collision.hit);

                    // get the bounding box of the plane
                    collision = GetRayCollisionBox(ray, GetWindowBoundingBox(&w));
                    if (collision.hit) {
                        selected_window = &windows[i];
                        printf("Selected window: %d\n", i);
                        break;
                    }
                }
                // rlPopMatrix();
            }
        }
        else if (mode == ScaleObject) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                mode = CursorMovement;
            }
            else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_CAPS_LOCK)) {
                mode = CursorMovement;
                selected_window->scale = selected_window->originalScale;
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
                selected_window->scale = scale;
                printf("Scale: %f\n", selected_window->scale);
            }
        }

        if (selected_window != NULL) {
            MyWindow w = *selected_window;
            XImage *image = XGetRGBImage(display, w.window, 0, 0, w.attr->width, w.attr->height);
            MyUpdateTexture(image, w.texture);
            XDestroyImage(image);
        }

        BeginDrawing();

        ClearBackground(RAYWHITE);

        BeginMode3D(camera);

        DrawGrid(10, 1.0f);

        // draw collision box
        if (collision.hit) {
            DrawCube(collision.point, 0.1f, 0.1f, 0.1f, RED);
        }
        DrawRay(ray, GREEN);
        for (int i = 0; i < WIN_COUNT; i++) {
            MyWindow w = windows[i];

            BoundingBox bbox = GetWindowBoundingBox(&w);
            DrawBoundingBox(bbox, BLUE);
        }

        // rlPushMatrix();
        // rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);

        for (int i = 0; i < WIN_COUNT; i++) {
            MyWindow w = windows[i];
            DrawModel(*w.model, w.position, w.scale, WHITE);
            // DrawModelWires(*w.model, w.position, w.scale, BLACK);
            // DrawModelEx(*w.model, w.position, w.rotation, 90.0f, (Vector3){w.scale, w.scale, 1.0f}, WHITE);

            Color color = selected_window != NULL && selected_window->window == w.window ? RED : BLACK;

            float width = w.attr->width * w.scale / 350;
            float height = w.attr->height * w.scale / 350;
            DrawCubeWires(w.position,
                width, 0,
                height, color);
        }

        // rlPopMatrix();

        EndMode3D();

        // display frame rate on screen
        int screenWidth = GetScreenWidth();
        DrawFPS(screenWidth - 80, 10);

        DrawRectangle(10, 10, 200, 50, Fade(SKYBLUE, 0.5f));
        DrawRectangleLines(10, 10, 200, 50, BLUE);

        char *modeText = mode == CameraMovement ? "Camera Movement" : mode == CursorMovement ? "Cursor Movement" : "Scale Object";
        Color modeColor = mode == CameraMovement ? BLUE : mode == CursorMovement ? GREEN : RED;

        DrawText(TextFormat("Mode: %s", modeText), 2, 0, 10, modeColor);
        DrawText("- Press [Space] to change modes", 20, 20, 10, DARKGRAY);
        DrawText("- Press [Escape] to exit", 20, 40, 10, DARKGRAY);

        // end the frame and get ready for the next one  (display frame, poll input, etc...)
        EndDrawing();
    }

    // cleanup
    for (int i = 0; i < WIN_COUNT; i++) {
        MyWindow w = windows[i];
        UnloadModel(*w.model);
        XFree(w.attr);
    }
    XCloseDisplay(display);
    CloseWindow();
    return 0;
}
