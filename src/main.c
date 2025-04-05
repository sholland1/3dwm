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

const Vector3 ORIGIN = {0.0f, 0.0f, 0.0f};

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
    struct MyWindow *next;
} MyWindow;

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

    MyWindow windows[2] = {0};
    windows[0].window = 0x1e0002c;
    windows[1].window = 0x2a00003;

    for (int i = 0; i < 2; i++) {
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

    windows[0].next = &windows[1];
    windows[1].next = &windows[0];

    MyWindow *selected_window = &windows[0];

    // disable the escape key
    SetExitKey(-1);

    // game loop
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_N)) {
            selected_window = selected_window->next;
        }

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
            else if (IsKeyPressed(KEY_S)) {
                mode = ScaleObject;
                selected_window->originalScale = selected_window->scale;
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

        XImage *image = XGetRGBImage(display, selected_window->window, 0, 0, selected_window->attr->width, selected_window->attr->height);
        MyUpdateTexture(image, selected_window->texture);
        XDestroyImage(image);

        BeginDrawing();

        ClearBackground(RAYWHITE);

        BeginMode3D(camera);

        DrawGrid(10, 1.0f);

        rlPushMatrix();

        rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);

        Vector3 modelPosition = {0, 3.25f, -1};
        DrawModel(*selected_window->model, modelPosition, selected_window->scale, WHITE);

        rlPopMatrix();

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
    for (int i = 0; i < 2; i++) {
        MyWindow w = windows[i];
        UnloadModel(*w.model);
        XFree(w.attr);
    }
    XCloseDisplay(display);
    CloseWindow();
    return 0;
}
