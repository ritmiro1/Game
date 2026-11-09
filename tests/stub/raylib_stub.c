// Автоматически сгенерированные заглушки raylib для безэкранных тестов.
#include "raylib.h"
#include "rlgl.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_frames = 0;
void InitWindow(int width, int height, const char *title) { ((void)0); }
void CloseWindow(void) { ((void)0); }
bool WindowShouldClose(void) { return g_frames > 900; }
bool IsWindowReady(void) { return true; }
bool IsWindowFullscreen(void) { return (bool){ 0 }; }
bool IsWindowHidden(void) { return (bool){ 0 }; }
bool IsWindowMinimized(void) { return (bool){ 0 }; }
bool IsWindowMaximized(void) { return (bool){ 0 }; }
bool IsWindowFocused(void) { return (bool){ 0 }; }
bool IsWindowResized(void) { return false; }
bool IsWindowState(unsigned int flag) { return (bool){ 0 }; }
void SetWindowState(unsigned int flags) { ((void)0); }
void ClearWindowState(unsigned int flags) { ((void)0); }
void ToggleFullscreen(void) { ((void)0); }
void ToggleBorderlessWindowed(void) { ((void)0); }
void MaximizeWindow(void) { ((void)0); }
void MinimizeWindow(void) { ((void)0); }
void RestoreWindow(void) { ((void)0); }
void SetWindowIcon(Image image) { ((void)0); }
void SetWindowIcons(Image *images, int count) { ((void)0); }
void SetWindowTitle(const char *title) { ((void)0); }
void SetWindowPosition(int x, int y) { ((void)0); }
void SetWindowMonitor(int monitor) { ((void)0); }
void SetWindowMinSize(int width, int height) { ((void)0); }
void SetWindowMaxSize(int width, int height) { ((void)0); }
void SetWindowSize(int width, int height) { ((void)0); }
void SetWindowOpacity(float opacity) { ((void)0); }
void SetWindowFocused(void) { ((void)0); }
void * GetWindowHandle(void) { return (void *){ 0 }; }
int GetScreenWidth(void) { return 1400; }
int GetScreenHeight(void) { return 900; }
int GetRenderWidth(void) { return (int){ 0 }; }
int GetRenderHeight(void) { return (int){ 0 }; }
int GetMonitorCount(void) { return (int){ 0 }; }
int GetCurrentMonitor(void) { return 0; }
Vector2 GetMonitorPosition(int monitor) { return (Vector2){ 0 }; }
int GetMonitorWidth(int monitor) { return 1920; }
int GetMonitorHeight(int monitor) { return 1080; }
int GetMonitorPhysicalWidth(int monitor) { return (int){ 0 }; }
int GetMonitorPhysicalHeight(int monitor) { return (int){ 0 }; }
int GetMonitorRefreshRate(int monitor) { return (int){ 0 }; }
Vector2 GetWindowPosition(void) { return (Vector2){ 0 }; }
Vector2 GetWindowScaleDPI(void) { return (Vector2){ 0 }; }
const char * GetMonitorName(int monitor) { return (const char *){ 0 }; }
void SetClipboardText(const char *text) { ((void)0); }
const char * GetClipboardText(void) { return (const char *){ 0 }; }
Image GetClipboardImage(void) { return (Image){ 0 }; }
void EnableEventWaiting(void) { ((void)0); }
void DisableEventWaiting(void) { ((void)0); }
void ShowCursor(void) { ((void)0); }
void HideCursor(void) { ((void)0); }
bool IsCursorHidden(void) { return false; }
void EnableCursor(void) { ((void)0); }
void DisableCursor(void) { ((void)0); }
bool IsCursorOnScreen(void) { return (bool){ 0 }; }
void ClearBackground(Color color) { ((void)0); }
void BeginDrawing(void) { ((void)0); }
void EndDrawing(void) { ((void)0); }
void BeginMode2D(Camera2D camera) { ((void)0); }
void EndMode2D(void) { ((void)0); }
void BeginMode3D(Camera3D camera) { ((void)0); }
void EndMode3D(void) { ((void)0); }
void BeginTextureMode(RenderTexture2D target) { ((void)0); }
void EndTextureMode(void) { ((void)0); }
void BeginShaderMode(Shader shader) { ((void)0); }
void EndShaderMode(void) { ((void)0); }
void BeginBlendMode(int mode) { ((void)0); }
void EndBlendMode(void) { ((void)0); }
void BeginScissorMode(int x, int y, int width, int height) { ((void)0); }
void EndScissorMode(void) { ((void)0); }
void BeginVrStereoMode(VrStereoConfig config) { ((void)0); }
void EndVrStereoMode(void) { ((void)0); }
VrStereoConfig LoadVrStereoConfig(VrDeviceInfo device) { return (VrStereoConfig){ 0 }; }
void UnloadVrStereoConfig(VrStereoConfig config) { ((void)0); }
Shader LoadShader(const char *vsFileName, const char *fsFileName) { return (Shader){ 0 }; }
Shader LoadShaderFromMemory(const char *vsCode, const char *fsCode) { return (Shader){ 0 }; }
bool IsShaderValid(Shader shader) { return (bool){ 0 }; }
int GetShaderLocation(Shader shader, const char *uniformName) { (void)uniformName; return -1; }
int GetShaderLocationAttrib(Shader shader, const char *attribName) { return -1; }
void SetShaderValue(Shader shader, int locIndex, const void *value, int uniformType) { ((void)0); }
void SetShaderValueV(Shader shader, int locIndex, const void *value, int uniformType, int count) { ((void)0); }
void SetShaderValueMatrix(Shader shader, int locIndex, Matrix mat) { ((void)0); }
void SetShaderValueTexture(Shader shader, int locIndex, Texture2D texture) { ((void)0); }
void UnloadShader(Shader shader) { ((void)0); }
Ray GetScreenToWorldRay(Vector2 position, Camera camera) { return (Ray){ 0 }; }
Ray GetScreenToWorldRayEx(Vector2 position, Camera camera, int width, int height) { return (Ray){ 0 }; }
Vector2 GetWorldToScreen(Vector3 position, Camera camera) { return (Vector2){ 0 }; }
Vector2 GetWorldToScreenEx(Vector3 position, Camera camera, int width, int height) { return (Vector2){ 0 }; }
Vector2 GetWorldToScreen2D(Vector2 position, Camera2D camera) { return (Vector2){ 0 }; }
Vector2 GetScreenToWorld2D(Vector2 position, Camera2D camera) { return (Vector2){ 0 }; }
Matrix GetCameraMatrix(Camera camera) { return (Matrix){ 0 }; }
Matrix GetCameraMatrix2D(Camera2D camera) { return (Matrix){ 0 }; }
void SetTargetFPS(int fps) { ((void)0); }
float GetFrameTime(void) { g_frames++; return 1.0f/60.0f; }
double GetTime(void) { return (double)g_frames/60.0; }
int GetFPS(void) { return (int){ 0 }; }
void SwapScreenBuffer(void) { ((void)0); }
void PollInputEvents(void) { ((void)0); }
void WaitTime(double seconds) { ((void)0); }
void SetRandomSeed(unsigned int seed) { ((void)0); }
int GetRandomValue(int min, int max) { return (int){ 0 }; }
int * LoadRandomSequence(unsigned int count, int min, int max) { return (int *){ 0 }; }
void UnloadRandomSequence(int *sequence) { ((void)0); }
void TakeScreenshot(const char *fileName) { ((void)0); }
void SetConfigFlags(unsigned int flags) { ((void)0); }
void OpenURL(const char *url) { ((void)0); }
void TraceLog(int logLevel, const char *text, ...) { ((void)0); }
void SetTraceLogLevel(int logLevel) { ((void)0); }
void * MemAlloc(unsigned int size) { return (void *){ 0 }; }
void * MemRealloc(void *ptr, unsigned int size) { return (void *){ 0 }; }
void MemFree(void *ptr) { ((void)0); }
void SetTraceLogCallback(TraceLogCallback callback) { ((void)0); }
void SetLoadFileDataCallback(LoadFileDataCallback callback) { ((void)0); }
void SetSaveFileDataCallback(SaveFileDataCallback callback) { ((void)0); }
void SetLoadFileTextCallback(LoadFileTextCallback callback) { ((void)0); }
void SetSaveFileTextCallback(SaveFileTextCallback callback) { ((void)0); }
unsigned char * LoadFileData(const char *fileName, int *dataSize) { return (unsigned char *){ 0 }; }
void UnloadFileData(unsigned char *data) { ((void)0); }
bool SaveFileData(const char *fileName, void *data, int dataSize) { return (bool){ 0 }; }
bool ExportDataAsCode(const unsigned char *data, int dataSize, const char *fileName) { return (bool){ 0 }; }
char * LoadFileText(const char *fileName) { return (char *){ 0 }; }
void UnloadFileText(char *text) { ((void)0); }
bool SaveFileText(const char *fileName, char *text) { return (bool){ 0 }; }
bool FileExists(const char *fileName) { return (bool){ 0 }; }
bool DirectoryExists(const char *dirPath) { return (bool){ 0 }; }
bool IsFileExtension(const char *fileName, const char *ext) { return (bool){ 0 }; }
int GetFileLength(const char *fileName) { return (int){ 0 }; }
const char * GetFileExtension(const char *fileName) { return (const char *){ 0 }; }
const char * GetFileName(const char *filePath) { return (const char *){ 0 }; }
const char * GetFileNameWithoutExt(const char *filePath) { return (const char *){ 0 }; }
const char * GetDirectoryPath(const char *filePath) { return (const char *){ 0 }; }
const char * GetPrevDirectoryPath(const char *dirPath) { return (const char *){ 0 }; }
const char * GetWorkingDirectory(void) { return (const char *){ 0 }; }
const char * GetApplicationDirectory(void) { return "./"; }
int MakeDirectory(const char *dirPath) { return (int){ 0 }; }
bool ChangeDirectory(const char *dir) { return (bool){ 0 }; }
bool IsPathFile(const char *path) { return (bool){ 0 }; }
bool IsFileNameValid(const char *fileName) { return (bool){ 0 }; }
FilePathList LoadDirectoryFiles(const char *dirPath) { return (FilePathList){ 0 }; }
FilePathList LoadDirectoryFilesEx(const char *basePath, const char *filter, bool scanSubdirs) { return (FilePathList){ 0 }; }
void UnloadDirectoryFiles(FilePathList files) { ((void)0); }
bool IsFileDropped(void) { return (bool){ 0 }; }
FilePathList LoadDroppedFiles(void) { return (FilePathList){ 0 }; }
void UnloadDroppedFiles(FilePathList files) { ((void)0); }
long GetFileModTime(const char *fileName) { return (long){ 0 }; }
unsigned char * CompressData(const unsigned char *data, int dataSize, int *compDataSize) { return (unsigned char *){ 0 }; }
unsigned char * DecompressData(const unsigned char *compData, int compDataSize, int *dataSize) { return (unsigned char *){ 0 }; }
char * EncodeDataBase64(const unsigned char *data, int dataSize, int *outputSize) { return (char *){ 0 }; }
unsigned char * DecodeDataBase64(const unsigned char *data, int *outputSize) { return (unsigned char *){ 0 }; }
unsigned int ComputeCRC32(unsigned char *data, int dataSize) { return (unsigned int){ 0 }; }
unsigned int * ComputeMD5(unsigned char *data, int dataSize) { return (unsigned int *){ 0 }; }
unsigned int * ComputeSHA1(unsigned char *data, int dataSize) { return (unsigned int *){ 0 }; }
AutomationEventList LoadAutomationEventList(const char *fileName) { return (AutomationEventList){ 0 }; }
void UnloadAutomationEventList(AutomationEventList list) { ((void)0); }
bool ExportAutomationEventList(AutomationEventList list, const char *fileName) { return (bool){ 0 }; }
void SetAutomationEventList(AutomationEventList *list) { ((void)0); }
void SetAutomationEventBaseFrame(int frame) { ((void)0); }
void StartAutomationEventRecording(void) { ((void)0); }
void StopAutomationEventRecording(void) { ((void)0); }
void PlayAutomationEvent(AutomationEvent event) { ((void)0); }
bool IsKeyPressed(int key) { return (bool){ 0 }; }
bool IsKeyPressedRepeat(int key) { return (bool){ 0 }; }
bool IsKeyDown(int key) { return (bool){ 0 }; }
bool IsKeyReleased(int key) { return (bool){ 0 }; }
bool IsKeyUp(int key) { return (bool){ 0 }; }
int GetKeyPressed(void) { return (int){ 0 }; }
int GetCharPressed(void) { return (int){ 0 }; }
void SetExitKey(int key) { ((void)0); }
bool IsGamepadAvailable(int gamepad) { return (bool){ 0 }; }
const char * GetGamepadName(int gamepad) { return (const char *){ 0 }; }
bool IsGamepadButtonPressed(int gamepad, int button) { return (bool){ 0 }; }
bool IsGamepadButtonDown(int gamepad, int button) { return (bool){ 0 }; }
bool IsGamepadButtonReleased(int gamepad, int button) { return (bool){ 0 }; }
bool IsGamepadButtonUp(int gamepad, int button) { return (bool){ 0 }; }
int GetGamepadButtonPressed(void) { return (int){ 0 }; }
int GetGamepadAxisCount(int gamepad) { return (int){ 0 }; }
float GetGamepadAxisMovement(int gamepad, int axis) { return (float){ 0 }; }
int SetGamepadMappings(const char *mappings) { return (int){ 0 }; }
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration) { ((void)0); }
bool IsMouseButtonPressed(int button) { return (bool){ 0 }; }
bool IsMouseButtonDown(int button) { return (bool){ 0 }; }
bool IsMouseButtonReleased(int button) { return (bool){ 0 }; }
bool IsMouseButtonUp(int button) { return (bool){ 0 }; }
int GetMouseX(void) { return (int){ 0 }; }
int GetMouseY(void) { return (int){ 0 }; }
Vector2 GetMousePosition(void) { return (Vector2){ 60.0f, 60.0f }; }
Vector2 GetMouseDelta(void) { return (Vector2){ 0.0f, 0.0f }; }
void SetMousePosition(int x, int y) { ((void)0); }
void SetMouseOffset(int offsetX, int offsetY) { ((void)0); }
void SetMouseScale(float scaleX, float scaleY) { ((void)0); }
float GetMouseWheelMove(void) { return 0.0f; }
Vector2 GetMouseWheelMoveV(void) { return (Vector2){ 0 }; }
void SetMouseCursor(int cursor) { ((void)0); }
int GetTouchX(void) { return (int){ 0 }; }
int GetTouchY(void) { return (int){ 0 }; }
Vector2 GetTouchPosition(int index) { return (Vector2){ 0 }; }
int GetTouchPointId(int index) { return (int){ 0 }; }
int GetTouchPointCount(void) { return (int){ 0 }; }
void SetGesturesEnabled(unsigned int flags) { ((void)0); }
bool IsGestureDetected(unsigned int gesture) { return (bool){ 0 }; }
int GetGestureDetected(void) { return (int){ 0 }; }
float GetGestureHoldDuration(void) { return (float){ 0 }; }
Vector2 GetGestureDragVector(void) { return (Vector2){ 0 }; }
float GetGestureDragAngle(void) { return (float){ 0 }; }
Vector2 GetGesturePinchVector(void) { return (Vector2){ 0 }; }
float GetGesturePinchAngle(void) { return (float){ 0 }; }
void UpdateCamera(Camera *camera, int mode) { ((void)0); }
void UpdateCameraPro(Camera *camera, Vector3 movement, Vector3 rotation, float zoom) { ((void)0); }
void SetShapesTexture(Texture2D texture, Rectangle source) { ((void)0); }
Texture2D GetShapesTexture(void) { return (Texture2D){ 0 }; }
Rectangle GetShapesTextureRectangle(void) { return (Rectangle){ 0 }; }
void DrawPixel(int posX, int posY, Color color) { ((void)0); }
void DrawPixelV(Vector2 position, Color color) { ((void)0); }
void DrawLine(int startPosX, int startPosY, int endPosX, int endPosY, Color color) { ((void)0); }
void DrawLineV(Vector2 startPos, Vector2 endPos, Color color) { ((void)0); }
void DrawLineEx(Vector2 startPos, Vector2 endPos, float thick, Color color) { ((void)0); }
void DrawLineStrip(const Vector2 *points, int pointCount, Color color) { ((void)0); }
void DrawLineBezier(Vector2 startPos, Vector2 endPos, float thick, Color color) { ((void)0); }
void DrawCircle(int centerX, int centerY, float radius, Color color) { ((void)0); }
void DrawCircleSector(Vector2 center, float radius, float startAngle, float endAngle, int segments, Color color) { ((void)0); }
void DrawCircleSectorLines(Vector2 center, float radius, float startAngle, float endAngle, int segments, Color color) { ((void)0); }
void DrawCircleGradient(int centerX, int centerY, float radius, Color inner, Color outer) { ((void)0); }
void DrawCircleV(Vector2 center, float radius, Color color) { ((void)0); }
void DrawCircleLines(int centerX, int centerY, float radius, Color color) { ((void)0); }
void DrawCircleLinesV(Vector2 center, float radius, Color color) { ((void)0); }
void DrawEllipse(int centerX, int centerY, float radiusH, float radiusV, Color color) { ((void)0); }
void DrawEllipseLines(int centerX, int centerY, float radiusH, float radiusV, Color color) { ((void)0); }
void DrawRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color) { ((void)0); }
void DrawRingLines(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color) { ((void)0); }
void DrawRectangle(int posX, int posY, int width, int height, Color color) { ((void)0); }
void DrawRectangleV(Vector2 position, Vector2 size, Color color) { ((void)0); }
void DrawRectangleRec(Rectangle rec, Color color) { ((void)0); }
void DrawRectanglePro(Rectangle rec, Vector2 origin, float rotation, Color color) { ((void)0); }
void DrawRectangleGradientV(int posX, int posY, int width, int height, Color top, Color bottom) { ((void)0); }
void DrawRectangleGradientH(int posX, int posY, int width, int height, Color left, Color right) { ((void)0); }
void DrawRectangleGradientEx(Rectangle rec, Color topLeft, Color bottomLeft, Color topRight, Color bottomRight) { ((void)0); }
void DrawRectangleLines(int posX, int posY, int width, int height, Color color) { ((void)0); }
void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color) { ((void)0); }
void DrawRectangleRounded(Rectangle rec, float roundness, int segments, Color color) { ((void)0); }
void DrawRectangleRoundedLines(Rectangle rec, float roundness, int segments, Color color) { ((void)0); }
void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness, int segments, float lineThick, Color color) { ((void)0); }
void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) { ((void)0); }
void DrawTriangleLines(Vector2 v1, Vector2 v2, Vector2 v3, Color color) { ((void)0); }
void DrawTriangleFan(const Vector2 *points, int pointCount, Color color) { ((void)0); }
void DrawTriangleStrip(const Vector2 *points, int pointCount, Color color) { ((void)0); }
void DrawPoly(Vector2 center, int sides, float radius, float rotation, Color color) { ((void)0); }
void DrawPolyLines(Vector2 center, int sides, float radius, float rotation, Color color) { ((void)0); }
void DrawPolyLinesEx(Vector2 center, int sides, float radius, float rotation, float lineThick, Color color) { ((void)0); }
void DrawSplineLinear(const Vector2 *points, int pointCount, float thick, Color color) { ((void)0); }
void DrawSplineBasis(const Vector2 *points, int pointCount, float thick, Color color) { ((void)0); }
void DrawSplineCatmullRom(const Vector2 *points, int pointCount, float thick, Color color) { ((void)0); }
void DrawSplineBezierQuadratic(const Vector2 *points, int pointCount, float thick, Color color) { ((void)0); }
void DrawSplineBezierCubic(const Vector2 *points, int pointCount, float thick, Color color) { ((void)0); }
void DrawSplineSegmentLinear(Vector2 p1, Vector2 p2, float thick, Color color) { ((void)0); }
void DrawSplineSegmentBasis(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float thick, Color color) { ((void)0); }
void DrawSplineSegmentCatmullRom(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float thick, Color color) { ((void)0); }
void DrawSplineSegmentBezierQuadratic(Vector2 p1, Vector2 c2, Vector2 p3, float thick, Color color) { ((void)0); }
void DrawSplineSegmentBezierCubic(Vector2 p1, Vector2 c2, Vector2 c3, Vector2 p4, float thick, Color color) { ((void)0); }
Vector2 GetSplinePointLinear(Vector2 startPos, Vector2 endPos, float t) { return (Vector2){ 0 }; }
Vector2 GetSplinePointBasis(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float t) { return (Vector2){ 0 }; }
Vector2 GetSplinePointCatmullRom(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float t) { return (Vector2){ 0 }; }
Vector2 GetSplinePointBezierQuad(Vector2 p1, Vector2 c2, Vector2 p3, float t) { return (Vector2){ 0 }; }
Vector2 GetSplinePointBezierCubic(Vector2 p1, Vector2 c2, Vector2 c3, Vector2 p4, float t) { return (Vector2){ 0 }; }
bool CheckCollisionRecs(Rectangle rec1, Rectangle rec2) { return (bool){ 0 }; }
bool CheckCollisionCircles(Vector2 center1, float radius1, Vector2 center2, float radius2) { return (bool){ 0 }; }
bool CheckCollisionCircleRec(Vector2 center, float radius, Rectangle rec) { return (bool){ 0 }; }
bool CheckCollisionCircleLine(Vector2 center, float radius, Vector2 p1, Vector2 p2) { return (bool){ 0 }; }
bool CheckCollisionPointRec(Vector2 point, Rectangle rec) { return (bool){ 0 }; }
bool CheckCollisionPointCircle(Vector2 point, Vector2 center, float radius) { return (bool){ 0 }; }
bool CheckCollisionPointTriangle(Vector2 point, Vector2 p1, Vector2 p2, Vector2 p3) { return (bool){ 0 }; }
bool CheckCollisionPointLine(Vector2 point, Vector2 p1, Vector2 p2, int threshold) { return (bool){ 0 }; }
bool CheckCollisionPointPoly(Vector2 point, const Vector2 *points, int pointCount) { return (bool){ 0 }; }
bool CheckCollisionLines(Vector2 startPos1, Vector2 endPos1, Vector2 startPos2, Vector2 endPos2, Vector2 *collisionPoint) { return (bool){ 0 }; }
Rectangle GetCollisionRec(Rectangle rec1, Rectangle rec2) { return (Rectangle){ 0 }; }
Image LoadImage(const char *fileName) { return (Image){ 0 }; }
Image LoadImageRaw(const char *fileName, int width, int height, int format, int headerSize) { return (Image){ 0 }; }
Image LoadImageAnim(const char *fileName, int *frames) { return (Image){ 0 }; }
Image LoadImageAnimFromMemory(const char *fileType, const unsigned char *fileData, int dataSize, int *frames) { return (Image){ 0 }; }
Image LoadImageFromMemory(const char *fileType, const unsigned char *fileData, int dataSize) { return (Image){ 0 }; }
Image LoadImageFromTexture(Texture2D texture) { return (Image){ 0 }; }
Image LoadImageFromScreen(void) { return (Image){ 0 }; }
bool IsImageValid(Image image) { return (bool){ 0 }; }
void UnloadImage(Image image) { ((void)0); }
bool ExportImage(Image image, const char *fileName) { return (bool){ 0 }; }
unsigned char * ExportImageToMemory(Image image, const char *fileType, int *fileSize) { return (unsigned char *){ 0 }; }
bool ExportImageAsCode(Image image, const char *fileName) { return (bool){ 0 }; }
Image GenImageColor(int width, int height, Color color) { return (Image){ 0 }; }
Image GenImageGradientLinear(int width, int height, int direction, Color start, Color end) { return (Image){ 0 }; }
Image GenImageGradientRadial(int width, int height, float density, Color inner, Color outer) { return (Image){ 0 }; }
Image GenImageGradientSquare(int width, int height, float density, Color inner, Color outer) { return (Image){ 0 }; }
Image GenImageChecked(int width, int height, int checksX, int checksY, Color col1, Color col2) { return (Image){ 0 }; }
Image GenImageWhiteNoise(int width, int height, float factor) { return (Image){ 0 }; }
Image GenImagePerlinNoise(int width, int height, int offsetX, int offsetY, float scale) { return (Image){ 0 }; }
Image GenImageCellular(int width, int height, int tileSize) { return (Image){ 0 }; }
Image GenImageText(int width, int height, const char *text) { return (Image){ 0 }; }
Image ImageCopy(Image image) { return (Image){ 0 }; }
Image ImageFromImage(Image image, Rectangle rec) { return (Image){ 0 }; }
Image ImageFromChannel(Image image, int selectedChannel) { return (Image){ 0 }; }
Image ImageText(const char *text, int fontSize, Color color) { return (Image){ 0 }; }
Image ImageTextEx(Font font, const char *text, float fontSize, float spacing, Color tint) { return (Image){ 0 }; }
void ImageFormat(Image *image, int newFormat) { ((void)0); }
void ImageToPOT(Image *image, Color fill) { ((void)0); }
void ImageCrop(Image *image, Rectangle crop) { ((void)0); }
void ImageAlphaCrop(Image *image, float threshold) { ((void)0); }
void ImageAlphaClear(Image *image, Color color, float threshold) { ((void)0); }
void ImageAlphaMask(Image *image, Image alphaMask) { ((void)0); }
void ImageAlphaPremultiply(Image *image) { ((void)0); }
void ImageBlurGaussian(Image *image, int blurSize) { ((void)0); }
void ImageKernelConvolution(Image *image, const float *kernel, int kernelSize) { ((void)0); }
void ImageResize(Image *image, int newWidth, int newHeight) { ((void)0); }
void ImageResizeNN(Image *image, int newWidth,int newHeight) { ((void)0); }
void ImageResizeCanvas(Image *image, int newWidth, int newHeight, int offsetX, int offsetY, Color fill) { ((void)0); }
void ImageMipmaps(Image *image) { ((void)0); }
void ImageDither(Image *image, int rBpp, int gBpp, int bBpp, int aBpp) { ((void)0); }
void ImageFlipVertical(Image *image) { ((void)0); }
void ImageFlipHorizontal(Image *image) { ((void)0); }
void ImageRotate(Image *image, int degrees) { ((void)0); }
void ImageRotateCW(Image *image) { ((void)0); }
void ImageRotateCCW(Image *image) { ((void)0); }
void ImageColorTint(Image *image, Color color) { ((void)0); }
void ImageColorInvert(Image *image) { ((void)0); }
void ImageColorGrayscale(Image *image) { ((void)0); }
void ImageColorContrast(Image *image, float contrast) { ((void)0); }
void ImageColorBrightness(Image *image, int brightness) { ((void)0); }
void ImageColorReplace(Image *image, Color color, Color replace) { ((void)0); }
Color * LoadImageColors(Image image) { return (Color *){ 0 }; }
Color * LoadImagePalette(Image image, int maxPaletteSize, int *colorCount) { return (Color *){ 0 }; }
void UnloadImageColors(Color *colors) { ((void)0); }
void UnloadImagePalette(Color *colors) { ((void)0); }
Rectangle GetImageAlphaBorder(Image image, float threshold) { return (Rectangle){ 0 }; }
Color GetImageColor(Image image, int x, int y) { return (Color){ 0 }; }
void ImageClearBackground(Image *dst, Color color) { ((void)0); }
void ImageDrawPixel(Image *dst, int posX, int posY, Color color) { ((void)0); }
void ImageDrawPixelV(Image *dst, Vector2 position, Color color) { ((void)0); }
void ImageDrawLine(Image *dst, int startPosX, int startPosY, int endPosX, int endPosY, Color color) { ((void)0); }
void ImageDrawLineV(Image *dst, Vector2 start, Vector2 end, Color color) { ((void)0); }
void ImageDrawLineEx(Image *dst, Vector2 start, Vector2 end, int thick, Color color) { ((void)0); }
void ImageDrawCircle(Image *dst, int centerX, int centerY, int radius, Color color) { ((void)0); }
void ImageDrawCircleV(Image *dst, Vector2 center, int radius, Color color) { ((void)0); }
void ImageDrawCircleLines(Image *dst, int centerX, int centerY, int radius, Color color) { ((void)0); }
void ImageDrawCircleLinesV(Image *dst, Vector2 center, int radius, Color color) { ((void)0); }
void ImageDrawRectangle(Image *dst, int posX, int posY, int width, int height, Color color) { ((void)0); }
void ImageDrawRectangleV(Image *dst, Vector2 position, Vector2 size, Color color) { ((void)0); }
void ImageDrawRectangleRec(Image *dst, Rectangle rec, Color color) { ((void)0); }
void ImageDrawRectangleLines(Image *dst, Rectangle rec, int thick, Color color) { ((void)0); }
void ImageDrawTriangle(Image *dst, Vector2 v1, Vector2 v2, Vector2 v3, Color color) { ((void)0); }
void ImageDrawTriangleEx(Image *dst, Vector2 v1, Vector2 v2, Vector2 v3, Color c1, Color c2, Color c3) { ((void)0); }
void ImageDrawTriangleLines(Image *dst, Vector2 v1, Vector2 v2, Vector2 v3, Color color) { ((void)0); }
void ImageDrawTriangleFan(Image *dst, Vector2 *points, int pointCount, Color color) { ((void)0); }
void ImageDrawTriangleStrip(Image *dst, Vector2 *points, int pointCount, Color color) { ((void)0); }
void ImageDraw(Image *dst, Image src, Rectangle srcRec, Rectangle dstRec, Color tint) { ((void)0); }
void ImageDrawText(Image *dst, const char *text, int posX, int posY, int fontSize, Color color) { ((void)0); }
void ImageDrawTextEx(Image *dst, Font font, const char *text, Vector2 position, float fontSize, float spacing, Color tint) { ((void)0); }
Texture2D LoadTexture(const char *fileName) { return (Texture2D){ 0 }; }
Texture2D LoadTextureFromImage(Image image) { return (Texture2D){ 0 }; }
TextureCubemap LoadTextureCubemap(Image image, int layout) { return (TextureCubemap){ 0 }; }
RenderTexture2D LoadRenderTexture(int width, int height) { return (RenderTexture2D){ 0 }; }
bool IsTextureValid(Texture2D texture) { return (bool){ 0 }; }
void UnloadTexture(Texture2D texture) { ((void)0); }
bool IsRenderTextureValid(RenderTexture2D target) { return (bool){ 0 }; }
void UnloadRenderTexture(RenderTexture2D target) { ((void)0); }
void UpdateTexture(Texture2D texture, const void *pixels) { ((void)0); }
void UpdateTextureRec(Texture2D texture, Rectangle rec, const void *pixels) { ((void)0); }
void GenTextureMipmaps(Texture2D *texture) { ((void)0); }
void SetTextureFilter(Texture2D texture, int filter) { ((void)0); }
void SetTextureWrap(Texture2D texture, int wrap) { ((void)0); }
void DrawTexture(Texture2D texture, int posX, int posY, Color tint) { ((void)0); }
void DrawTextureV(Texture2D texture, Vector2 position, Color tint) { ((void)0); }
void DrawTextureEx(Texture2D texture, Vector2 position, float rotation, float scale, Color tint) { ((void)0); }
void DrawTextureRec(Texture2D texture, Rectangle source, Vector2 position, Color tint) { ((void)0); }
void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest, Vector2 origin, float rotation, Color tint) { ((void)0); }
void DrawTextureNPatch(Texture2D texture, NPatchInfo nPatchInfo, Rectangle dest, Vector2 origin, float rotation, Color tint) { ((void)0); }
bool ColorIsEqual(Color col1, Color col2) { return (bool){ 0 }; }
Color Fade(Color color, float alpha) { return (Color){ 0 }; }
int ColorToInt(Color color) { return (int){ 0 }; }
Vector4 ColorNormalize(Color color) { return (Vector4){ 0 }; }
Color ColorFromNormalized(Vector4 normalized) { return (Color){ 0 }; }
Vector3 ColorToHSV(Color color) { return (Vector3){ 0 }; }
Color ColorFromHSV(float hue, float saturation, float value) { return (Color){ 0 }; }
Color ColorTint(Color color, Color tint) { return (Color){ 0 }; }
Color ColorBrightness(Color color, float factor) { return (Color){ 0 }; }
Color ColorContrast(Color color, float contrast) { return (Color){ 0 }; }
Color ColorAlpha(Color color, float alpha) { return (Color){ 0 }; }
Color ColorAlphaBlend(Color dst, Color src, Color tint) { return (Color){ 0 }; }
Color ColorLerp(Color color1, Color color2, float factor) { return (Color){ 0 }; }
Color GetColor(unsigned int hexValue) { return (Color){ 0 }; }
Color GetPixelColor(void *srcPtr, int format) { return (Color){ 0 }; }
void SetPixelColor(void *dstPtr, Color color, int format) { ((void)0); }
int GetPixelDataSize(int width, int height, int format) { return (int){ 0 }; }
Font GetFontDefault(void) { { static Font f; static int init = 0; if (!init) { init = 1; f.baseSize = 12; f.glyphCount = 1200; f.glyphPadding = 0; f.glyphs = (GlyphInfo *)calloc(1200, sizeof(GlyphInfo)); f.recs = (Rectangle *)calloc(1200, sizeof(Rectangle)); for (int i = 0; i < 1200; i++) { f.glyphs[i].value = i; f.glyphs[i].advanceX = 8; f.glyphs[i].offsetX = 0; f.glyphs[i].offsetY = 0; } f.texture.id = 1; f.texture.width = 1; f.texture.height = 1; } return f; } }
Font LoadFont(const char *fileName) { return (Font){ 0 }; }
Font LoadFontEx(const char *fileName, int fontSize, int *codepoints, int codepointCount) { return (Font){ 0 }; }
Font LoadFontFromImage(Image image, Color key, int firstChar) { return (Font){ 0 }; }
Font LoadFontFromMemory(const char *fileType, const unsigned char *fileData, int dataSize, int fontSize, int *codepoints, int codepointCount) { return (Font){ 0 }; }
bool IsFontValid(Font font) { return (bool){ 0 }; }
GlyphInfo * LoadFontData(const unsigned char *fileData, int dataSize, int fontSize, int *codepoints, int codepointCount, int type) { return (GlyphInfo *){ 0 }; }
Image GenImageFontAtlas(const GlyphInfo *glyphs, Rectangle **glyphRecs, int glyphCount, int fontSize, int padding, int packMethod) { return (Image){ 0 }; }
void UnloadFontData(GlyphInfo *glyphs, int glyphCount) { ((void)0); }
void UnloadFont(Font font) { ((void)0); }
bool ExportFontAsCode(Font font, const char *fileName) { return (bool){ 0 }; }
void DrawFPS(int posX, int posY) { ((void)0); }
void DrawText(const char *text, int posX, int posY, int fontSize, Color color) { ((void)0); }
void DrawTextEx(Font font, const char *text, Vector2 position, float fontSize, float spacing, Color tint) { ((void)0); }
void DrawTextPro(Font font, const char *text, Vector2 position, Vector2 origin, float rotation, float fontSize, float spacing, Color tint) { ((void)0); }
void DrawTextCodepoint(Font font, int codepoint, Vector2 position, float fontSize, Color tint) { ((void)0); }
void DrawTextCodepoints(Font font, const int *codepoints, int codepointCount, Vector2 position, float fontSize, float spacing, Color tint) { ((void)0); }
void SetTextLineSpacing(int spacing) { ((void)0); }
int MeasureText(const char *text, int fontSize) { return (int){ 0 }; }
Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing) { return (Vector2){ (float)(text?strlen(text):0) * fontSize * 0.5f, fontSize }; }
int GetGlyphIndex(Font font, int codepoint) { return (int){ 0 }; }
GlyphInfo GetGlyphInfo(Font font, int codepoint) { return (GlyphInfo){ 0 }; }
Rectangle GetGlyphAtlasRec(Font font, int codepoint) { return (Rectangle){ 0 }; }
char * LoadUTF8(const int *codepoints, int length) { return (char *){ 0 }; }
void UnloadUTF8(char *text) { ((void)0); }
int * LoadCodepoints(const char *text, int *count) { return (int *){ 0 }; }
void UnloadCodepoints(int *codepoints) { ((void)0); }
int GetCodepointCount(const char *text) { { int n = 0; int i = 0; while (text && text[i]) { unsigned char c0 = (unsigned char)text[i]; i += (c0 >= 0xF0) ? 4 : (c0 >= 0xE0) ? 3 : (c0 >= 0xC0) ? 2 : 1; n++; } return n; } }
int GetCodepoint(const char *text, int *codepointSize) { { unsigned char c0 = (unsigned char)text[0]; int sz = 1; int cp = c0; if (c0 >= 0xF0) { sz = 4; cp = c0 & 0x07; } else if (c0 >= 0xE0) { sz = 3; cp = c0 & 0x0F; } else if (c0 >= 0xC0) { sz = 2; cp = c0 & 0x1F; } for (int i = 1; i < sz; i++) cp = (cp << 6) | ((unsigned char)text[i] & 0x3F); if (codepointSize) *codepointSize = sz; return cp; } }
int GetCodepointNext(const char *text, int *codepointSize) { { unsigned char c0 = (unsigned char)text[0]; int sz = 1; int cp = c0; if (c0 >= 0xF0) { sz = 4; cp = c0 & 0x07; } else if (c0 >= 0xE0) { sz = 3; cp = c0 & 0x0F; } else if (c0 >= 0xC0) { sz = 2; cp = c0 & 0x1F; } for (int i = 1; i < sz; i++) cp = (cp << 6) | ((unsigned char)text[i] & 0x3F); if (codepointSize) *codepointSize = sz; return cp; } }
int GetCodepointPrevious(const char *text, int *codepointSize) { return (int){ 0 }; }
const char * CodepointToUTF8(int codepoint, int *utf8Size) { { static char b[8]; int n = 0; unsigned int cp = (unsigned int)codepoint; if (cp < 0x80) b[n++] = (char)cp; else if (cp < 0x800) { b[n++] = (char)(0xC0 | (cp >> 6)); b[n++] = (char)(0x80 | (cp & 0x3F)); } else if (cp < 0x10000) { b[n++] = (char)(0xE0 | (cp >> 12)); b[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[n++] = (char)(0x80 | (cp & 0x3F)); } else { b[n++] = (char)(0xF0 | (cp >> 18)); b[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); b[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[n++] = (char)(0x80 | (cp & 0x3F)); } if (utf8Size) *utf8Size = n; b[n] = 0; return b; } }
int TextCopy(char *dst, const char *src) { return (int){ 0 }; }
bool TextIsEqual(const char *text1, const char *text2) { return (bool){ 0 }; }
unsigned int TextLength(const char *text) { return (unsigned int){ 0 }; }
const char * TextFormat(const char *text, ...) { return (const char *){ 0 }; }
const char * TextSubtext(const char *text, int position, int length) { { static char b[4096]; int n = (int)strlen(text); if (position >= n) position = n; int len = (length < 0) ? (n - position) : length; if (len > 4095) len = 4095; memcpy(b, text + position, len); b[len] = 0; return b; } }
char * TextReplace(const char *text, const char *replace, const char *by) { return (char *){ 0 }; }
char * TextInsert(const char *text, const char *insert, int position) { return (char *){ 0 }; }
const char * TextJoin(const char **textList, int count, const char *delimiter) { return (const char *){ 0 }; }
const char ** TextSplit(const char *text, char delimiter, int *count) { return (const char **){ 0 }; }
void TextAppend(char *text, const char *append, int *position) { ((void)0); }
int TextFindIndex(const char *text, const char *find) { return (int){ 0 }; }
const char * TextToUpper(const char *text) { return (const char *){ 0 }; }
const char * TextToLower(const char *text) { return (const char *){ 0 }; }
const char * TextToPascal(const char *text) { return (const char *){ 0 }; }
const char * TextToSnake(const char *text) { return (const char *){ 0 }; }
const char * TextToCamel(const char *text) { return (const char *){ 0 }; }
int TextToInteger(const char *text) { return (int){ 0 }; }
float TextToFloat(const char *text) { return (float){ 0 }; }
void DrawLine3D(Vector3 startPos, Vector3 endPos, Color color) { ((void)0); }
void DrawPoint3D(Vector3 position, Color color) { ((void)0); }
void DrawCircle3D(Vector3 center, float radius, Vector3 rotationAxis, float rotationAngle, Color color) { ((void)0); }
void DrawTriangle3D(Vector3 v1, Vector3 v2, Vector3 v3, Color color) { ((void)0); }
void DrawTriangleStrip3D(const Vector3 *points, int pointCount, Color color) { ((void)0); }
void DrawCube(Vector3 position, float width, float height, float length, Color color) { ((void)0); }
void DrawCubeV(Vector3 position, Vector3 size, Color color) { ((void)0); }
void DrawCubeWires(Vector3 position, float width, float height, float length, Color color) { ((void)0); }
void DrawCubeWiresV(Vector3 position, Vector3 size, Color color) { ((void)0); }
void DrawSphere(Vector3 centerPos, float radius, Color color) { ((void)0); }
void DrawSphereEx(Vector3 centerPos, float radius, int rings, int slices, Color color) { ((void)0); }
void DrawSphereWires(Vector3 centerPos, float radius, int rings, int slices, Color color) { ((void)0); }
void DrawCylinder(Vector3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) { ((void)0); }
void DrawCylinderEx(Vector3 startPos, Vector3 endPos, float startRadius, float endRadius, int sides, Color color) { ((void)0); }
void DrawCylinderWires(Vector3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) { ((void)0); }
void DrawCylinderWiresEx(Vector3 startPos, Vector3 endPos, float startRadius, float endRadius, int sides, Color color) { ((void)0); }
void DrawCapsule(Vector3 startPos, Vector3 endPos, float radius, int slices, int rings, Color color) { ((void)0); }
void DrawCapsuleWires(Vector3 startPos, Vector3 endPos, float radius, int slices, int rings, Color color) { ((void)0); }
void DrawPlane(Vector3 centerPos, Vector2 size, Color color) { ((void)0); }
void DrawRay(Ray ray, Color color) { ((void)0); }
void DrawGrid(int slices, float spacing) { ((void)0); }
Model LoadModel(const char *fileName) { return (Model){ 0 }; }
Model LoadModelFromMesh(Mesh mesh) { return (Model){ 0 }; }
bool IsModelValid(Model model) { return (bool){ 0 }; }
void UnloadModel(Model model) { ((void)0); }
BoundingBox GetModelBoundingBox(Model model) { return (BoundingBox){ 0 }; }
void DrawModel(Model model, Vector3 position, float scale, Color tint) { ((void)0); }
void DrawModelEx(Model model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint) { ((void)0); }
void DrawModelWires(Model model, Vector3 position, float scale, Color tint) { ((void)0); }
void DrawModelWiresEx(Model model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint) { ((void)0); }
void DrawModelPoints(Model model, Vector3 position, float scale, Color tint) { ((void)0); }
void DrawModelPointsEx(Model model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint) { ((void)0); }
void DrawBoundingBox(BoundingBox box, Color color) { ((void)0); }
void DrawBillboard(Camera camera, Texture2D texture, Vector3 position, float scale, Color tint) { ((void)0); }
void DrawBillboardRec(Camera camera, Texture2D texture, Rectangle source, Vector3 position, Vector2 size, Color tint) { ((void)0); }
void DrawBillboardPro(Camera camera, Texture2D texture, Rectangle source, Vector3 position, Vector3 up, Vector2 size, Vector2 origin, float rotation, Color tint) { ((void)0); }
void UploadMesh(Mesh *mesh, bool dynamic) { ((void)0); }
void UpdateMeshBuffer(Mesh mesh, int index, const void *data, int dataSize, int offset) { ((void)0); }
void UnloadMesh(Mesh mesh) { ((void)0); }
void DrawMesh(Mesh mesh, Material material, Matrix transform) { ((void)0); }
void DrawMeshInstanced(Mesh mesh, Material material, const Matrix *transforms, int instances) { ((void)0); }
BoundingBox GetMeshBoundingBox(Mesh mesh) { return (BoundingBox){ 0 }; }
void GenMeshTangents(Mesh *mesh) { ((void)0); }
bool ExportMesh(Mesh mesh, const char *fileName) { return (bool){ 0 }; }
bool ExportMeshAsCode(Mesh mesh, const char *fileName) { return (bool){ 0 }; }
Mesh GenMeshPoly(int sides, float radius) { return (Mesh){ 0 }; }
Mesh GenMeshPlane(float width, float length, int resX, int resZ) { return (Mesh){ 0 }; }
Mesh GenMeshCube(float width, float height, float length) { return (Mesh){ 0 }; }
Mesh GenMeshSphere(float radius, int rings, int slices) { return (Mesh){ 0 }; }
Mesh GenMeshHemiSphere(float radius, int rings, int slices) { return (Mesh){ 0 }; }
Mesh GenMeshCylinder(float radius, float height, int slices) { return (Mesh){ 0 }; }
Mesh GenMeshCone(float radius, float height, int slices) { return (Mesh){ 0 }; }
Mesh GenMeshTorus(float radius, float size, int radSeg, int sides) { return (Mesh){ 0 }; }
Mesh GenMeshKnot(float radius, float size, int radSeg, int sides) { return (Mesh){ 0 }; }
Mesh GenMeshHeightmap(Image heightmap, Vector3 size) { return (Mesh){ 0 }; }
Mesh GenMeshCubicmap(Image cubicmap, Vector3 cubeSize) { return (Mesh){ 0 }; }
Material * LoadMaterials(const char *fileName, int *materialCount) { return (Material *){ 0 }; }
Material LoadMaterialDefault(void) { { Material m = { 0 }; m.maps = (MaterialMap *)calloc(12, sizeof(MaterialMap)); return m; } }
bool IsMaterialValid(Material material) { return (bool){ 0 }; }
void UnloadMaterial(Material material) { { if (material.maps) free(material.maps); } }
void SetMaterialTexture(Material *material, int mapType, Texture2D texture) { ((void)0); }
void SetModelMeshMaterial(Model *model, int meshId, int materialId) { ((void)0); }
ModelAnimation * LoadModelAnimations(const char *fileName, int *animCount) { return NULL; }
void UpdateModelAnimation(Model model, ModelAnimation anim, int frame) { ((void)0); }
void UpdateModelAnimationBones(Model model, ModelAnimation anim, int frame) { ((void)0); }
void UnloadModelAnimation(ModelAnimation anim) { ((void)0); }
void UnloadModelAnimations(ModelAnimation *animations, int animCount) { ((void)0); }
bool IsModelAnimationValid(Model model, ModelAnimation anim) { return (bool){ 0 }; }
bool CheckCollisionSpheres(Vector3 center1, float radius1, Vector3 center2, float radius2) { return (bool){ 0 }; }
bool CheckCollisionBoxes(BoundingBox box1, BoundingBox box2) { return (bool){ 0 }; }
bool CheckCollisionBoxSphere(BoundingBox box, Vector3 center, float radius) { return (bool){ 0 }; }
RayCollision GetRayCollisionSphere(Ray ray, Vector3 center, float radius) { return (RayCollision){ 0 }; }
RayCollision GetRayCollisionBox(Ray ray, BoundingBox box) { return (RayCollision){ 0 }; }
RayCollision GetRayCollisionMesh(Ray ray, Mesh mesh, Matrix transform) { return (RayCollision){ 0 }; }
RayCollision GetRayCollisionTriangle(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3) { return (RayCollision){ 0 }; }
RayCollision GetRayCollisionQuad(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 p4) { return (RayCollision){ 0 }; }
void InitAudioDevice(void) { ((void)0); }
void CloseAudioDevice(void) { ((void)0); }
bool IsAudioDeviceReady(void) { return true; }
void SetMasterVolume(float volume) { ((void)0); }
float GetMasterVolume(void) { return (float){ 0 }; }
Wave LoadWave(const char *fileName) { return (Wave){ 0 }; }
Wave LoadWaveFromMemory(const char *fileType, const unsigned char *fileData, int dataSize) { return (Wave){ 0 }; }
bool IsWaveValid(Wave wave) { return (bool){ 0 }; }
Sound LoadSound(const char *fileName) { return (Sound){ 0 }; }
Sound LoadSoundFromWave(Wave wave) { return (Sound){ 0 }; }
Sound LoadSoundAlias(Sound source) { return (Sound){ 0 }; }
bool IsSoundValid(Sound sound) { return (bool){ 0 }; }
void UpdateSound(Sound sound, const void *data, int sampleCount) { ((void)0); }
void UnloadWave(Wave wave) { ((void)0); }
void UnloadSound(Sound sound) { ((void)0); }
void UnloadSoundAlias(Sound alias) { ((void)0); }
bool ExportWave(Wave wave, const char *fileName) { return (bool){ 0 }; }
bool ExportWaveAsCode(Wave wave, const char *fileName) { return (bool){ 0 }; }
void PlaySound(Sound sound) { ((void)0); }
void StopSound(Sound sound) { ((void)0); }
void PauseSound(Sound sound) { ((void)0); }
void ResumeSound(Sound sound) { ((void)0); }
bool IsSoundPlaying(Sound sound) { return (bool){ 0 }; }
void SetSoundVolume(Sound sound, float volume) { ((void)0); }
void SetSoundPitch(Sound sound, float pitch) { ((void)0); }
void SetSoundPan(Sound sound, float pan) { ((void)0); }
Wave WaveCopy(Wave wave) { return (Wave){ 0 }; }
void WaveCrop(Wave *wave, int initFrame, int finalFrame) { ((void)0); }
void WaveFormat(Wave *wave, int sampleRate, int sampleSize, int channels) { ((void)0); }
float * LoadWaveSamples(Wave wave) { return (float *){ 0 }; }
void UnloadWaveSamples(float *samples) { ((void)0); }
Music LoadMusicStream(const char *fileName) { return (Music){ 0 }; }
Music LoadMusicStreamFromMemory(const char *fileType, const unsigned char *data, int dataSize) { return (Music){ 0 }; }
bool IsMusicValid(Music music) { return (bool){ 0 }; }
void UnloadMusicStream(Music music) { ((void)0); }
void PlayMusicStream(Music music) { ((void)0); }
bool IsMusicStreamPlaying(Music music) { return (bool){ 0 }; }
void UpdateMusicStream(Music music) { ((void)0); }
void StopMusicStream(Music music) { ((void)0); }
void PauseMusicStream(Music music) { ((void)0); }
void ResumeMusicStream(Music music) { ((void)0); }
void SeekMusicStream(Music music, float position) { ((void)0); }
void SetMusicVolume(Music music, float volume) { ((void)0); }
void SetMusicPitch(Music music, float pitch) { ((void)0); }
void SetMusicPan(Music music, float pan) { ((void)0); }
float GetMusicTimeLength(Music music) { return (float){ 0 }; }
float GetMusicTimePlayed(Music music) { return (float){ 0 }; }
AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int sampleSize, unsigned int channels) { return (AudioStream){ 0 }; }
bool IsAudioStreamValid(AudioStream stream) { return (bool){ 0 }; }
void UnloadAudioStream(AudioStream stream) { ((void)0); }
void UpdateAudioStream(AudioStream stream, const void *data, int frameCount) { ((void)0); }
bool IsAudioStreamProcessed(AudioStream stream) { return (bool){ 0 }; }
void PlayAudioStream(AudioStream stream) { ((void)0); }
void PauseAudioStream(AudioStream stream) { ((void)0); }
void ResumeAudioStream(AudioStream stream) { ((void)0); }
bool IsAudioStreamPlaying(AudioStream stream) { return (bool){ 0 }; }
void StopAudioStream(AudioStream stream) { ((void)0); }
void SetAudioStreamVolume(AudioStream stream, float volume) { ((void)0); }
void SetAudioStreamPitch(AudioStream stream, float pitch) { ((void)0); }
void SetAudioStreamPan(AudioStream stream, float pan) { ((void)0); }
void SetAudioStreamBufferSizeDefault(int size) { ((void)0); }
void SetAudioStreamCallback(AudioStream stream, AudioCallback callback) { ((void)0); }
void AttachAudioStreamProcessor(AudioStream stream, AudioCallback processor) { ((void)0); }
void DetachAudioStreamProcessor(AudioStream stream, AudioCallback processor) { ((void)0); }
void AttachAudioMixedProcessor(AudioCallback processor) { ((void)0); }
void DetachAudioMixedProcessor(AudioCallback processor) { ((void)0); }
void rlMatrixMode(int mode) { ((void)0); }
void rlPushMatrix(void) { ((void)0); }
void rlPopMatrix(void) { ((void)0); }
void rlLoadIdentity(void) { ((void)0); }
void rlTranslatef(float x, float y, float z) { ((void)0); }
void rlRotatef(float angle, float x, float y, float z) { ((void)0); }
void rlScalef(float x, float y, float z) { ((void)0); }
void rlMultMatrixf(const float *matf) { ((void)0); }
void rlFrustum(double left, double right, double bottom, double top, double znear, double zfar) { ((void)0); }
void rlOrtho(double left, double right, double bottom, double top, double znear, double zfar) { ((void)0); }
void rlViewport(int x, int y, int width, int height) { ((void)0); }
void rlSetClipPlanes(double nearPlane, double farPlane) { ((void)0); }
double rlGetCullDistanceNear(void) { return (double){ 0 }; }
double rlGetCullDistanceFar(void) { return (double){ 0 }; }
void rlBegin(int mode) { ((void)0); }
void rlEnd(void) { ((void)0); }
void rlVertex2i(int x, int y) { ((void)0); }
void rlVertex2f(float x, float y) { ((void)0); }
void rlVertex3f(float x, float y, float z) { ((void)0); }
void rlTexCoord2f(float x, float y) { ((void)0); }
void rlNormal3f(float x, float y, float z) { ((void)0); }
void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { ((void)0); }
void rlColor3f(float x, float y, float z) { ((void)0); }
void rlColor4f(float x, float y, float z, float w) { ((void)0); }
bool rlEnableVertexArray(unsigned int vaoId) { return (bool){ 0 }; }
void rlDisableVertexArray(void) { ((void)0); }
void rlEnableVertexBuffer(unsigned int id) { ((void)0); }
void rlDisableVertexBuffer(void) { ((void)0); }
void rlEnableVertexBufferElement(unsigned int id) { ((void)0); }
void rlDisableVertexBufferElement(void) { ((void)0); }
void rlEnableVertexAttribute(unsigned int index) { ((void)0); }
void rlDisableVertexAttribute(unsigned int index) { ((void)0); }
void rlEnableStatePointer(int vertexAttribType, void *buffer) { ((void)0); }
void rlDisableStatePointer(int vertexAttribType) { ((void)0); }
void rlActiveTextureSlot(int slot) { ((void)0); }
void rlEnableTexture(unsigned int id) { ((void)0); }
void rlDisableTexture(void) { ((void)0); }
void rlEnableTextureCubemap(unsigned int id) { ((void)0); }
void rlDisableTextureCubemap(void) { ((void)0); }
void rlTextureParameters(unsigned int id, int param, int value) { ((void)0); }
void rlCubemapParameters(unsigned int id, int param, int value) { ((void)0); }
void rlEnableShader(unsigned int id) { ((void)0); }
void rlDisableShader(void) { ((void)0); }
void rlEnableFramebuffer(unsigned int id) { ((void)0); }
void rlDisableFramebuffer(void) { ((void)0); }
unsigned int rlGetActiveFramebuffer(void) { return (unsigned int){ 0 }; }
void rlActiveDrawBuffers(int count) { ((void)0); }
void rlBlitFramebuffer(int srcX, int srcY, int srcWidth, int srcHeight, int dstX, int dstY, int dstWidth, int dstHeight, int bufferMask) { ((void)0); }
void rlBindFramebuffer(unsigned int target, unsigned int framebuffer) { ((void)0); }
void rlEnableColorBlend(void) { ((void)0); }
void rlDisableColorBlend(void) { ((void)0); }
void rlEnableDepthTest(void) { ((void)0); }
void rlDisableDepthTest(void) { ((void)0); }
void rlEnableDepthMask(void) { ((void)0); }
void rlDisableDepthMask(void) { ((void)0); }
void rlEnableBackfaceCulling(void) { ((void)0); }
void rlDisableBackfaceCulling(void) { ((void)0); }
void rlColorMask(bool r, bool g, bool b, bool a) { ((void)0); }
void rlSetCullFace(int mode) { ((void)0); }
void rlEnableScissorTest(void) { ((void)0); }
void rlDisableScissorTest(void) { ((void)0); }
void rlScissor(int x, int y, int width, int height) { ((void)0); }
void rlEnableWireMode(void) { ((void)0); }
void rlEnablePointMode(void) { ((void)0); }
void rlDisableWireMode(void) { ((void)0); }
void rlSetLineWidth(float width) { ((void)0); }
float rlGetLineWidth(void) { return (float){ 0 }; }
void rlEnableSmoothLines(void) { ((void)0); }
void rlDisableSmoothLines(void) { ((void)0); }
void rlEnableStereoRender(void) { ((void)0); }
void rlDisableStereoRender(void) { ((void)0); }
bool rlIsStereoRenderEnabled(void) { return (bool){ 0 }; }
void rlClearColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { ((void)0); }
void rlClearScreenBuffers(void) { ((void)0); }
void rlCheckErrors(void) { ((void)0); }
void rlSetBlendMode(int mode) { ((void)0); }
void rlSetBlendFactors(int glSrcFactor, int glDstFactor, int glEquation) { ((void)0); }
void rlSetBlendFactorsSeparate(int glSrcRGB, int glDstRGB, int glSrcAlpha, int glDstAlpha, int glEqRGB, int glEqAlpha) { ((void)0); }
void rlglInit(int width, int height) { ((void)0); }
void rlglClose(void) { ((void)0); }
void rlLoadExtensions(void *loader) { ((void)0); }
int rlGetVersion(void) { return (int){ 0 }; }
void rlSetFramebufferWidth(int width) { ((void)0); }
int rlGetFramebufferWidth(void) { return (int){ 0 }; }
void rlSetFramebufferHeight(int height) { ((void)0); }
int rlGetFramebufferHeight(void) { return (int){ 0 }; }
unsigned int rlGetTextureIdDefault(void) { return (unsigned int){ 0 }; }
unsigned int rlGetShaderIdDefault(void) { return (unsigned int){ 0 }; }
int * rlGetShaderLocsDefault(void) { return (int *){ 0 }; }
rlRenderBatch rlLoadRenderBatch(int numBuffers, int bufferElements) { return (rlRenderBatch){ 0 }; }
void rlUnloadRenderBatch(rlRenderBatch batch) { ((void)0); }
void rlDrawRenderBatch(rlRenderBatch *batch) { ((void)0); }
void rlSetRenderBatchActive(rlRenderBatch *batch) { ((void)0); }
void rlDrawRenderBatchActive(void) { ((void)0); }
bool rlCheckRenderBatchLimit(int vCount) { return (bool){ 0 }; }
void rlSetTexture(unsigned int id) { ((void)0); }
unsigned int rlLoadVertexArray(void) { return (unsigned int){ 0 }; }
unsigned int rlLoadVertexBuffer(const void *buffer, int size, bool dynamic) { return (unsigned int){ 0 }; }
unsigned int rlLoadVertexBufferElement(const void *buffer, int size, bool dynamic) { return (unsigned int){ 0 }; }
void rlUpdateVertexBuffer(unsigned int bufferId, const void *data, int dataSize, int offset) { ((void)0); }
void rlUpdateVertexBufferElements(unsigned int id, const void *data, int dataSize, int offset) { ((void)0); }
void rlUnloadVertexArray(unsigned int vaoId) { ((void)0); }
void rlUnloadVertexBuffer(unsigned int vboId) { ((void)0); }
void rlSetVertexAttribute(unsigned int index, int compSize, int type, bool normalized, int stride, int offset) { ((void)0); }
void rlSetVertexAttributeDivisor(unsigned int index, int divisor) { ((void)0); }
void rlSetVertexAttributeDefault(int locIndex, const void *value, int attribType, int count) { ((void)0); }
void rlDrawVertexArray(int offset, int count) { ((void)0); }
void rlDrawVertexArrayElements(int offset, int count, const void *buffer) { ((void)0); }
void rlDrawVertexArrayInstanced(int offset, int count, int instances) { ((void)0); }
void rlDrawVertexArrayElementsInstanced(int offset, int count, const void *buffer, int instances) { ((void)0); }
unsigned int rlLoadTexture(const void *data, int width, int height, int format, int mipmapCount) { return (unsigned int){ 0 }; }
unsigned int rlLoadTextureDepth(int width, int height, bool useRenderBuffer) { return (unsigned int){ 0 }; }
unsigned int rlLoadTextureCubemap(const void *data, int size, int format, int mipmapCount) { return (unsigned int){ 0 }; }
void rlUpdateTexture(unsigned int id, int offsetX, int offsetY, int width, int height, int format, const void *data) { ((void)0); }
void rlGetGlTextureFormats(int format, unsigned int *glInternalFormat, unsigned int *glFormat, unsigned int *glType) { ((void)0); }
const char * rlGetPixelFormatName(unsigned int format) { return (const char *){ 0 }; }
void rlUnloadTexture(unsigned int id) { ((void)0); }
void rlGenTextureMipmaps(unsigned int id, int width, int height, int format, int *mipmaps) { ((void)0); }
void * rlReadTexturePixels(unsigned int id, int width, int height, int format) { return (void *){ 0 }; }
unsigned char * rlReadScreenPixels(int width, int height) { return (unsigned char *){ 0 }; }
unsigned int rlLoadFramebuffer(void) { return (unsigned int){ 0 }; }
void rlFramebufferAttach(unsigned int fboId, unsigned int texId, int attachType, int texType, int mipLevel) { ((void)0); }
bool rlFramebufferComplete(unsigned int id) { return (bool){ 0 }; }
void rlUnloadFramebuffer(unsigned int id) { ((void)0); }
unsigned int rlLoadShaderCode(const char *vsCode, const char *fsCode) { return (unsigned int){ 0 }; }
unsigned int rlCompileShader(const char *shaderCode, int type) { return (unsigned int){ 0 }; }
unsigned int rlLoadShaderProgram(unsigned int vShaderId, unsigned int fShaderId) { return (unsigned int){ 0 }; }
void rlUnloadShaderProgram(unsigned int id) { ((void)0); }
int rlGetLocationUniform(unsigned int shaderId, const char *uniformName) { return (int){ 0 }; }
int rlGetLocationAttrib(unsigned int shaderId, const char *attribName) { return (int){ 0 }; }
void rlSetUniform(int locIndex, const void *value, int uniformType, int count) { ((void)0); }
void rlSetUniformMatrix(int locIndex, Matrix mat) { ((void)0); }
void rlSetUniformMatrices(int locIndex, const Matrix *mat, int count) { ((void)0); }
void rlSetUniformSampler(int locIndex, unsigned int textureId) { ((void)0); }
void rlSetShader(unsigned int id, int *locs) { ((void)0); }
unsigned int rlLoadComputeShaderProgram(unsigned int shaderId) { return (unsigned int){ 0 }; }
void rlComputeShaderDispatch(unsigned int groupX, unsigned int groupY, unsigned int groupZ) { ((void)0); }
unsigned int rlLoadShaderBuffer(unsigned int size, const void *data, int usageHint) { return (unsigned int){ 0 }; }
void rlUnloadShaderBuffer(unsigned int ssboId) { ((void)0); }
void rlUpdateShaderBuffer(unsigned int id, const void *data, unsigned int dataSize, unsigned int offset) { ((void)0); }
void rlBindShaderBuffer(unsigned int id, unsigned int index) { ((void)0); }
void rlReadShaderBuffer(unsigned int id, void *dest, unsigned int count, unsigned int offset) { ((void)0); }
void rlCopyShaderBuffer(unsigned int destId, unsigned int srcId, unsigned int destOffset, unsigned int srcOffset, unsigned int count) { ((void)0); }
unsigned int rlGetShaderBufferSize(unsigned int id) { return (unsigned int){ 0 }; }
void rlBindImageTexture(unsigned int id, unsigned int index, int format, bool readonly) { ((void)0); }
Matrix rlGetMatrixModelview(void) { return (Matrix){ 0 }; }
Matrix rlGetMatrixProjection(void) { return (Matrix){ 0 }; }
Matrix rlGetMatrixTransform(void) { return (Matrix){ 0 }; }
Matrix rlGetMatrixProjectionStereo(int eye) { return (Matrix){ 0 }; }
Matrix rlGetMatrixViewOffsetStereo(int eye) { return (Matrix){ 0 }; }
void rlSetMatrixProjection(Matrix proj) { ((void)0); }
void rlSetMatrixModelview(Matrix view) { ((void)0); }
void rlSetMatrixProjectionStereo(Matrix right, Matrix left) { ((void)0); }
void rlSetMatrixViewOffsetStereo(Matrix right, Matrix left) { ((void)0); }
void rlLoadDrawCube(void) { ((void)0); }
void rlLoadDrawQuad(void) { ((void)0); }
