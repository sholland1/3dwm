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
    ScaleWindow,
    MoveWindowZ,
    MoveWindowXY,
} ControlMode;

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

typedef struct MyWindow {
    Window window;
    XWindowAttributes *attr;
    Model *model;
    Texture *texture;
    bool visible;
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
    windows[1].window = 0x2a00003;

    for (int i = 0; i < WIN_COUNT; i++) {
        MyWindow *w = &windows[i];
        w->attr = (XWindowAttributes *)malloc(sizeof(XWindowAttributes));
        if (XGetWindowAttributes(display, w->window, w->attr) == 0) {
            fprintf(stderr, "Unable to get window attributes\n");
            XCloseDisplay(display);
            return 1;
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

        w->visible = true;
    }

    windows[0].model->transform = LookAtTarget(MatrixTranslate(0, 3.25f, -0.8), camera.position);
    windows[1].model->transform = LookAtTarget(MatrixTranslate(2, 2.25f, -1), camera.position);

    MyWindow *selected_window = &windows[0];
    ControlMode mode = CursorMovement;

    // disable the escape key
    SetExitKey(-1);

    Ray ray = {0};
    RayCollision collision = { 0 };
    Vector2 originalMousePosition = {0};
    Matrix originalTransform = {0};

    float rotationAngle = 0.0f;
    Vector3 nextNormal = {0};

    // game loop
    while (!WindowShouldClose()) {
        if (mode == CameraMovement) {
            MyUpdateCamera(&camera);
            if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                mode = CursorMovement;
                EnableCursor();
            }
            else if (IsKeyPressed(KEY_H)) {
                for (int i = 0; i < WIN_COUNT; i++) {
                    windows[i].visible = !windows[i].visible;
                }
            }
            else {
        // if (IsKeyPressed(KEY_TAB)) {
            for (int i = 0; i < WIN_COUNT; i++) {
                MyWindow *w = &windows[i];
                w->model->transform = LookAtTarget(w->model->transform, camera.position);
            }
        // }
            }
        }
        else if (mode == CursorMovement) {
            if (IsKeyPressed(KEY_SPACE)) {
                mode = CameraMovement;
                DisableCursor();
            }
            else if (IsKeyPressed(KEY_H)) {
                for (int i = 0; i < WIN_COUNT; i++) {
                    windows[i].visible = !windows[i].visible;
                }
            }
            else if (selected_window != NULL && IsKeyPressed(KEY_S)) {
                mode = ScaleWindow;
                originalTransform = selected_window->model->transform;
            }
            else if (selected_window != NULL && IsKeyPressed(KEY_Z)) {
                mode = MoveWindowZ;
                originalTransform = selected_window->model->transform;
                // camera.target = selected_window->position;
                originalMousePosition = GetMousePosition();
            }
            else if (selected_window != NULL && IsKeyPressed(KEY_G)) {
                mode = MoveWindowXY;
                originalTransform = selected_window->model->transform;
                originalMousePosition = GetMousePosition();
            }
            else {//if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                ray = GetScreenToWorldRay(GetMousePosition(), camera);

                collision.distance = 1000000.0f;
                collision.hit = false;
                RayCollision tempCollision = {0};
                // Check collision between ray and box
                for (int i = 0; i < WIN_COUNT; i++) {
                    MyWindow *w = &windows[i];
                    tempCollision = GetRayCollisionMesh(ray, w->model->meshes[0], w->model->transform);
                    if (tempCollision.hit && tempCollision.distance <= collision.distance) {
                        collision = tempCollision;
                        selected_window = w;
                    }
                }
            }
        }
        else if (mode == ScaleWindow) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                mode = CursorMovement;
            }
            else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_CAPS_LOCK)) {
                mode = CursorMovement;
                selected_window->model->transform = originalTransform;
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
                selected_window->model->transform = MatrixMultiply(scaleMat, originalTransform);
            }
        }
        else if (mode == MoveWindowZ) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                mode = CursorMovement;
            }
            else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_CAPS_LOCK)) {
                mode = CursorMovement;
                selected_window->model->transform = originalTransform;
            }
            else {
                // move window toward the camera when mouse is above center
                // move window away from the camera when mouse is below center

                Vector3 pos = {originalTransform.m12, originalTransform.m13, originalTransform.m14};
                Vector3 moveDirection = Vector3Subtract(pos, camera.position);
                float scalar = (originalMousePosition.y - GetMouseY()) / 60.0f;
                Vector3 moveVector = Vector3Scale(moveDirection, scalar);

                Matrix m = MatrixTranslate(moveVector.x, moveVector.y, moveVector.z);
                selected_window->model->transform = MatrixMultiply(originalTransform, m);
            }
        }
        else if (mode == MoveWindowXY) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                mode = CursorMovement;
            }
            else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_CAPS_LOCK)) {
                mode = CursorMovement;
                selected_window->model->transform = originalTransform;
            }
            else {
                Vector2 mousePosition = GetMousePosition();
                Vector3 pos = {originalTransform.m12, originalTransform.m13, originalTransform.m14};
                Vector3 directionOfWindow = Vector3Subtract(pos, camera.position);

                Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));

                Vector3 moveDirectionX = Vector3RotateByAxisAngle(directionOfWindow, camera.up, (originalMousePosition.x - mousePosition.x) / 800.0f);
                Vector3 moveDirection = Vector3RotateByAxisAngle(moveDirectionX, right, (originalMousePosition.y - mousePosition.y) / 800.0f);

                Vector3 moveVector = Vector3Subtract(moveDirection, directionOfWindow);

                Matrix m = MatrixTranslate(pos.x + moveVector.x, pos.y + moveVector.y, pos.z + moveVector.z);
                selected_window->model->transform = LookAtTarget(m, camera.position);
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
            MyWindow *w = &windows[i];
            if (!w->visible) break;
            DrawModel(*w->model, ORIGIN, 1.0f, WHITE);

            Color color = selected_window != NULL && selected_window->window == w->window ? RED : BLACK;
            DrawWindowBorder(w, color);

            if (selected_window->window == w->window)
                DrawWindowNormal(w, GREEN);
        }

        EndMode3D();

        // display frame rate on screen
        int screenWidth = GetScreenWidth();
        DrawFPS(screenWidth - 80, 10);

        DrawRectangle(10, 10, 200, 50, Fade(SKYBLUE, 0.5f));
        DrawRectangleLines(10, 10, 200, 50, BLUE);

        const char *modeText = GetModeText(mode);
        Color modeColor = GetModeColor(mode);

        DrawText(TextFormat("Mode: %s", modeText), 2, 0, 10, modeColor);
        DrawText("- Press [Space] to change modes", 20, 20, 10, DARKGRAY);
        DrawText("- Press [Escape] to exit", 20, 40, 10, DARKGRAY);

        // end the frame and get ready for the next one  (display frame, poll input, etc...)
        EndDrawing();
    }

    // cleanup
    for (int i = 0; i < WIN_COUNT; i++) {
        MyWindow *w = &windows[i];
        UnloadModel(*w->model);
        XFree(w->attr);
    }
    XCloseDisplay(display);
    CloseWindow();
    return 0;
}
