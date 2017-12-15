/**********************************************************************************************
*
*   rlgl - raylib OpenGL abstraction layer
*
*   rlgl is a wrapper for multiple OpenGL versions (1.1, 2.1, 3.3 Core, ES 2.0) to 
*   pseudo-OpenGL 1.1 style functions (rlVertex, rlTranslate, rlRotate...). 
*
*   When chosing an OpenGL version greater than OpenGL 1.1, rlgl stores vertex data on internal
*   VBO buffers (and VAOs if available). It requires calling 3 functions:
*       rlglInit()  - Initialize internal buffers and auxiliar resources
*       rlglDraw()  - Process internal buffers and send required draw calls
*       rlglClose() - De-initialize internal buffers data and other auxiliar resources
*
*   CONFIGURATION:
*
*   #define GRAPHICS_API_OPENGL_11
*   #define GRAPHICS_API_OPENGL_21
*   #define GRAPHICS_API_OPENGL_33
*   #define GRAPHICS_API_OPENGL_ES2
*       Use selected OpenGL backend
*
*   #define RLGL_STANDALONE
*       Use rlgl as standalone library (no raylib dependency)
*
*   #define SUPPORT_VR_SIMULATOR
*       Support VR simulation functionality (stereo rendering)
*
*   #define SUPPORT_DISTORTION_SHADER
*       Include stereo rendering distortion shader (shader_distortion.h)
*
*   DEPENDENCIES:
*       raymath     - 3D math functionality (Vector3, Matrix, Quaternion)
*       GLAD        - OpenGL extensions loading (OpenGL 3.3 Core only)
*
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2014-2017 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

// Default configuration flags (supported features)
//-------------------------------------------------
#define SUPPORT_VR_SIMULATOR
#define SUPPORT_DISTORTION_SHADER
//-------------------------------------------------

#include "rlgl.h"

#include <stdio.h>                  // Required for: fopen(), fclose(), fread()... [Used only on LoadText()]
#include <stdlib.h>                 // Required for: malloc(), free(), rand()
#include <string.h>                 // Required for: strcmp(), strlen(), strtok() [Used only in extensions loading]
#include <math.h>                   // Required for: atan2()

#if !defined(RLGL_STANDALONE)
    #include "raymath.h"            // Required for: Vector3 and Matrix functions
#endif

#if defined(GRAPHICS_API_OPENGL_11)
    #if defined(__APPLE__)
        #include <OpenGL/gl.h>      // OpenGL 1.1 library for OSX
    #else
        #include <GL/gl.h>          // OpenGL 1.1 library
    #endif
#endif

#if defined(GRAPHICS_API_OPENGL_21)
    #define GRAPHICS_API_OPENGL_33
#endif

#if defined(GRAPHICS_API_OPENGL_33)
    #if defined(__APPLE__)
        #include <OpenGL/gl3.h>     // OpenGL 3 library for OSX
    #else
        #define GLAD_IMPLEMENTATION
        #if defined(RLGL_STANDALONE)
            #include "glad.h"           // GLAD extensions loading library, includes OpenGL headers
        #else
            #include "external/glad.h"  // GLAD extensions loading library, includes OpenGL headers
        #endif
    #endif
#endif

#if defined(GRAPHICS_API_OPENGL_ES2)
    #include <EGL/egl.h>            // EGL library
    #include <GLES2/gl2.h>          // OpenGL ES 2.0 library
    #include <GLES2/gl2ext.h>       // OpenGL ES 2.0 extensions library
#endif

#if defined(RLGL_STANDALONE)
    #include <stdarg.h>             // Required for: va_list, va_start(), vfprintf(), va_end() [Used only on TraceLog()]
#endif

#if !defined(GRAPHICS_API_OPENGL_11) && defined(SUPPORT_DISTORTION_SHADER)
    #include "shader_distortion.h"  // Distortion shader to be embedded
#endif

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#define MATRIX_STACK_SIZE          16   // Matrix stack max size
#define MAX_DRAWS_BY_TEXTURE      256   // Draws are organized by texture changes
#define TEMP_VERTEX_BUFFER_SIZE  4096   // Temporal Vertex Buffer (required for vertex-transformations)
                                        // NOTE: Every vertex are 3 floats (12 bytes)

#ifndef GL_SHADING_LANGUAGE_VERSION
    #define GL_SHADING_LANGUAGE_VERSION         0x8B8C
#endif

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
    #define GL_COMPRESSED_RGB_S3TC_DXT1_EXT     0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT    0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT    0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
    #define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT    0x83F3
#endif
#ifndef GL_ETC1_RGB8_OES
    #define GL_ETC1_RGB8_OES                    0x8D64
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
    #define GL_COMPRESSED_RGB8_ETC2             0x9274
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
    #define GL_COMPRESSED_RGBA8_ETC2_EAC        0x9278
#endif
#ifndef GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG
    #define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG  0x8C00
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
    #define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
    #define GL_COMPRESSED_RGBA_ASTC_4x4_KHR     0x93b0
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_8x8_KHR
    #define GL_COMPRESSED_RGBA_ASTC_8x8_KHR     0x93b7
#endif

#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
    #define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT   0x84FF
#endif

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
    #define GL_TEXTURE_MAX_ANISOTROPY_EXT       0x84FE
#endif

#if defined(GRAPHICS_API_OPENGL_11)
    #define GL_UNSIGNED_SHORT_5_6_5             0x8363
    #define GL_UNSIGNED_SHORT_5_5_5_1           0x8034
    #define GL_UNSIGNED_SHORT_4_4_4_4           0x8033
#endif

#if defined(GRAPHICS_API_OPENGL_21)
    #define GL_LUMINANCE                        0x1909
    #define GL_LUMINANCE_ALPHA                  0x190A
#endif

#if defined(GRAPHICS_API_OPENGL_ES2)
    #define glClearDepth                glClearDepthf
    #define GL_READ_FRAMEBUFFER         GL_FRAMEBUFFER
    #define GL_DRAW_FRAMEBUFFER         GL_FRAMEBUFFER
#endif

// Default vertex attribute names on shader to set location points
#define DEFAULT_ATTRIB_POSITION_NAME    "vertexPosition"    // shader-location = 0
#define DEFAULT_ATTRIB_TEXCOORD_NAME    "vertexTexCoord"    // shader-location = 1
#define DEFAULT_ATTRIB_NORMAL_NAME      "vertexNormal"      // shader-location = 2
#define DEFAULT_ATTRIB_COLOR_NAME       "vertexColor"       // shader-location = 3
#define DEFAULT_ATTRIB_TANGENT_NAME     "vertexTangent"     // shader-location = 4
#define DEFAULT_ATTRIB_TEXCOORD2_NAME   "vertexTexCoord2"   // shader-location = 5

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------

// Dynamic vertex buffers (position + texcoords + colors + indices arrays)
typedef struct DynamicBuffer {
    int vCounter;               // vertex position counter to process (and draw) from full buffer
    int tcCounter;              // vertex texcoord counter to process (and draw) from full buffer
    int cCounter;               // vertex color counter to process (and draw) from full buffer
    float *vertices;            // vertex position (XYZ - 3 components per vertex) (shader-location = 0)
    float *texcoords;           // vertex texture coordinates (UV - 2 components per vertex) (shader-location = 1)
    unsigned char *colors;      // vertex colors (RGBA - 4 components per vertex) (shader-location = 3)
#if defined(GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_33)
    unsigned int *indices;      // vertex indices (in case vertex data comes indexed) (6 indices per quad)
#elif defined(GRAPHICS_API_OPENGL_ES2)
    unsigned short *indices;    // vertex indices (in case vertex data comes indexed) (6 indices per quad)
                                // NOTE: 6*2 byte = 12 byte, not alignment problem!
#endif
    unsigned int vaoId;         // OpenGL Vertex Array Object id
    unsigned int vboId[4];      // OpenGL Vertex Buffer Objects id (4 types of vertex data)
} DynamicBuffer;

// Draw call type
// NOTE: Used to track required draw-calls, organized by texture
typedef struct DrawCall {
    int vertexCount;
    GLuint vaoId;
    GLuint textureId;
    GLuint shaderId;

    Matrix projection;
    Matrix modelview;

    // TODO: Store additional draw state data
    //int blendMode;
    //Guint fboId;
} DrawCall;

#if defined(SUPPORT_VR_SIMULATOR)
// VR Stereo rendering configuration for simulator
typedef struct VrStereoConfig {
    RenderTexture2D stereoFbo;      // VR stereo rendering framebuffer
    Shader distortionShader;        // VR stereo rendering distortion shader
    Rectangle eyesViewport[2];      // VR stereo rendering eyes viewports
    Matrix eyesProjection[2];       // VR stereo rendering eyes projection matrices
    Matrix eyesViewOffset[2];       // VR stereo rendering eyes view offset matrices
} VrStereoConfig;
#endif

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
static Matrix stack[MATRIX_STACK_SIZE];
static int stackCounter = 0;

static Matrix modelview;
static Matrix projection;
static Matrix *currentMatrix;
static int currentMatrixMode;

static int currentDrawMode;

static float currentDepth = -1.0f;

static DynamicBuffer lines;                 // Default dynamic buffer for lines data
static DynamicBuffer triangles;             // Default dynamic buffer for triangles data
static DynamicBuffer quads;                 // Default dynamic buffer for quads data (used to draw textures)

// Default buffers draw calls
static DrawCall *draws;
static int drawsCounter;

// Temp vertex buffer to be used with rlTranslate, rlRotate, rlScale
static Vector3 *tempBuffer;
static int tempBufferCount = 0;
static bool useTempBuffer = false;

// Shaders
static unsigned int defaultVShaderId;       // Default vertex shader id (used by default shader program)
static unsigned int defaultFShaderId;       // Default fragment shader Id (used by default shader program)
static Shader defaultShader;                // Basic shader, support vertex color and diffuse texture
static Shader currentShader;                // Shader to be used on rendering (by default, defaultShader)

// Extension supported flag: VAO
static bool vaoSupported = false;           // VAO support (OpenGL ES2 could not support VAO extension)

// Extension supported flag: Compressed textures
static bool texCompETC1Supported = false;   // ETC1 texture compression support
static bool texCompETC2Supported = false;   // ETC2/EAC texture compression support
static bool texCompPVRTSupported = false;   // PVR texture compression support
static bool texCompASTCSupported = false;   // ASTC texture compression support

#if defined(SUPPORT_VR_SIMULATOR)
// VR global variables
static VrStereoConfig vrConfig;         // VR stereo configuration for simulator
static bool vrSimulatorReady = false;   // VR simulator ready flag
static bool vrStereoRender = false;     // VR stereo rendering enabled/disabled flag
                                        // NOTE: This flag is useful to render data over stereo image (i.e. FPS)
#endif  // defined(SUPPORT_VR_SIMULATOR)

#endif  // defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

// Extension supported flag: Anisotropic filtering
static bool texAnisotropicFilterSupported = false;  // Anisotropic texture filtering support
static float maxAnisotropicLevel = 0.0f;        // Maximum anisotropy level supported (minimum is 2.0f)

// Extension supported flag: Clamp mirror wrap mode
static bool texClampMirrorSupported = false;    // Clamp mirror wrap mode supported

#if defined(GRAPHICS_API_OPENGL_ES2)
// NOTE: VAO functionality is exposed through extensions (OES)
static PFNGLGENVERTEXARRAYSOESPROC glGenVertexArrays;
static PFNGLBINDVERTEXARRAYOESPROC glBindVertexArray;
static PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArrays;
//static PFNGLISVERTEXARRAYOESPROC glIsVertexArray;        // NOTE: Fails in WebGL, omitted
#endif

static bool debugMarkerSupported = false;

// Compressed textures support flags
static bool texCompDXTSupported = false;    // DDS texture compression support
static bool texNPOTSupported = false;       // NPOT textures full support
static bool texFloatSupported = false;      // float textures support (32 bit per channel)

static int blendMode = 0;   // Track current blending mode

// White texture useful for plain color polys (required by shader)
static unsigned int whiteTexture;

// Default framebuffer size
static int screenWidth;     // Default framebuffer width
static int screenHeight;    // Default framebuffer height

//----------------------------------------------------------------------------------
// Module specific Functions Declaration
//----------------------------------------------------------------------------------
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
static void LoadTextureCompressed(unsigned char *data, int width, int height, int compressedFormat, int mipmapCount);

static unsigned int CompileShader(const char *shaderStr, int type);     // Compile custom shader and return shader id
static unsigned int LoadShaderProgram(unsigned int vShaderId, unsigned int fShaderId);  // Load custom shader program

static Shader LoadShaderDefault(void);      // Load default shader (just vertex positioning and texture coloring)
static void SetShaderDefaultLocations(Shader *shader); // Bind default shader locations (attributes and uniforms)
static void UnloadShaderDefault(void);      // Unload default shader

static void LoadBuffersDefault(void);       // Load default internal buffers (lines, triangles, quads)
static void UpdateBuffersDefault(void);     // Update default internal buffers (VAOs/VBOs) with vertex data
static void DrawBuffersDefault(void);       // Draw default internal buffers vertex data
static void UnloadBuffersDefault(void);     // Unload default internal buffers vertex data from CPU and GPU

static void GenDrawCube(void);              // Generate and draw cube
static void GenDrawQuad(void);              // Generate and draw quad

#if defined(SUPPORT_VR_SIMULATOR)
static void SetStereoConfig(VrDeviceInfo info); // Configure stereo rendering (including distortion shader) with HMD device parameters
static void SetStereoView(int eye, Matrix matProjection, Matrix matModelView); // Set internal projection and modelview matrix depending on eye
#endif

#endif  // defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

#if defined(GRAPHICS_API_OPENGL_11)
static int GenerateMipmaps(unsigned char *data, int baseWidth, int baseHeight);
static Color *GenNextMipmap(Color *srcData, int srcWidth, int srcHeight);
#endif

//----------------------------------------------------------------------------------
// Module Functions Definition - Matrix operations
//----------------------------------------------------------------------------------

#if defined(GRAPHICS_API_OPENGL_11)

// Fallback to OpenGL 1.1 function calls
//---------------------------------------
void rlMatrixMode(int mode)
{
    switch (mode)
    {
        case RL_PROJECTION: glMatrixMode(GL_PROJECTION); break;
        case RL_MODELVIEW: glMatrixMode(GL_MODELVIEW); break;
        case RL_TEXTURE: glMatrixMode(GL_TEXTURE); break;
        default: break;
    }
}

void rlFrustum(double left, double right, double bottom, double top, double zNear, double zFar)
{
    glFrustum(left, right, bottom, top, zNear, zFar);
}

void rlOrtho(double left, double right, double bottom, double top, double zNear, double zFar)
{
    glOrtho(left, right, bottom, top, zNear, zFar);
}

void rlPushMatrix(void) { glPushMatrix(); }
void rlPopMatrix(void) { glPopMatrix(); }
void rlLoadIdentity(void) { glLoadIdentity(); }
void rlTranslatef(float x, float y, float z) { glTranslatef(x, y, z); }
void rlRotatef(float angleDeg, float x, float y, float z) { glRotatef(angleDeg, x, y, z); }
void rlScalef(float x, float y, float z) { glScalef(x, y, z); }
void rlMultMatrixf(float *matf) { glMultMatrixf(matf); }

#elif defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

// Choose the current matrix to be transformed
void rlMatrixMode(int mode)
{
    if (mode == RL_PROJECTION) currentMatrix = &projection;
    else if (mode == RL_MODELVIEW) currentMatrix = &modelview;
    //else if (mode == RL_TEXTURE) // Not supported

    currentMatrixMode = mode;
}

// Push the current matrix to stack
void rlPushMatrix(void)
{
    if (stackCounter == MATRIX_STACK_SIZE - 1)
    {
        TraceLog(LOG_ERROR, "Stack Buffer Overflow (MAX %i Matrix)", MATRIX_STACK_SIZE);
    }

    stack[stackCounter] = *currentMatrix;
//    rlLoadIdentity();
    stackCounter++;

    if (currentMatrixMode == RL_MODELVIEW) useTempBuffer = true;
}

// Pop lattest inserted matrix from stack
void rlPopMatrix(void)
{
    if (stackCounter > 0)
    {
        Matrix mat = stack[stackCounter - 1];
        *currentMatrix = mat;
        stackCounter--;
    }
}

// Reset current matrix to identity matrix
void rlLoadIdentity(void)
{
    *currentMatrix = MatrixIdentity();
}

// Multiply the current matrix by a translation matrix
void rlTranslatef(float x, float y, float z)
{
    Matrix matTranslation = MatrixTranslate(x, y, z);

    // NOTE: We transpose matrix with multiplication order
    *currentMatrix = MatrixMultiply(matTranslation, *currentMatrix);
}

// Multiply the current matrix by a rotation matrix
void rlRotatef(float angleDeg, float x, float y, float z)
{
    Matrix matRotation = MatrixIdentity();

    Vector3 axis = (Vector3){ x, y, z };
    Vector3Normalize(&axis);
    matRotation = MatrixRotate(axis, angleDeg*DEG2RAD);

    // NOTE: We transpose matrix with multiplication order
    *currentMatrix = MatrixMultiply(matRotation, *currentMatrix);
}

// Multiply the current matrix by a scaling matrix
void rlScalef(float x, float y, float z)
{
    Matrix matScale = MatrixScale(x, y, z);

    // NOTE: We transpose matrix with multiplication order
    *currentMatrix = MatrixMultiply(matScale, *currentMatrix);
}

// Multiply the current matrix by another matrix
void rlMultMatrixf(float *matf)
{
    // Matrix creation from array
    Matrix mat = { matf[0], matf[4], matf[8], matf[12],
                   matf[1], matf[5], matf[9], matf[13],
                   matf[2], matf[6], matf[10], matf[14],
                   matf[3], matf[7], matf[11], matf[15] };

    *currentMatrix = MatrixMultiply(*currentMatrix, mat);
}

// Multiply the current matrix by a perspective matrix generated by parameters
void rlFrustum(double left, double right, double bottom, double top, double near, double far)
{
    Matrix matPerps = MatrixFrustum(left, right, bottom, top, near, far);

    *currentMatrix = MatrixMultiply(*currentMatrix, matPerps);
}

// Multiply the current matrix by an orthographic matrix generated by parameters
void rlOrtho(double left, double right, double bottom, double top, double near, double far)
{
    Matrix matOrtho = MatrixOrtho(left, right, bottom, top, near, far);

    *currentMatrix = MatrixMultiply(*currentMatrix, matOrtho);
}

#endif

// Set the viewport area (transformation from normalized device coordinates to window coordinates)
// NOTE: Updates global variables: screenWidth, screenHeight
void rlViewport(int x, int y, int width, int height)
{
    glViewport(x, y, width, height);
}

//----------------------------------------------------------------------------------
// Module Functions Definition - Vertex level operations
//----------------------------------------------------------------------------------
#if defined(GRAPHICS_API_OPENGL_11)

// Fallback to OpenGL 1.1 function calls
//---------------------------------------
void rlBegin(int mode)
{
    switch (mode)
    {
        case RL_LINES: glBegin(GL_LINES); break;
        case RL_TRIANGLES: glBegin(GL_TRIANGLES); break;
        case RL_QUADS: glBegin(GL_QUADS); break;
        default: break;
    }
}

void rlEnd() { glEnd(); }
void rlVertex2i(int x, int y) { glVertex2i(x, y); }
void rlVertex2f(float x, float y) { glVertex2f(x, y); }
void rlVertex3f(float x, float y, float z) { glVertex3f(x, y, z); }
void rlTexCoord2f(float x, float y) { glTexCoord2f(x, y); }
void rlNormal3f(float x, float y, float z) { glNormal3f(x, y, z); }
void rlColor4ub(byte r, byte g, byte b, byte a) { glColor4ub(r, g, b, a); }
void rlColor3f(float x, float y, float z) { glColor3f(x, y, z); }
void rlColor4f(float x, float y, float z, float w) { glColor4f(x, y, z, w); }

#elif defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

// Initialize drawing mode (how to organize vertex)
void rlBegin(int mode)
{
    // Draw mode can only be RL_LINES, RL_TRIANGLES and RL_QUADS
    currentDrawMode = mode;
}

// Finish vertex providing
void rlEnd(void)
{
    if (useTempBuffer)
    {
        // NOTE: In this case, *currentMatrix is already transposed because transposing has been applied
        // independently to translation-scale-rotation matrices -> t(M1 x M2) = t(M2) x t(M1)
        // This way, rlTranslatef(), rlRotatef()... behaviour is the same than OpenGL 1.1

        // Apply transformation matrix to all temp vertices
        for (int i = 0; i < tempBufferCount; i++) Vector3Transform(&tempBuffer[i], *currentMatrix);

        // Deactivate tempBuffer usage to allow rlVertex3f do its job
        useTempBuffer = false;

        // Copy all transformed vertices to right VAO
        for (int i = 0; i < tempBufferCount; i++) rlVertex3f(tempBuffer[i].x, tempBuffer[i].y, tempBuffer[i].z);

        // Reset temp buffer
        tempBufferCount = 0;
    }

    // Make sure vertexCount is the same for vertices-texcoords-normals-colors
    // NOTE: In OpenGL 1.1, one glColor call can be made for all the subsequent glVertex calls.
    switch (currentDrawMode)
    {
        case RL_LINES:
        {
            if (lines.vCounter != lines.cCounter)
            {
                int addColors = lines.vCounter - lines.cCounter;

                for (int i = 0; i < addColors; i++)
                {
                    lines.colors[4*lines.cCounter] = lines.colors[4*lines.cCounter - 4];
                    lines.colors[4*lines.cCounter + 1] = lines.colors[4*lines.cCounter - 3];
                    lines.colors[4*lines.cCounter + 2] = lines.colors[4*lines.cCounter - 2];
                    lines.colors[4*lines.cCounter + 3] = lines.colors[4*lines.cCounter - 1];

                    lines.cCounter++;
                }
            }
        } break;
        case RL_TRIANGLES:
        {
            if (triangles.vCounter != triangles.cCounter)
            {
                int addColors = triangles.vCounter - triangles.cCounter;

                for (int i = 0; i < addColors; i++)
                {
                    triangles.colors[4*triangles.cCounter] = triangles.colors[4*triangles.cCounter - 4];
                    triangles.colors[4*triangles.cCounter + 1] = triangles.colors[4*triangles.cCounter - 3];
                    triangles.colors[4*triangles.cCounter + 2] = triangles.colors[4*triangles.cCounter - 2];
                    triangles.colors[4*triangles.cCounter + 3] = triangles.colors[4*triangles.cCounter - 1];

                    triangles.cCounter++;
                }
            }
        } break;
        case RL_QUADS:
        {
            // Make sure colors count match vertex count
            if (quads.vCounter != quads.cCounter)
            {
                int addColors = quads.vCounter - quads.cCounter;

                for (int i = 0; i < addColors; i++)
                {
                    quads.colors[4*quads.cCounter] = quads.colors[4*quads.cCounter - 4];
                    quads.colors[4*quads.cCounter + 1] = quads.colors[4*quads.cCounter - 3];
                    quads.colors[4*quads.cCounter + 2] = quads.colors[4*quads.cCounter - 2];
                    quads.colors[4*quads.cCounter + 3] = quads.colors[4*quads.cCounter - 1];

                    quads.cCounter++;
                }
            }

            // Make sure texcoords count match vertex count
            if (quads.vCounter != quads.tcCounter)
            {
                int addTexCoords = quads.vCounter - quads.tcCounter;

                for (int i = 0; i < addTexCoords; i++)
                {
                    quads.texcoords[2*quads.tcCounter] = 0.0f;
                    quads.texcoords[2*quads.tcCounter + 1] = 0.0f;

                    quads.tcCounter++;
                }
            }

            // TODO: Make sure normals count match vertex count... if normals support is added in a future... :P

        } break;
        default: break;
    }

    // NOTE: Depth increment is dependant on rlOrtho(): z-near and z-far values,
    // as well as depth buffer bit-depth (16bit or 24bit or 32bit)
    // Correct increment formula would be: depthInc = (zfar - znear)/pow(2, bits)
    currentDepth += (1.0f/20000.0f);
}

// Define one vertex (position)
void rlVertex3f(float x, float y, float z)
{
    if (useTempBuffer)
    {
        tempBuffer[tempBufferCount].x = x;
        tempBuffer[tempBufferCount].y = y;
        tempBuffer[tempBufferCount].z = z;
        tempBufferCount++;
    }
    else
    {
        switch (currentDrawMode)
        {
            case RL_LINES:
            {
                // Verify that MAX_LINES_BATCH limit not reached
                if (lines.vCounter/2 < MAX_LINES_BATCH)
                {
                    lines.vertices[3*lines.vCounter] = x;
                    lines.vertices[3*lines.vCounter + 1] = y;
                    lines.vertices[3*lines.vCounter + 2] = z;

                    lines.vCounter++;
                }
                else TraceLog(LOG_ERROR, "MAX_LINES_BATCH overflow");

            } break;
            case RL_TRIANGLES:
            {
                // Verify that MAX_TRIANGLES_BATCH limit not reached
                if (triangles.vCounter/3 < MAX_TRIANGLES_BATCH)
                {
                    triangles.vertices[3*triangles.vCounter] = x;
                    triangles.vertices[3*triangles.vCounter + 1] = y;
                    triangles.vertices[3*triangles.vCounter + 2] = z;

                    triangles.vCounter++;
                }
                else TraceLog(LOG_ERROR, "MAX_TRIANGLES_BATCH overflow");

            } break;
            case RL_QUADS:
            {
                // Verify that MAX_QUADS_BATCH limit not reached
                if (quads.vCounter/4 < MAX_QUADS_BATCH)
                {
                    quads.vertices[3*quads.vCounter] = x;
                    quads.vertices[3*quads.vCounter + 1] = y;
                    quads.vertices[3*quads.vCounter + 2] = z;

                    quads.vCounter++;

                    draws[drawsCounter - 1].vertexCount++;
                }
                else TraceLog(LOG_ERROR, "MAX_QUADS_BATCH overflow");

            } break;
            default: break;
        }
    }
}

// Define one vertex (position)
void rlVertex2f(float x, float y)
{
    rlVertex3f(x, y, currentDepth);
}

// Define one vertex (position)
void rlVertex2i(int x, int y)
{
    rlVertex3f((float)x, (float)y, currentDepth);
}

// Define one vertex (texture coordinate)
// NOTE: Texture coordinates are limited to QUADS only
void rlTexCoord2f(float x, float y)
{
    if (currentDrawMode == RL_QUADS)
    {
        quads.texcoords[2*quads.tcCounter] = x;
        quads.texcoords[2*quads.tcCounter + 1] = y;

        quads.tcCounter++;
    }
}

// Define one vertex (normal)
// NOTE: Normals limited to TRIANGLES only ?
void rlNormal3f(float x, float y, float z)
{
    // TODO: Normals usage...
}

// Define one vertex (color)
void rlColor4ub(byte x, byte y, byte z, byte w)
{
    switch (currentDrawMode)
    {
        case RL_LINES:
        {
            lines.colors[4*lines.cCounter] = x;
            lines.colors[4*lines.cCounter + 1] = y;
            lines.colors[4*lines.cCounter + 2] = z;
            lines.colors[4*lines.cCounter + 3] = w;

            lines.cCounter++;

        } break;
        case RL_TRIANGLES:
        {
            triangles.colors[4*triangles.cCounter] = x;
            triangles.colors[4*triangles.cCounter + 1] = y;
            triangles.colors[4*triangles.cCounter + 2] = z;
            triangles.colors[4*triangles.cCounter + 3] = w;

            triangles.cCounter++;

        } break;
        case RL_QUADS:
        {
            quads.colors[4*quads.cCounter] = x;
            quads.colors[4*quads.cCounter + 1] = y;
            quads.colors[4*quads.cCounter + 2] = z;
            quads.colors[4*quads.cCounter + 3] = w;

            quads.cCounter++;

        } break;
        default: break;
    }
}

// Define one vertex (color)
void rlColor4f(float r, float g, float b, float a)
{
    rlColor4ub((byte)(r*255), (byte)(g*255), (byte)(b*255), (byte)(a*255));
}

// Define one vertex (color)
void rlColor3f(float x, float y, float z)
{
    rlColor4ub((byte)(x*255), (byte)(y*255), (byte)(z*255), 255);
}

#endif

//----------------------------------------------------------------------------------
// Module Functions Definition - OpenGL equivalent functions (common to 1.1, 3.3+, ES2)
//----------------------------------------------------------------------------------

// Enable texture usage
void rlEnableTexture(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_11)
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, id);
#endif

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (draws[drawsCounter - 1].textureId != id)
    {
        if (draws[drawsCounter - 1].vertexCount > 0) drawsCounter++;
        
        if (drawsCounter >= MAX_DRAWS_BY_TEXTURE)
        {
            rlglDraw();
            drawsCounter = 1;
        }

        draws[drawsCounter - 1].textureId = id;
        draws[drawsCounter - 1].vertexCount = 0;
    }
#endif
}

// Disable texture usage
void rlDisableTexture(void)
{
#if defined(GRAPHICS_API_OPENGL_11)
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
#else
    // NOTE: If quads batch limit is reached,
    // we force a draw call and next batch starts
    if (quads.vCounter/4 >= MAX_QUADS_BATCH) rlglDraw();
#endif
}

// Set texture parameters (wrap mode/filter mode)
void rlTextureParameters(unsigned int id, int param, int value)
{
    glBindTexture(GL_TEXTURE_2D, id);

    switch (param)
    {
        case RL_TEXTURE_WRAP_S:
        case RL_TEXTURE_WRAP_T:
        {
            if ((value == RL_WRAP_CLAMP_MIRROR) && !texClampMirrorSupported) TraceLog(LOG_WARNING, "Clamp mirror wrap mode not supported");
            else glTexParameteri(GL_TEXTURE_2D, param, value);
        } break;
        case RL_TEXTURE_MAG_FILTER:
        case RL_TEXTURE_MIN_FILTER: glTexParameteri(GL_TEXTURE_2D, param, value); break;
        case RL_TEXTURE_ANISOTROPIC_FILTER:
        {
            if (value <= maxAnisotropicLevel) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)value);
            else if (maxAnisotropicLevel > 0.0f)
            {
                TraceLog(LOG_WARNING, "[TEX ID %i] Maximum anisotropic filter level supported is %iX", id, maxAnisotropicLevel);
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)value);
            }
            else TraceLog(LOG_WARNING, "Anisotropic filtering not supported");
        } break;
        default: break;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

// Enable rendering to texture (fbo)
void rlEnableRenderTexture(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glBindFramebuffer(GL_FRAMEBUFFER, id);

    //glDisable(GL_CULL_FACE);    // Allow double side drawing for texture flipping
    //glCullFace(GL_FRONT);
#endif
}

// Disable rendering to texture
void rlDisableRenderTexture(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //glEnable(GL_CULL_FACE);
    //glCullFace(GL_BACK);
#endif
}

// Enable depth test
void rlEnableDepthTest(void)
{
    glEnable(GL_DEPTH_TEST);
}

// Disable depth test
void rlDisableDepthTest(void)
{
    glDisable(GL_DEPTH_TEST);
}

// Enable wire mode
void rlEnableWireMode(void)
{
#if defined (GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_33)
    // NOTE: glPolygonMode() not available on OpenGL ES
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif
}

// Disable wire mode
void rlDisableWireMode(void)
{
#if defined (GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_33)
    // NOTE: glPolygonMode() not available on OpenGL ES
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
}

// Unload texture from GPU memory
void rlDeleteTextures(unsigned int id)
{
    if (id > 0) glDeleteTextures(1, &id);
}

// Unload render texture from GPU memory
void rlDeleteRenderTextures(RenderTexture2D target)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (target.id > 0) glDeleteFramebuffers(1, &target.id);
    if (target.texture.id > 0) glDeleteTextures(1, &target.texture.id);
    if (target.depth.id > 0) glDeleteTextures(1, &target.depth.id);

    TraceLog(LOG_INFO, "[FBO ID %i] Unloaded render texture data from VRAM (GPU)", target.id);
#endif
}

// Unload shader from GPU memory
void rlDeleteShader(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (id != 0) glDeleteProgram(id);
#endif
}

// Unload vertex data (VAO) from GPU memory
void rlDeleteVertexArrays(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (vaoSupported)
    {
        if (id != 0) glDeleteVertexArrays(1, &id);
        TraceLog(LOG_INFO, "[VAO ID %i] Unloaded model data from VRAM (GPU)", id);
    }
#endif
}

// Unload vertex data (VBO) from GPU memory
void rlDeleteBuffers(unsigned int id)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (id != 0)
    {
        glDeleteBuffers(1, &id);
        if (!vaoSupported) TraceLog(LOG_INFO, "[VBO ID %i] Unloaded model vertex data from VRAM (GPU)", id);
    }
#endif
}

// Clear color buffer with color
void rlClearColor(byte r, byte g, byte b, byte a)
{
    // Color values clamp to 0.0f(0) and 1.0f(255)
    float cr = (float)r/255;
    float cg = (float)g/255;
    float cb = (float)b/255;
    float ca = (float)a/255;

    glClearColor(cr, cg, cb, ca);
}

// Clear used screen buffers (color and depth)
void rlClearScreenBuffers(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);     // Clear used buffers: Color and Depth (Depth is used for 3D)
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);     // Stencil buffer not used...
}

//----------------------------------------------------------------------------------
// Module Functions Definition - rlgl Functions
//----------------------------------------------------------------------------------

// Initialize rlgl: OpenGL extensions, default buffers/shaders/textures, OpenGL states
void rlglInit(int width, int height)
{
    // Check OpenGL information and capabilities
    //------------------------------------------------------------------------------

    // Print current OpenGL and GLSL version
    TraceLog(LOG_INFO, "GPU: Vendor:   %s", glGetString(GL_VENDOR));
    TraceLog(LOG_INFO, "GPU: Renderer: %s", glGetString(GL_RENDERER));
    TraceLog(LOG_INFO, "GPU: Version:  %s", glGetString(GL_VERSION));
    TraceLog(LOG_INFO, "GPU: GLSL:     %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

    // NOTE: We can get a bunch of extra information about GPU capabilities (glGet*)
    //int maxTexSize;
    //glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    //TraceLog(LOG_INFO, "GL_MAX_TEXTURE_SIZE: %i", maxTexSize);

    //GL_MAX_TEXTURE_IMAGE_UNITS
    //GL_MAX_VIEWPORT_DIMS

    //int numAuxBuffers;
    //glGetIntegerv(GL_AUX_BUFFERS, &numAuxBuffers);
    //TraceLog(LOG_INFO, "GL_AUX_BUFFERS: %i", numAuxBuffers);

    //GLint numComp = 0;
    //GLint format[32] = { 0 };
    //glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numComp);
    //glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, format);
    //for (int i = 0; i < numComp; i++) TraceLog(LOG_INFO, "Supported compressed format: 0x%x", format[i]);

    // NOTE: We don't need that much data on screen... right now...

#if defined(GRAPHICS_API_OPENGL_11)
    //TraceLog(LOG_INFO, "OpenGL 1.1 (or driver default) profile initialized");
#endif

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // Get supported extensions list
    GLint numExt = 0;

#if defined(GRAPHICS_API_OPENGL_33)

    // NOTE: On OpenGL 3.3 VAO and NPOT are supported by default
    vaoSupported = true;
    texNPOTSupported = true;
    texFloatSupported = true;

    // We get a list of available extensions and we check for some of them (compressed textures)
    // NOTE: We don't need to check again supported extensions but we do (GLAD already dealt with that)
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExt);

#ifdef _MSC_VER
    const char **extList = malloc(sizeof(const char *)*numExt);
#else
    const char *extList[numExt];
#endif

    for (int i = 0; i < numExt; i++) extList[i] = (char *)glGetStringi(GL_EXTENSIONS, i);

#elif defined(GRAPHICS_API_OPENGL_ES2)
    char *extensions = (char *)glGetString(GL_EXTENSIONS);  // One big const string

    // NOTE: We have to duplicate string because glGetString() returns a const value
    // If not duplicated, it fails in some systems (Raspberry Pi)
    // Equivalent to function: char *strdup(const char *str)
    char *extensionsDup;
    size_t len = strlen(extensions) + 1;
    void *newstr = malloc(len);
    if (newstr == NULL) extensionsDup = NULL;
    extensionsDup = (char *)memcpy(newstr, extensions, len);

    // NOTE: String could be splitted using strtok() function (string.h)
    // NOTE: strtok() modifies the received string, it can not be const

    char *extList[512];     // Allocate 512 strings pointers (2 KB)

    extList[numExt] = strtok(extensionsDup, " ");

    while (extList[numExt] != NULL)
    {
        numExt++;
        extList[numExt] = strtok(NULL, " ");
    }

    free(extensionsDup);    // Duplicated string must be deallocated

    numExt -= 1;
#endif

    TraceLog(LOG_INFO, "Number of supported extensions: %i", numExt);

    // Show supported extensions
    //for (int i = 0; i < numExt; i++)  TraceLog(LOG_INFO, "Supported extension: %s", extList[i]);

    // Check required extensions
    for (int i = 0; i < numExt; i++)
    {
#if defined(GRAPHICS_API_OPENGL_ES2)
        // Check VAO support
        // NOTE: Only check on OpenGL ES, OpenGL 3.3 has VAO support as core feature
        if (strcmp(extList[i], (const char *)"GL_OES_vertex_array_object") == 0)
        {
            vaoSupported = true;

            // The extension is supported by our hardware and driver, try to get related functions pointers
            // NOTE: emscripten does not support VAOs natively, it uses emulation and it reduces overall performance...
            glGenVertexArrays = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArraysOES");
            glBindVertexArray = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArrayOES");
            glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSOESPROC)eglGetProcAddress("glDeleteVertexArraysOES");
            //glIsVertexArray = (PFNGLISVERTEXARRAYOESPROC)eglGetProcAddress("glIsVertexArrayOES");     // NOTE: Fails in WebGL, omitted
        }

        // Check NPOT textures support
        // NOTE: Only check on OpenGL ES, OpenGL 3.3 has NPOT textures full support as core feature
        if (strcmp(extList[i], (const char *)"GL_OES_texture_npot") == 0) texNPOTSupported = true;
        
        // Check texture float support
        if (strcmp(extList[i], (const char *)"OES_texture_float") == 0) texFloatSupported = true;
#endif

        // DDS texture compression support
        if ((strcmp(extList[i], (const char *)"GL_EXT_texture_compression_s3tc") == 0) ||
            (strcmp(extList[i], (const char *)"GL_WEBGL_compressed_texture_s3tc") == 0) ||
            (strcmp(extList[i], (const char *)"GL_WEBKIT_WEBGL_compressed_texture_s3tc") == 0)) texCompDXTSupported = true;

        // ETC1 texture compression support
        if ((strcmp(extList[i], (const char *)"GL_OES_compressed_ETC1_RGB8_texture") == 0) ||
            (strcmp(extList[i], (const char *)"GL_WEBGL_compressed_texture_etc1") == 0)) texCompETC1Supported = true;

        // ETC2/EAC texture compression support
        if (strcmp(extList[i], (const char *)"GL_ARB_ES3_compatibility") == 0) texCompETC2Supported = true;

        // PVR texture compression support
        if (strcmp(extList[i], (const char *)"GL_IMG_texture_compression_pvrtc") == 0) texCompPVRTSupported = true;

        // ASTC texture compression support
        if (strcmp(extList[i], (const char *)"GL_KHR_texture_compression_astc_hdr") == 0) texCompASTCSupported = true;

        // Anisotropic texture filter support
        if (strcmp(extList[i], (const char *)"GL_EXT_texture_filter_anisotropic") == 0)
        {
            texAnisotropicFilterSupported = true;
            glGetFloatv(0x84FF, &maxAnisotropicLevel);   // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
        }

        if(strcmp(extList[i], (const char *)"GL_EXT_debug_marker") == 0) {
            debugMarkerSupported = true;
        }

        // Clamp mirror wrap mode supported
        if (strcmp(extList[i], (const char *)"GL_EXT_texture_mirror_clamp") == 0) texClampMirrorSupported = true;
    }

#ifdef _MSC_VER
    free(extList);
#endif

#if defined(GRAPHICS_API_OPENGL_ES2)
    if (vaoSupported) TraceLog(LOG_INFO, "[EXTENSION] VAO extension detected, VAO functions initialized successfully");
    else TraceLog(LOG_WARNING, "[EXTENSION] VAO extension not found, VAO usage not supported");

    if (texNPOTSupported) TraceLog(LOG_INFO, "[EXTENSION] NPOT textures extension detected, full NPOT textures supported");
    else TraceLog(LOG_WARNING, "[EXTENSION] NPOT textures extension not found, limited NPOT support (no-mipmaps, no-repeat)");
#endif

    if (texCompDXTSupported) TraceLog(LOG_INFO, "[EXTENSION] DXT compressed textures supported");
    if (texCompETC1Supported) TraceLog(LOG_INFO, "[EXTENSION] ETC1 compressed textures supported");
    if (texCompETC2Supported) TraceLog(LOG_INFO, "[EXTENSION] ETC2/EAC compressed textures supported");
    if (texCompPVRTSupported) TraceLog(LOG_INFO, "[EXTENSION] PVRT compressed textures supported");
    if (texCompASTCSupported) TraceLog(LOG_INFO, "[EXTENSION] ASTC compressed textures supported");

    if (texAnisotropicFilterSupported) TraceLog(LOG_INFO, "[EXTENSION] Anisotropic textures filtering supported (max: %.0fX)", maxAnisotropicLevel);
    if (texClampMirrorSupported) TraceLog(LOG_INFO, "[EXTENSION] Clamp mirror wrap texture mode supported");

    if (debugMarkerSupported) TraceLog(LOG_INFO, "[EXTENSION] Debug Marker supported");

    // Initialize buffers, default shaders and default textures
    //----------------------------------------------------------

    // Init default white texture
    unsigned char pixels[4] = { 255, 255, 255, 255 };   // 1 pixel RGBA (4 bytes)

    whiteTexture = rlLoadTexture(pixels, 1, 1, UNCOMPRESSED_R8G8B8A8, 1);

    if (whiteTexture != 0) TraceLog(LOG_INFO, "[TEX ID %i] Base white texture loaded successfully", whiteTexture);
    else TraceLog(LOG_WARNING, "Base white texture could not be loaded");

    // Init default Shader (customized for GL 3.3 and ES2)
    defaultShader = LoadShaderDefault();
    currentShader = defaultShader;

    // Init default vertex arrays buffers (lines, triangles, quads)
    LoadBuffersDefault();

    // Init temp vertex buffer, used when transformation required (translate, rotate, scale)
    tempBuffer = (Vector3 *)malloc(sizeof(Vector3)*TEMP_VERTEX_BUFFER_SIZE);

    for (int i = 0; i < TEMP_VERTEX_BUFFER_SIZE; i++) tempBuffer[i] = Vector3Zero();

    // Init draw calls tracking system
    draws = (DrawCall *)malloc(sizeof(DrawCall)*MAX_DRAWS_BY_TEXTURE);

    for (int i = 0; i < MAX_DRAWS_BY_TEXTURE; i++)
    {
        draws[i].textureId = 0;
        draws[i].vertexCount = 0;
    }

    drawsCounter = 1;
    draws[drawsCounter - 1].textureId = whiteTexture;
    currentDrawMode = RL_TRIANGLES;     // Set default draw mode

    // Init internal matrix stack (emulating OpenGL 1.1)
    for (int i = 0; i < MATRIX_STACK_SIZE; i++) stack[i] = MatrixIdentity();

    // Init internal projection and modelview matrices
    projection = MatrixIdentity();
    modelview = MatrixIdentity();
    currentMatrix = &modelview;
#endif      // defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

    // Initialize OpenGL default states
    //----------------------------------------------------------

    // Init state: Depth test
    glDepthFunc(GL_LEQUAL);                                 // Type of depth testing to apply
    glDisable(GL_DEPTH_TEST);                               // Disable depth testing for 2D (only used for 3D)

    // Init state: Blending mode
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);      // Color blending function (how colors are mixed)
    glEnable(GL_BLEND);                                     // Enable color blending (required to work with transparencies)

    // Init state: Culling
    // NOTE: All shapes/models triangles are drawn CCW
    glCullFace(GL_BACK);                                    // Cull the back face (default)
    glFrontFace(GL_CCW);                                    // Front face are defined counter clockwise (default)
    glEnable(GL_CULL_FACE);                                 // Enable backface culling

#if defined(GRAPHICS_API_OPENGL_11)
    // Init state: Color hints (deprecated in OpenGL 3.0+)
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);      // Improve quality of color and texture coordinate interpolation
    glShadeModel(GL_SMOOTH);                                // Smooth shading between vertex (vertex colors interpolation)
#endif

    // Init state: Color/Depth buffers clear
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);                   // Set clear color (black)
    glClearDepth(1.0f);                                     // Set clear depth value (default)
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);     // Clear color and depth buffers (depth buffer required for 3D)

    // Store screen size into global variables
    screenWidth = width;
    screenHeight = height;

    TraceLog(LOG_INFO, "OpenGL default states initialized successfully");
}

// Vertex Buffer Object deinitialization (memory free)
void rlglClose(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    UnloadShaderDefault();              // Unload default shader
    UnloadBuffersDefault();             // Unload default buffers (lines, triangles, quads)  
    glDeleteTextures(1, &whiteTexture); // Unload default texture
    
    TraceLog(LOG_INFO, "[TEX ID %i] Unloaded texture data (base white texture) from VRAM", whiteTexture);

    free(draws);
#endif
}

// Drawing batches: triangles, quads, lines
void rlglDraw(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: In a future version, models could be stored in a stack...
    //for (int i = 0; i < modelsCount; i++) rlDrawMesh(models[i]->mesh, models[i]->material, models[i]->transform);

    // NOTE: Default buffers upload and draw
    UpdateBuffersDefault();
    DrawBuffersDefault();       // NOTE: Stereo rendering is checked inside
#endif
}

// Returns current OpenGL version
int rlGetVersion(void)
{
#if defined(GRAPHICS_API_OPENGL_11)
    return OPENGL_11;
#elif defined(GRAPHICS_API_OPENGL_21)
    #if defined(__APPLE__)
        return OPENGL_33;   // NOTE: Force OpenGL 3.3 on OSX
    #else
        return OPENGL_21;
    #endif
#elif defined(GRAPHICS_API_OPENGL_33)
    return OPENGL_33;
#elif defined(GRAPHICS_API_OPENGL_ES2)
    return OPENGL_ES_20;
#endif
}

// Set debug marker
void rlSetDebugMarker(const char *text)
{
    if(debugMarkerSupported) glInsertEventMarkerEXT(0, text);   // 0 terminated string
}

// Load OpenGL extensions
// NOTE: External loader function could be passed as a pointer
void rlLoadExtensions(void *loader)
{
#if defined(GRAPHICS_API_OPENGL_21) || defined(GRAPHICS_API_OPENGL_33)
    // NOTE: glad is generated and contains only required OpenGL 3.3 Core extensions (and lower versions)
    #if !defined(__APPLE__)
        if (!gladLoadGLLoader((GLADloadproc)loader)) TraceLog(LOG_WARNING, "GLAD: Cannot load OpenGL extensions");
        else TraceLog(LOG_INFO, "GLAD: OpenGL extensions loaded successfully");

        #if defined(GRAPHICS_API_OPENGL_21)
        if (GLAD_GL_VERSION_2_1) TraceLog(LOG_INFO, "OpenGL 2.1 profile supported");
        #elif defined(GRAPHICS_API_OPENGL_33)
        if(GLAD_GL_VERSION_3_3) TraceLog(LOG_INFO, "OpenGL 3.3 Core profile supported");
        else TraceLog(LOG_ERROR, "OpenGL 3.3 Core profile not supported");
        #endif
    #endif

    // With GLAD, we can check if an extension is supported using the GLAD_GL_xxx booleans
    //if (GLAD_GL_ARB_vertex_array_object) // Use GL_ARB_vertex_array_object
#endif
}

// Get world coordinates from screen coordinates
Vector3 rlUnproject(Vector3 source, Matrix proj, Matrix view)
{
    Vector3 result = { 0.0f, 0.0f, 0.0f };

    // Calculate unproject matrix (multiply view patrix by projection matrix) and invert it
    Matrix matViewProj = MatrixMultiply(view, proj);
    MatrixInvert(&matViewProj);

    // Create quaternion from source point
    Quaternion quat = { source.x, source.y, source.z, 1.0f };

    // Multiply quat point by unproject matrix
    QuaternionTransform(&quat, matViewProj);

    // Normalized world points in vectors
    result.x = quat.x/quat.w;
    result.y = quat.y/quat.w;
    result.z = quat.z/quat.w;

    return result;
}

// Convert image data to OpenGL texture (returns OpenGL valid Id)
unsigned int rlLoadTexture(void *data, int width, int height, int format, int mipmapCount)
{
    glBindTexture(GL_TEXTURE_2D, 0);    // Free any old binding

    GLuint id = 0;

    // Check texture format support by OpenGL 1.1 (compressed textures not supported)
#if defined(GRAPHICS_API_OPENGL_11)
    if (format >= COMPRESSED_DXT1_RGB)
    {
        TraceLog(LOG_WARNING, "OpenGL 1.1 does not support GPU compressed texture formats");
        return id;
    }
#endif

    if ((!texCompDXTSupported) && ((format == COMPRESSED_DXT1_RGB) || (format == COMPRESSED_DXT1_RGBA) ||
        (format == COMPRESSED_DXT3_RGBA) || (format == COMPRESSED_DXT5_RGBA)))
    {
        TraceLog(LOG_WARNING, "DXT compressed texture format not supported");
        return id;
    }
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if ((!texCompETC1Supported) && (format == COMPRESSED_ETC1_RGB))
    {
        TraceLog(LOG_WARNING, "ETC1 compressed texture format not supported");
        return id;
    }

    if ((!texCompETC2Supported) && ((format == COMPRESSED_ETC2_RGB) || (format == COMPRESSED_ETC2_EAC_RGBA)))
    {
        TraceLog(LOG_WARNING, "ETC2 compressed texture format not supported");
        return id;
    }

    if ((!texCompPVRTSupported) && ((format == COMPRESSED_PVRT_RGB) || (format == COMPRESSED_PVRT_RGBA)))
    {
        TraceLog(LOG_WARNING, "PVRT compressed texture format not supported");
        return id;
    }

    if ((!texCompASTCSupported) && ((format == COMPRESSED_ASTC_4x4_RGBA) || (format == COMPRESSED_ASTC_8x8_RGBA)))
    {
        TraceLog(LOG_WARNING, "ASTC compressed texture format not supported");
        return id;
    }
#endif

    glGenTextures(1, &id);              // Generate Pointer to the texture

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    //glActiveTexture(GL_TEXTURE0);     // If not defined, using GL_TEXTURE0 by default (shader texture)
#endif

    glBindTexture(GL_TEXTURE_2D, id);

#if defined(GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_21) || defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: on OpenGL ES 2.0 (WebGL), internalFormat must match format and options allowed are: GL_LUMINANCE, GL_RGB, GL_RGBA
    switch (format)
    {
        case UNCOMPRESSED_GRAYSCALE: glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_GRAY_ALPHA: glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width, height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G6B5: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G5B5A1: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, (unsigned short *)data); break;
        case UNCOMPRESSED_R4G4B4A4: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8A8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
    #if defined(GRAPHICS_API_OPENGL_21)
        case COMPRESSED_DXT1_RGB: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, mipmapCount); break;
        case COMPRESSED_DXT1_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, mipmapCount); break;
        case COMPRESSED_DXT3_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, mipmapCount); break;
        case COMPRESSED_DXT5_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, mipmapCount); break;
    #endif
    #if defined(GRAPHICS_API_OPENGL_ES2)
        case UNCOMPRESSED_R32G32B32: if (texFloatSupported) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_FLOAT, (float *)data); break;  // NOTE: Requires extension OES_texture_float
        case COMPRESSED_DXT1_RGB: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, mipmapCount); break;
        case COMPRESSED_DXT1_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, mipmapCount); break;
        case COMPRESSED_DXT3_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, mipmapCount); break;      // NOTE: Not supported by WebGL
        case COMPRESSED_DXT5_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, mipmapCount); break;      // NOTE: Not supported by WebGL
        case COMPRESSED_ETC1_RGB: if (texCompETC1Supported) LoadTextureCompressed((unsigned char *)data, width, height, GL_ETC1_RGB8_OES, mipmapCount); break;                      // NOTE: Requires OpenGL ES 2.0 or OpenGL 4.3
        case COMPRESSED_ETC2_RGB: if (texCompETC2Supported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGB8_ETC2, mipmapCount); break;               // NOTE: Requires OpenGL ES 3.0 or OpenGL 4.3
        case COMPRESSED_ETC2_EAC_RGBA: if (texCompETC2Supported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA8_ETC2_EAC, mipmapCount); break;     // NOTE: Requires OpenGL ES 3.0 or OpenGL 4.3
        case COMPRESSED_PVRT_RGB: if (texCompPVRTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG, mipmapCount); break;    // NOTE: Requires PowerVR GPU
        case COMPRESSED_PVRT_RGBA: if (texCompPVRTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, mipmapCount); break;  // NOTE: Requires PowerVR GPU
        case COMPRESSED_ASTC_4x4_RGBA: if (texCompASTCSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_ASTC_4x4_KHR, mipmapCount); break;  // NOTE: Requires OpenGL ES 3.1 or OpenGL 4.3
        case COMPRESSED_ASTC_8x8_RGBA: if (texCompASTCSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_ASTC_8x8_KHR, mipmapCount); break;  // NOTE: Requires OpenGL ES 3.1 or OpenGL 4.3
    #endif
        default: TraceLog(LOG_WARNING, "Texture format not supported"); break;
    }
#elif defined(GRAPHICS_API_OPENGL_33)
    // NOTE: We define internal (GPU) format as GL_RGBA8 (probably BGRA8 in practice, driver takes care)
    // NOTE: On embedded systems, we let the driver choose the best internal format

    // Support for multiple color modes (16bit color modes and grayscale)
    // (sized)internalFormat    format          type
    // GL_R                     GL_RED      GL_UNSIGNED_BYTE
    // GL_RGB565                GL_RGB      GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_5_6_5
    // GL_RGB5_A1               GL_RGBA     GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_5_5_5_1
    // GL_RGBA4                 GL_RGBA     GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_4_4_4_4
    // GL_RGBA8                 GL_RGBA     GL_UNSIGNED_BYTE
    // GL_RGB8                  GL_RGB      GL_UNSIGNED_BYTE

    switch (format)
    {
        case UNCOMPRESSED_GRAYSCALE:
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, (unsigned char *)data);

            // With swizzleMask we define how a one channel texture will be mapped to RGBA
            // Required GL >= 3.3 or EXT_texture_swizzle/ARB_texture_swizzle
            GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

            TraceLog(LOG_INFO, "[TEX ID %i] Grayscale texture loaded and swizzled", id);
        } break;
        case UNCOMPRESSED_GRAY_ALPHA:
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, (unsigned char *)data);

            GLint swizzleMask[] = { GL_RED, GL_RED, GL_RED, GL_GREEN };
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
        } break;

        case UNCOMPRESSED_R5G6B5: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G5B5A1: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, (unsigned short *)data); break;
        case UNCOMPRESSED_R4G4B4A4: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8A8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R32G32B32: if (texFloatSupported) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, (float *)data); break;
        case COMPRESSED_DXT1_RGB: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, mipmapCount); break;
        case COMPRESSED_DXT1_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, mipmapCount); break;
        case COMPRESSED_DXT3_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, mipmapCount); break;
        case COMPRESSED_DXT5_RGBA: if (texCompDXTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, mipmapCount); break;
        case COMPRESSED_ETC1_RGB: if (texCompETC1Supported) LoadTextureCompressed((unsigned char *)data, width, height, GL_ETC1_RGB8_OES, mipmapCount); break;                      // NOTE: Requires OpenGL ES 2.0 or OpenGL 4.3
        case COMPRESSED_ETC2_RGB: if (texCompETC2Supported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGB8_ETC2, mipmapCount); break;               // NOTE: Requires OpenGL ES 3.0 or OpenGL 4.3
        case COMPRESSED_ETC2_EAC_RGBA: if (texCompETC2Supported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA8_ETC2_EAC, mipmapCount); break;     // NOTE: Requires OpenGL ES 3.0 or OpenGL 4.3
        case COMPRESSED_PVRT_RGB: if (texCompPVRTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG, mipmapCount); break;    // NOTE: Requires PowerVR GPU
        case COMPRESSED_PVRT_RGBA: if (texCompPVRTSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, mipmapCount); break;  // NOTE: Requires PowerVR GPU
        case COMPRESSED_ASTC_4x4_RGBA: if (texCompASTCSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_ASTC_4x4_KHR, mipmapCount); break;  // NOTE: Requires OpenGL ES 3.1 or OpenGL 4.3
        case COMPRESSED_ASTC_8x8_RGBA: if (texCompASTCSupported) LoadTextureCompressed((unsigned char *)data, width, height, GL_COMPRESSED_RGBA_ASTC_8x8_KHR, mipmapCount); break;  // NOTE: Requires OpenGL ES 3.1 or OpenGL 4.3
        default: TraceLog(LOG_WARNING, "Texture format not recognized"); break;
    }
#endif

    // Texture parameters configuration
    // NOTE: glTexParameteri does NOT affect texture uploading, just the way it's used
#if defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: OpenGL ES 2.0 with no GL_OES_texture_npot support (i.e. WebGL) has limited NPOT support, so CLAMP_TO_EDGE must be used
    if (texNPOTSupported)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);       // Set texture to repeat on x-axis
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);       // Set texture to repeat on y-axis
    }
    else
    {
        // NOTE: If using negative texture coordinates (LoadOBJ()), it does not work!
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);       // Set texture to clamp on x-axis
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);       // Set texture to clamp on y-axis
    }
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);       // Set texture to repeat on x-axis
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);       // Set texture to repeat on y-axis
#endif

    // Magnification and minification filters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);  // Alternative: GL_LINEAR
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // Alternative: GL_LINEAR

#if defined(GRAPHICS_API_OPENGL_33)
    if (mipmapCount > 1)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);   // Activate Trilinear filtering for mipmaps (must be available)
    }
#endif

    // At this point we have the texture loaded in GPU and texture parameters configured

    // NOTE: If mipmaps were not in data, they are not generated automatically

    // Unbind current texture
    glBindTexture(GL_TEXTURE_2D, 0);

    if (id > 0) TraceLog(LOG_INFO, "[TEX ID %i] Texture created successfully (%ix%i)", id, width, height);
    else TraceLog(LOG_WARNING, "Texture could not be created");

    return id;
}

// Update already loaded texture in GPU with new data
void rlUpdateTexture(unsigned int id, int width, int height, int format, const void *data)
{
    glBindTexture(GL_TEXTURE_2D, id);

#if defined(GRAPHICS_API_OPENGL_33)
    switch (format)
    {
        case UNCOMPRESSED_GRAYSCALE: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_GRAY_ALPHA: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RG, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G6B5: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G5B5A1: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, (unsigned short *)data); break;
        case UNCOMPRESSED_R4G4B4A4: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8A8: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        default: TraceLog(LOG_WARNING, "Texture format updating not supported"); break;
    }
#elif defined(GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: on OpenGL ES 2.0 (WebGL), internalFormat must match format and options allowed are: GL_LUMINANCE, GL_RGB, GL_RGBA
    switch (format)
    {
        case UNCOMPRESSED_GRAYSCALE: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_GRAY_ALPHA: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G6B5: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        case UNCOMPRESSED_R5G5B5A1: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, (unsigned short *)data); break;
        case UNCOMPRESSED_R4G4B4A4: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, (unsigned short *)data); break;
        case UNCOMPRESSED_R8G8B8A8: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char *)data); break;
        default: TraceLog(LOG_WARNING, "Texture format updating not supported"); break;
    }
#endif
}

// Unload texture from GPU memory
void rlUnloadTexture(unsigned int id)
{
    if (id > 0) glDeleteTextures(1, &id);
}


// Load a texture to be used for rendering (fbo with color and depth attachments)
RenderTexture2D rlLoadRenderTexture(int width, int height)
{
    RenderTexture2D target;

    target.id = 0;

    target.texture.id = 0;
    target.texture.width = width;
    target.texture.height = height;
    target.texture.format = UNCOMPRESSED_R8G8B8A8;
    target.texture.mipmaps = 1;

    target.depth.id = 0;
    target.depth.width = width;
    target.depth.height = height;
    target.depth.format = 19;       //DEPTH_COMPONENT_24BIT
    target.depth.mipmaps = 1;

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // Create the texture that will serve as the color attachment for the framebuffer
    glGenTextures(1, &target.texture.id);
    glBindTexture(GL_TEXTURE_2D, target.texture.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

#if defined(GRAPHICS_API_OPENGL_21) || defined(GRAPHICS_API_OPENGL_ES2)
    #define USE_DEPTH_RENDERBUFFER
#else
    #define USE_DEPTH_TEXTURE
#endif

#if defined(USE_DEPTH_RENDERBUFFER)
    // Create the renderbuffer that will serve as the depth attachment for the framebuffer.
    glGenRenderbuffers(1, &target.depth.id);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depth.id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);    // GL_DEPTH_COMPONENT24 not supported on Android
#elif defined(USE_DEPTH_TEXTURE)
    // NOTE: We can also use a texture for depth buffer (GL_ARB_depth_texture/GL_OES_depth_texture extension required)
    // A renderbuffer is simpler than a texture and could offer better performance on embedded devices
    glGenTextures(1, &target.depth.id);
    glBindTexture(GL_TEXTURE_2D, target.depth.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif

    // Create the framebuffer object
    glGenFramebuffers(1, &target.id);
    glBindFramebuffer(GL_FRAMEBUFFER, target.id);

    // Attach color texture and depth renderbuffer to FBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture.id, 0);
#if defined(USE_DEPTH_RENDERBUFFER)
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.depth.id);
#elif defined(USE_DEPTH_TEXTURE)
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, target.depth.id, 0);
#endif

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        TraceLog(LOG_WARNING, "Framebuffer object could not be created...");

        switch (status)
        {
            case GL_FRAMEBUFFER_UNSUPPORTED: TraceLog(LOG_WARNING, "Framebuffer is unsupported"); break;
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: TraceLog(LOG_WARNING, "Framebuffer incomplete attachment"); break;
#if defined(GRAPHICS_API_OPENGL_ES2)
            case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS: TraceLog(LOG_WARNING, "Framebuffer incomplete dimensions"); break;
#endif
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: TraceLog(LOG_WARNING, "Framebuffer incomplete missing attachment"); break;
            default: break;
        }

        glDeleteTextures(1, &target.texture.id);
        glDeleteTextures(1, &target.depth.id);
        glDeleteFramebuffers(1, &target.id);
    }
    else TraceLog(LOG_INFO, "[FBO ID %i] Framebuffer object created successfully", target.id);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif

    return target;
}

// Generate mipmap data for selected texture
void rlGenerateMipmaps(Texture2D *texture)
{
    glBindTexture(GL_TEXTURE_2D, texture->id);

    // Check if texture is power-of-two (POT)
    bool texIsPOT = false;

    if (((texture->width > 0) && ((texture->width & (texture->width - 1)) == 0)) &&
        ((texture->height > 0) && ((texture->height & (texture->height - 1)) == 0))) texIsPOT = true;

    if ((texIsPOT) || (texNPOTSupported))
    {
#if defined(GRAPHICS_API_OPENGL_11)
        // Compute required mipmaps
        void *data = rlReadTexturePixels(*texture);

        // NOTE: data size is reallocated to fit mipmaps data
        // NOTE: CPU mipmap generation only supports RGBA 32bit data
        int mipmapCount = GenerateMipmaps(data, texture->width, texture->height);

        int size = texture->width*texture->height*4;  // RGBA 32bit only
        int offset = size;

        int mipWidth = texture->width/2;
        int mipHeight = texture->height/2;

        // Load the mipmaps
        for (int level = 1; level < mipmapCount; level++)
        {
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, mipWidth, mipHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data + offset);

            size = mipWidth*mipHeight*4;
            offset += size;

            mipWidth /= 2;
            mipHeight /= 2;
        }

        TraceLog(LOG_WARNING, "[TEX ID %i] Mipmaps generated manually on CPU side", texture->id);

        // NOTE: Once mipmaps have been generated and data has been uploaded to GPU VRAM, we can discard RAM data
        free(data);

        texture->mipmaps = mipmapCount + 1;
#endif

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
        //glHint(GL_GENERATE_MIPMAP_HINT, GL_DONT_CARE);   // Hint for mipmaps generation algorythm: GL_FASTEST, GL_NICEST, GL_DONT_CARE
        glGenerateMipmap(GL_TEXTURE_2D);    // Generate mipmaps automatically
        TraceLog(LOG_INFO, "[TEX ID %i] Mipmaps generated automatically", texture->id);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);   // Activate Trilinear filtering for mipmaps

        #define MIN(a,b) (((a)<(b))?(a):(b))
        #define MAX(a,b) (((a)>(b))?(a):(b))

        texture->mipmaps =  1 + (int)floor(log(MAX(texture->width, texture->height))/log(2));
#endif
    }
    else TraceLog(LOG_WARNING, "[TEX ID %i] Mipmaps can not be generated", texture->id);

    glBindTexture(GL_TEXTURE_2D, 0);
}

// Upload vertex data into a VAO (if supported) and VBO
// TODO: Check if mesh has already been loaded in GPU
void rlLoadMesh(Mesh *mesh, bool dynamic)
{
    mesh->vaoId = 0;        // Vertex Array Object
    mesh->vboId[0] = 0;     // Vertex positions VBO
    mesh->vboId[1] = 0;     // Vertex texcoords VBO
    mesh->vboId[2] = 0;     // Vertex normals VBO
    mesh->vboId[3] = 0;     // Vertex colors VBO
    mesh->vboId[4] = 0;     // Vertex tangents VBO
    mesh->vboId[5] = 0;     // Vertex texcoords2 VBO
    mesh->vboId[6] = 0;     // Vertex indices VBO

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    int drawHint = GL_STATIC_DRAW;
    if (dynamic) drawHint = GL_DYNAMIC_DRAW;

    if (vaoSupported)
    {
        // Initialize Quads VAO (Buffer A)
        glGenVertexArrays(1, &mesh->vaoId);
        glBindVertexArray(mesh->vaoId);
    }

    // NOTE: Attributes must be uploaded considering default locations points

    // Enable vertex attributes: position (shader-location = 0)
    glGenBuffers(1, &mesh->vboId[0]);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vboId[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*mesh->vertexCount, mesh->vertices, drawHint);
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, 0, 0);
    glEnableVertexAttribArray(0);

    // Enable vertex attributes: texcoords (shader-location = 1)
    glGenBuffers(1, &mesh->vboId[1]);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vboId[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*mesh->vertexCount, mesh->texcoords, drawHint);
    glVertexAttribPointer(1, 2, GL_FLOAT, 0, 0, 0);
    glEnableVertexAttribArray(1);

    // Enable vertex attributes: normals (shader-location = 2)
    if (mesh->normals != NULL)
    {
        glGenBuffers(1, &mesh->vboId[2]);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vboId[2]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*mesh->vertexCount, mesh->normals, drawHint);
        glVertexAttribPointer(2, 3, GL_FLOAT, 0, 0, 0);
        glEnableVertexAttribArray(2);
    }
    else
    {
        // Default color vertex attribute set to WHITE
        glVertexAttrib3f(2, 1.0f, 1.0f, 1.0f);
        glDisableVertexAttribArray(2);
    }

    // Default color vertex attribute (shader-location = 3)
    if (mesh->colors != NULL)
    {
        glGenBuffers(1, &mesh->vboId[3]);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vboId[3]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(unsigned char)*4*mesh->vertexCount, mesh->colors, drawHint);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
        glEnableVertexAttribArray(3);
    }
    else
    {
        // Default color vertex attribute set to WHITE
        glVertexAttrib4f(3, 1.0f, 1.0f, 1.0f, 1.0f);
        glDisableVertexAttribArray(3);
    }

    // Default tangent vertex attribute (shader-location = 4)
    if (mesh->tangents != NULL)
    {
        glGenBuffers(1, &mesh->vboId[4]);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vboId[4]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*mesh->vertexCount, mesh->tangents, drawHint);
        glVertexAttribPointer(4, 3, GL_FLOAT, 0, 0, 0);
        glEnableVertexAttribArray(4);
    }
    else
    {
        // Default tangents vertex attribute
        glVertexAttrib3f(4, 0.0f, 0.0f, 0.0f);
        glDisableVertexAttribArray(4);
    }

    // Default texcoord2 vertex attribute (shader-location = 5)
    if (mesh->texcoords2 != NULL)
    {
        glGenBuffers(1, &mesh->vboId[5]);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vboId[5]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*mesh->vertexCount, mesh->texcoords2, drawHint);
        glVertexAttribPointer(5, 2, GL_FLOAT, 0, 0, 0);
        glEnableVertexAttribArray(5);
    }
    else
    {
        // Default tangents vertex attribute
        glVertexAttrib2f(5, 0.0f, 0.0f);
        glDisableVertexAttribArray(5);
    }

    if (mesh->indices != NULL)
    {
        glGenBuffers(1, &mesh->vboId[6]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->vboId[6]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned short)*mesh->triangleCount*3, mesh->indices, GL_STATIC_DRAW);
    }

    if (vaoSupported)
    {
        if (mesh->vaoId > 0) TraceLog(LOG_INFO, "[VAO ID %i] Mesh uploaded successfully to VRAM (GPU)", mesh->vaoId);
        else TraceLog(LOG_WARNING, "Mesh could not be uploaded to VRAM (GPU)");
    }
    else
    {
        TraceLog(LOG_INFO, "[VBOs] Mesh uploaded successfully to VRAM (GPU)");
    }
#endif
}

// Update vertex data on GPU (upload new data to one buffer)
void rlUpdateMesh(Mesh mesh, int buffer, int numVertex)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // Activate mesh VAO
    if (vaoSupported) glBindVertexArray(mesh.vaoId);

    switch (buffer)
    {
        case 0:     // Update vertices (vertex position)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[0]);
            if (numVertex >= mesh.vertexCount) glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*numVertex, mesh.vertices, GL_DYNAMIC_DRAW);
            else glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*numVertex, mesh.vertices);

        } break;
        case 1:     // Update texcoords (vertex texture coordinates)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[1]);
            if (numVertex >= mesh.vertexCount) glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*numVertex, mesh.texcoords, GL_DYNAMIC_DRAW);
            else glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*2*numVertex, mesh.texcoords);

        } break;
        case 2:     // Update normals (vertex normals)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[2]);
            if (numVertex >= mesh.vertexCount) glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*numVertex, mesh.normals, GL_DYNAMIC_DRAW);
            else glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*numVertex, mesh.normals);

        } break;
        case 3:     // Update colors (vertex colors)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[3]);
            if (numVertex >= mesh.vertexCount) glBufferData(GL_ARRAY_BUFFER, sizeof(float)*4*numVertex, mesh.colors, GL_DYNAMIC_DRAW);
            else glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(unsigned char)*4*numVertex, mesh.colors);

        } break;
        case 4:     // Update tangents (vertex tangents)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[4]);
            if (numVertex >= mesh.vertexCount) glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*numVertex, mesh.tangents, GL_DYNAMIC_DRAW);
            else glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*numVertex, mesh.tangents);
        } break;
        case 5:     // Update texcoords2 (vertex second texture coordinates)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[5]);
            if (numVertex >= mesh.vertexCount) glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*numVertex, mesh.texcoords2, GL_DYNAMIC_DRAW);
            else glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*2*numVertex, mesh.texcoords2);
        } break;
        default: break;
    }

    // Unbind the current VAO
    if (vaoSupported) glBindVertexArray(0);

    // Another option would be using buffer mapping...
    //mesh.vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
    // Now we can modify vertices
    //glUnmapBuffer(GL_ARRAY_BUFFER);
#endif
}

// Draw a 3d mesh with material and transform
void rlDrawMesh(Mesh mesh, Material material, Matrix transform)
{
#if defined(GRAPHICS_API_OPENGL_11)
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, material.maps[MAP_DIFFUSE].texture.id);

    // NOTE: On OpenGL 1.1 we use Vertex Arrays to draw model
    glEnableClientState(GL_VERTEX_ARRAY);                   // Enable vertex array
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);            // Enable texture coords array
    if (mesh.normals != NULL) glEnableClientState(GL_NORMAL_ARRAY);     // Enable normals array
    if (mesh.colors != NULL) glEnableClientState(GL_COLOR_ARRAY);       // Enable colors array

    glVertexPointer(3, GL_FLOAT, 0, mesh.vertices);         // Pointer to vertex coords array
    glTexCoordPointer(2, GL_FLOAT, 0, mesh.texcoords);      // Pointer to texture coords array
    if (mesh.normals != NULL) glNormalPointer(GL_FLOAT, 0, mesh.normals);           // Pointer to normals array
    if (mesh.colors != NULL) glColorPointer(4, GL_UNSIGNED_BYTE, 0, mesh.colors);   // Pointer to colors array

    rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(transform));
        rlColor4ub(material.maps[MAP_DIFFUSE].color.r, material.maps[MAP_DIFFUSE].color.g, material.maps[MAP_DIFFUSE].color.b, material.maps[MAP_DIFFUSE].color.a);

        if (mesh.indices != NULL) glDrawElements(GL_TRIANGLES, mesh.triangleCount*3, GL_UNSIGNED_SHORT, mesh.indices);
        else glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    rlPopMatrix();

    glDisableClientState(GL_VERTEX_ARRAY);                  // Disable vertex array
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);           // Disable texture coords array
    if (mesh.normals != NULL) glDisableClientState(GL_NORMAL_ARRAY);    // Disable normals array
    if (mesh.colors != NULL) glDisableClientState(GL_NORMAL_ARRAY);     // Disable colors array

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // Bind shader program
    glUseProgram(material.shader.id);   

    // Matrices and other values required by shader
    //-----------------------------------------------------
    // Calculate and send to shader model matrix (used by PBR shader)
    if (material.shader.locs[LOC_MATRIX_MODEL] != -1) SetShaderValueMatrix(material.shader, material.shader.locs[LOC_MATRIX_MODEL], transform);
    
    // Upload to shader material.colDiffuse
    if (material.shader.locs[LOC_COLOR_DIFFUSE] != -1)
        glUniform4f(material.shader.locs[LOC_COLOR_DIFFUSE], (float)material.maps[MAP_DIFFUSE].color.r/255.0f, 
                                                           (float)material.maps[MAP_DIFFUSE].color.g/255.0f, 
                                                           (float)material.maps[MAP_DIFFUSE].color.b/255.0f, 
                                                           (float)material.maps[MAP_DIFFUSE].color.a/255.0f);

    // Upload to shader material.colSpecular (if available)
    if (material.shader.locs[LOC_COLOR_SPECULAR] != -1) 
        glUniform4f(material.shader.locs[LOC_COLOR_SPECULAR], (float)material.maps[MAP_SPECULAR].color.r/255.0f, 
                                                               (float)material.maps[MAP_SPECULAR].color.g/255.0f, 
                                                               (float)material.maps[MAP_SPECULAR].color.b/255.0f, 
                                                               (float)material.maps[MAP_SPECULAR].color.a/255.0f);
                                                               
    if (material.shader.locs[LOC_MATRIX_VIEW] != -1) SetShaderValueMatrix(material.shader, material.shader.locs[LOC_MATRIX_VIEW], modelview);
    if (material.shader.locs[LOC_MATRIX_PROJECTION] != -1) SetShaderValueMatrix(material.shader, material.shader.locs[LOC_MATRIX_PROJECTION], projection);

    // At this point the modelview matrix just contains the view matrix (camera)
    // That's because Begin3dMode() sets it an no model-drawing function modifies it, all use rlPushMatrix() and rlPopMatrix()
    Matrix matView = modelview;         // View matrix (camera)
    Matrix matProjection = projection;  // Projection matrix (perspective)

    // Calculate model-view matrix combining matModel and matView
    Matrix matModelView = MatrixMultiply(transform, matView);           // Transform to camera-space coordinates
    //-----------------------------------------------------

    // Bind active texture maps (if available)
    for (int i = 0; i < MAX_MATERIAL_MAPS; i++)
    {
        if (material.maps[i].texture.id > 0)
        {
            glActiveTexture(GL_TEXTURE0 + i);
            if ((i == MAP_IRRADIANCE) || (i == MAP_PREFILTER) || (i == MAP_CUBEMAP)) glBindTexture(GL_TEXTURE_CUBE_MAP, material.maps[i].texture.id);
            else glBindTexture(GL_TEXTURE_2D, material.maps[i].texture.id);

            glUniform1i(material.shader.locs[LOC_MAP_DIFFUSE + i], i);
        }
    }

    // Bind vertex array objects (or VBOs)
    if (vaoSupported) glBindVertexArray(mesh.vaoId);
    else
    {
        // TODO: Simplify VBO binding into a for loop

        // Bind mesh VBO data: vertex position (shader-location = 0)
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[0]);
        glVertexAttribPointer(material.shader.locs[LOC_VERTEX_POSITION], 3, GL_FLOAT, 0, 0, 0);
        glEnableVertexAttribArray(material.shader.locs[LOC_VERTEX_POSITION]);

        // Bind mesh VBO data: vertex texcoords (shader-location = 1)
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[1]);
        glVertexAttribPointer(material.shader.locs[LOC_VERTEX_TEXCOORD01], 2, GL_FLOAT, 0, 0, 0);
        glEnableVertexAttribArray(material.shader.locs[LOC_VERTEX_TEXCOORD01]);

        // Bind mesh VBO data: vertex normals (shader-location = 2, if available)
        if (material.shader.locs[LOC_VERTEX_NORMAL] != -1)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[2]);
            glVertexAttribPointer(material.shader.locs[LOC_VERTEX_NORMAL], 3, GL_FLOAT, 0, 0, 0);
            glEnableVertexAttribArray(material.shader.locs[LOC_VERTEX_NORMAL]);
        }

        // Bind mesh VBO data: vertex colors (shader-location = 3, if available)
        if (material.shader.locs[LOC_VERTEX_COLOR] != -1)
        {
            if (mesh.vboId[3] != 0)
            {
                glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[3]);
                glVertexAttribPointer(material.shader.locs[LOC_VERTEX_COLOR], 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
                glEnableVertexAttribArray(material.shader.locs[LOC_VERTEX_COLOR]);
            }
            else
            {
                // Set default value for unused attribute
                // NOTE: Required when using default shader and no VAO support
                glVertexAttrib4f(material.shader.locs[LOC_VERTEX_COLOR], 1.0f, 1.0f, 1.0f, 1.0f);
                glDisableVertexAttribArray(material.shader.locs[LOC_VERTEX_COLOR]);
            }
        }

        // Bind mesh VBO data: vertex tangents (shader-location = 4, if available)
        if (material.shader.locs[LOC_VERTEX_TANGENT] != -1)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[4]);
            glVertexAttribPointer(material.shader.locs[LOC_VERTEX_TANGENT], 3, GL_FLOAT, 0, 0, 0);
            glEnableVertexAttribArray(material.shader.locs[LOC_VERTEX_TANGENT]);
        }

        // Bind mesh VBO data: vertex texcoords2 (shader-location = 5, if available)
        if (material.shader.locs[LOC_VERTEX_TEXCOORD02] != -1)
        {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[5]);
            glVertexAttribPointer(material.shader.locs[LOC_VERTEX_TEXCOORD02], 2, GL_FLOAT, 0, 0, 0);
            glEnableVertexAttribArray(material.shader.locs[LOC_VERTEX_TEXCOORD02]);
        }

        if (mesh.indices != NULL) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.vboId[6]);
    }

    int eyesCount = 1;
#if defined(SUPPORT_VR_SIMULATOR)
    if (vrStereoRender) eyesCount = 2;
#endif
    
    for (int eye = 0; eye < eyesCount; eye++)
    {
        if (eyesCount == 1) modelview = matModelView;
        #if defined(SUPPORT_VR_SIMULATOR)
        else SetStereoView(eye, matProjection, matModelView);
        #endif

        // Calculate model-view-projection matrix (MVP)
        Matrix matMVP = MatrixMultiply(modelview, projection);        // Transform to screen-space coordinates

        // Send combined model-view-projection matrix to shader
        glUniformMatrix4fv(material.shader.locs[LOC_MATRIX_MVP], 1, false, MatrixToFloat(matMVP));

        // Draw call!
        if (mesh.indices != NULL) glDrawElements(GL_TRIANGLES, mesh.triangleCount*3, GL_UNSIGNED_SHORT, 0); // Indexed vertices draw
        else glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    }
    
    // Unbind all binded texture maps
    for (int i = 0; i < MAX_MATERIAL_MAPS; i++)
    {
        glActiveTexture(GL_TEXTURE0 + i);       // Set shader active texture
        if ((i == MAP_IRRADIANCE) || (i == MAP_PREFILTER) || (i == MAP_CUBEMAP)) glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        else glBindTexture(GL_TEXTURE_2D, 0);   // Unbind current active texture
    }

    // Unind vertex array objects (or VBOs)
    if (vaoSupported) glBindVertexArray(0);
    else
    {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        if (mesh.indices != NULL) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    // Unbind shader program
    glUseProgram(0);

    // Restore projection/modelview matrices
    // NOTE: In stereo rendering matrices are being modified to fit every eye
    projection = matProjection;
    modelview = matView;
#endif
}

// Unload mesh data from CPU and GPU
void rlUnloadMesh(Mesh *mesh)
{
    if (mesh->vertices != NULL) free(mesh->vertices);
    if (mesh->texcoords != NULL) free(mesh->texcoords);
    if (mesh->normals != NULL) free(mesh->normals);
    if (mesh->colors != NULL) free(mesh->colors);
    if (mesh->tangents != NULL) free(mesh->tangents);
    if (mesh->texcoords2 != NULL) free(mesh->texcoords2);
    if (mesh->indices != NULL) free(mesh->indices);

    rlDeleteBuffers(mesh->vboId[0]);   // vertex
    rlDeleteBuffers(mesh->vboId[1]);   // texcoords
    rlDeleteBuffers(mesh->vboId[2]);   // normals
    rlDeleteBuffers(mesh->vboId[3]);   // colors
    rlDeleteBuffers(mesh->vboId[4]);   // tangents
    rlDeleteBuffers(mesh->vboId[5]);   // texcoords2
    rlDeleteBuffers(mesh->vboId[6]);   // indices

    rlDeleteVertexArrays(mesh->vaoId);
}

// Read screen pixel data (color buffer)
unsigned char *rlReadScreenPixels(int width, int height)
{
    unsigned char *screenData = (unsigned char *)calloc(width*height*4, sizeof(unsigned char));

    // NOTE: glReadPixels returns image flipped vertically -> (0,0) is the bottom left corner of the framebuffer
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, screenData);

    // Flip image vertically!
    unsigned char *imgData = (unsigned char *)malloc(width*height*sizeof(unsigned char)*4);

    for (int y = height - 1; y >= 0; y--)
    {
        for (int x = 0; x < (width*4); x++)
        {
            // Flip line
            imgData[((height - 1) - y)*width*4 + x] = screenData[(y*width*4) + x];

            // Set alpha component value to 255 (no trasparent image retrieval)
            // NOTE: Alpha value has already been applied to RGB in framebuffer, we don't need it!
            if (((x + 1)%4) == 0) imgData[((height - 1) - y)*width*4 + x] = 255;
        }
    }

    free(screenData);

    return imgData;     // NOTE: image data should be freed
}

// Read texture pixel data
// NOTE: glGetTexImage() is not available on OpenGL ES 2.0
// Texture2D width and height are required on OpenGL ES 2.0. There is no way to get it from texture id.
void *rlReadTexturePixels(Texture2D texture)
{
    void *pixels = NULL;

#if defined(GRAPHICS_API_OPENGL_11) || defined(GRAPHICS_API_OPENGL_33)
    glBindTexture(GL_TEXTURE_2D, texture.id);

    // NOTE: Using texture.id, we can retrieve some texture info (but not on OpenGL ES 2.0)
    /*
    int width, height, format;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    // Other texture info: GL_TEXTURE_RED_SIZE, GL_TEXTURE_GREEN_SIZE, GL_TEXTURE_BLUE_SIZE, GL_TEXTURE_ALPHA_SIZE
    */

    int glFormat = 0, glType = 0;

    unsigned int size = texture.width*texture.height;

    // NOTE: GL_LUMINANCE and GL_LUMINANCE_ALPHA are removed since OpenGL 3.1
    // Must be replaced by GL_RED and GL_RG on Core OpenGL 3.3

    switch (texture.format)
    {
#if defined(GRAPHICS_API_OPENGL_11)
        case UNCOMPRESSED_GRAYSCALE: pixels = (unsigned char *)malloc(size); glFormat = GL_LUMINANCE; glType = GL_UNSIGNED_BYTE; break;            // 8 bit per pixel (no alpha)
        case UNCOMPRESSED_GRAY_ALPHA: pixels = (unsigned char *)malloc(size*2); glFormat = GL_LUMINANCE_ALPHA; glType = GL_UNSIGNED_BYTE; break;   // 16 bpp (2 channels)
#elif defined(GRAPHICS_API_OPENGL_33)
        case UNCOMPRESSED_GRAYSCALE: pixels = (unsigned char *)malloc(size); glFormat = GL_RED; glType = GL_UNSIGNED_BYTE; break;
        case UNCOMPRESSED_GRAY_ALPHA: pixels = (unsigned char *)malloc(size*2); glFormat = GL_RG; glType = GL_UNSIGNED_BYTE; break;
#endif
        case UNCOMPRESSED_R5G6B5: pixels = (unsigned short *)malloc(size); glFormat = GL_RGB; glType = GL_UNSIGNED_SHORT_5_6_5; break;             // 16 bpp
        case UNCOMPRESSED_R8G8B8: pixels = (unsigned char *)malloc(size*3); glFormat = GL_RGB; glType = GL_UNSIGNED_BYTE; break;                   // 24 bpp
        case UNCOMPRESSED_R5G5B5A1: pixels = (unsigned short *)malloc(size); glFormat = GL_RGBA; glType = GL_UNSIGNED_SHORT_5_5_5_1; break;        // 16 bpp (1 bit alpha)
        case UNCOMPRESSED_R4G4B4A4: pixels = (unsigned short *)malloc(size); glFormat = GL_RGBA; glType = GL_UNSIGNED_SHORT_4_4_4_4; break;        // 16 bpp (4 bit alpha)
        case UNCOMPRESSED_R8G8B8A8: pixels = (unsigned char *)malloc(size*4); glFormat = GL_RGBA; glType = GL_UNSIGNED_BYTE; break;                // 32 bpp
        default: TraceLog(LOG_WARNING, "Texture data retrieval, format not suported"); break;
    }

    // NOTE: Each row written to or read from by OpenGL pixel operations like glGetTexImage are aligned to a 4 byte boundary by default, which may add some padding.
    // Use glPixelStorei to modify padding with the GL_[UN]PACK_ALIGNMENT setting.
    // GL_PACK_ALIGNMENT affects operations that read from OpenGL memory (glReadPixels, glGetTexImage, etc.)
    // GL_UNPACK_ALIGNMENT affects operations that write to OpenGL memory (glTexImage, etc.)
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glGetTexImage(GL_TEXTURE_2D, 0, glFormat, glType, pixels);

    glBindTexture(GL_TEXTURE_2D, 0);
#endif

#if defined(GRAPHICS_API_OPENGL_ES2)

    RenderTexture2D fbo = rlLoadRenderTexture(texture.width, texture.height);

    // NOTE: Two possible Options:
    // 1 - Bind texture to color fbo attachment and glReadPixels()
    // 2 - Create an fbo, activate it, render quad with texture, glReadPixels()

#define GET_TEXTURE_FBO_OPTION_1    // It works
#if defined(GET_TEXTURE_FBO_OPTION_1)
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.id);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Attach our texture to FBO -> Texture must be RGBA
    // NOTE: Previoust attached texture is automatically detached
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);

    pixels = (unsigned char *)malloc(texture.width*texture.height*4*sizeof(unsigned char));

    // NOTE: We read data as RGBA because FBO texture is configured as RGBA, despite binding a RGB texture...
    glReadPixels(0, 0, texture.width, texture.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Re-attach internal FBO color texture before deleting it
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.texture.id, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

#elif defined(GET_TEXTURE_FBO_OPTION_2)
    // Render texture to fbo
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.id);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepthf(1.0f);
    //glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    //glDisable(GL_BLEND);
    
    glViewport(0, 0, texture.width, texture.height);
    rlOrtho(0.0, texture.width, texture.height, 0.0, 0.0, 1.0);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(GetShaderDefault().id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    GenDrawQuad();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    
    pixels = (unsigned char *)malloc(texture.width*texture.height*4*sizeof(unsigned char));

    glReadPixels(0, 0, texture.width, texture.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Bind framebuffer 0, which means render to back buffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Reset viewport dimensions to default
    glViewport(0, 0, screenWidth, screenHeight);
    
#endif // GET_TEXTURE_FBO_OPTION

    // Clean up temporal fbo
    rlDeleteRenderTextures(fbo);

#endif

    return pixels;
}

/*
// TODO: Record draw calls to be processed in batch
// NOTE: Global state must be kept
void rlRecordDraw(void)
{
    // TODO: Before adding a new draw, check if anything changed from last stored draw
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    draws[drawsCounter].vaoId = currentState.vaoId;             // lines.id, trangles.id, quads.id?
    draws[drawsCounter].textureId = currentState.textureId;     // whiteTexture?
    draws[drawsCounter].shaderId = currentState.shaderId;       // defaultShader.id
    draws[drawsCounter].projection = projection;
    draws[drawsCounter].modelview = modelview;
    draws[drawsCounter].vertexCount = currentState.vertexCount;

    drawsCounter++;
#endif
}
*/

//----------------------------------------------------------------------------------
// Module Functions Definition - Shaders Functions
// NOTE: Those functions are exposed directly to the user in raylib.h
//----------------------------------------------------------------------------------

// Get default internal texture (white texture)
Texture2D GetTextureDefault(void)
{
    Texture2D texture;

    texture.id = whiteTexture;
    texture.width = 1;
    texture.height = 1;
    texture.mipmaps = 1;
    texture.format = UNCOMPRESSED_R8G8B8A8;

    return texture;
}

// Get default shader
Shader GetShaderDefault(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    return defaultShader;
#else
    Shader shader = { 0 };
    return shader;
#endif
}

// Load text data from file
// NOTE: text chars array should be freed manually
char *LoadText(const char *fileName)
{
    FILE *textFile;
    char *text = NULL;

    int count = 0;

    if (fileName != NULL)
    {
        textFile = fopen(fileName,"rt");

        if (textFile != NULL)
        {
            fseek(textFile, 0, SEEK_END);
            count = ftell(textFile);
            rewind(textFile);

            if (count > 0)
            {
                text = (char *)malloc(sizeof(char)*(count + 1));
                count = fread(text, sizeof(char), count, textFile);
                text[count] = '\0';
            }

            fclose(textFile);
        }
        else TraceLog(LOG_WARNING, "[%s] Text file could not be opened", fileName);
    }

    return text;
}

// Load shader from files and bind default locations
// NOTE: If shader string is NULL, using default vertex/fragment shaders
Shader LoadShader(char *vsFileName, char *fsFileName)
{
    Shader shader = { 0 };
    
    // NOTE: All locations must be reseted to -1 (no location)
    for (int i = 0; i < MAX_SHADER_LOCATIONS; i++) shader.locs[i] = -1;

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

    unsigned int vertexShaderId = defaultVShaderId;
    unsigned int fragmentShaderId = defaultFShaderId;

    if (vsFileName != NULL)
    {
        char *vShaderStr = LoadText(vsFileName);
        
        if (vShaderStr != NULL)
        {
            vertexShaderId = CompileShader(vShaderStr, GL_VERTEX_SHADER);
            free(vShaderStr);
        }
    }

    if (fsFileName != NULL)
    {
        char* fShaderStr = LoadText(fsFileName);
        
        if (fShaderStr != NULL)
        {
            fragmentShaderId = CompileShader(fShaderStr, GL_FRAGMENT_SHADER);
            free(fShaderStr);
        }
    }

    if ((vertexShaderId == defaultVShaderId) && (fragmentShaderId == defaultFShaderId)) shader = defaultShader;
    else 
    {
        shader.id = LoadShaderProgram(vertexShaderId, fragmentShaderId);

        if (vertexShaderId != defaultVShaderId) glDeleteShader(vertexShaderId);
        if (fragmentShaderId != defaultFShaderId) glDeleteShader(fragmentShaderId);

        if (shader.id == 0)
        {
            TraceLog(LOG_WARNING, "Custom shader could not be loaded");
            shader = defaultShader;
        }
        
        // After shader loading, we TRY to set default location names
        if (shader.id > 0) SetShaderDefaultLocations(&shader);
    }
    
    // Get available shader uniforms
    // NOTE: This information is useful for debug...
    int uniformCount = -1;
    
    glGetProgramiv(shader.id, GL_ACTIVE_UNIFORMS, &uniformCount);
    
    for(int i = 0; i < uniformCount; i++)
    {
        int namelen = -1;
        int num = -1;
        char name[256]; // Assume no variable names longer than 256
        GLenum type = GL_ZERO;

        // Get the name of the uniforms
        glGetActiveUniform(shader.id, i,sizeof(name) - 1, &namelen, &num, &type, name);
        
        name[namelen] = 0;

        // Get the location of the named uniform
        GLuint location = glGetUniformLocation(shader.id, name);
        
        TraceLog(LOG_DEBUG, "[SHDR ID %i] Active uniform [%s] set at location: %i", shader.id, name, location);
    }
#endif

    return shader;
}

// Unload shader from GPU memory (VRAM)
void UnloadShader(Shader shader)
{
    if (shader.id > 0)
    {
        rlDeleteShader(shader.id);
        TraceLog(LOG_INFO, "[SHDR ID %i] Unloaded shader program data", shader.id);
    }
}

// Begin custom shader mode
void BeginShaderMode(Shader shader)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (currentShader.id != shader.id)
    {
        rlglDraw();
        currentShader = shader;
    }
#endif
}

// End custom shader mode (returns to default shader)
void EndShaderMode(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    BeginShaderMode(defaultShader);
#endif
}

// Get shader uniform location
int GetShaderLocation(Shader shader, const char *uniformName)
{
    int location = -1;
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    location = glGetUniformLocation(shader.id, uniformName);

    if (location == -1) TraceLog(LOG_WARNING, "[SHDR ID %i] Shader uniform [%s] COULD NOT BE FOUND", shader.id, uniformName);
    else TraceLog(LOG_INFO, "[SHDR ID %i] Shader uniform [%s] set at location: %i", shader.id, uniformName, location);
#endif
    return location;
}

// Set shader uniform value (float)
void SetShaderValue(Shader shader, int uniformLoc, const float *value, int size)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glUseProgram(shader.id);

    if (size == 1) glUniform1fv(uniformLoc, 1, value);          // Shader uniform type: float
    else if (size == 2) glUniform2fv(uniformLoc, 1, value);     // Shader uniform type: vec2
    else if (size == 3) glUniform3fv(uniformLoc, 1, value);     // Shader uniform type: vec3
    else if (size == 4) glUniform4fv(uniformLoc, 1, value);     // Shader uniform type: vec4
    else TraceLog(LOG_WARNING, "Shader value float array size not supported");

    //glUseProgram(0);      // Avoid reseting current shader program, in case other uniforms are set
#endif
}

// Set shader uniform value (int)
void SetShaderValuei(Shader shader, int uniformLoc, const int *value, int size)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glUseProgram(shader.id);

    if (size == 1) glUniform1iv(uniformLoc, 1, value);          // Shader uniform type: int
    else if (size == 2) glUniform2iv(uniformLoc, 1, value);     // Shader uniform type: ivec2
    else if (size == 3) glUniform3iv(uniformLoc, 1, value);     // Shader uniform type: ivec3
    else if (size == 4) glUniform4iv(uniformLoc, 1, value);     // Shader uniform type: ivec4
    else TraceLog(LOG_WARNING, "Shader value int array size not supported");

    //glUseProgram(0);
#endif
}

// Set shader uniform value (matrix 4x4)
void SetShaderValueMatrix(Shader shader, int uniformLoc, Matrix mat)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    glUseProgram(shader.id);

    glUniformMatrix4fv(uniformLoc, 1, false, MatrixToFloat(mat));

    //glUseProgram(0);
#endif
}

// Set a custom projection matrix (replaces internal projection matrix)
void SetMatrixProjection(Matrix proj)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    projection = proj;
#endif
}

// Set a custom modelview matrix (replaces internal modelview matrix)
void SetMatrixModelview(Matrix view)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    modelview = view;
#endif
}

Matrix GetMatrixModelview() {
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    return modelview;
#endif
}

// Generate cubemap texture from HDR texture
// TODO: OpenGL ES 2.0 does not support GL_RGB16F texture format, neither GL_DEPTH_COMPONENT24
Texture2D GenTextureCubemap(Shader shader, Texture2D skyHDR, int size)
{
    Texture2D cubemap = { 0 };
#if defined(GRAPHICS_API_OPENGL_33) // || defined(GRAPHICS_API_OPENGL_ES2)  
    // NOTE: SetShaderDefaultLocations() already setups locations for projection and view Matrix in shader
    // Other locations should be setup externally in shader before calling the function
    
    // Set up depth face culling and cubemap seamless
    glDisable(GL_CULL_FACE);
#if defined(GRAPHICS_API_OPENGL_33)
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);     // Flag not supported on OpenGL ES 2.0
#endif
    

    // Setup framebuffer
    unsigned int fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    // Set up cubemap to render and attach to framebuffer
    // NOTE: faces are stored with 16 bit floating point values
    glGenTextures(1, &cubemap.id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap.id);
    for (unsigned int i = 0; i < 6; i++) 
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, size, size, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if defined(GRAPHICS_API_OPENGL_33)
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);  // Flag not supported on OpenGL ES 2.0
#endif
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Create projection (transposed) and different views for each face
    Matrix fboProjection = MatrixPerspective(90.0*DEG2RAD, 1.0, 0.01, 1000.0);
    //MatrixTranspose(&fboProjection);
    Matrix fboViews[6] = {
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ -1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, -1.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, -1.0f }, (Vector3){ 0.0f, -1.0f, 0.0f })
    };

    // Convert HDR equirectangular environment map to cubemap equivalent
    glUseProgram(shader.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, skyHDR.id);
    SetShaderValueMatrix(shader, shader.locs[LOC_MATRIX_PROJECTION], fboProjection);

    // Note: don't forget to configure the viewport to the capture dimensions
    glViewport(0, 0, size, size);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    for (unsigned int i = 0; i < 6; i++)
    {
        SetShaderValueMatrix(shader, shader.locs[LOC_MATRIX_VIEW], fboViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemap.id, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        GenDrawCube();
    }

    // Unbind framebuffer and textures
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Reset viewport dimensions to default
    glViewport(0, 0, screenWidth, screenHeight);
    //glEnable(GL_CULL_FACE);

    cubemap.width = size;
    cubemap.height = size;
#endif
    return cubemap;
}

// Generate irradiance texture using cubemap data
// TODO: OpenGL ES 2.0 does not support GL_RGB16F texture format, neither GL_DEPTH_COMPONENT24
Texture2D GenTextureIrradiance(Shader shader, Texture2D cubemap, int size)
{
    Texture2D irradiance = { 0 };
    
#if defined(GRAPHICS_API_OPENGL_33) // || defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: SetShaderDefaultLocations() already setups locations for projection and view Matrix in shader
    // Other locations should be setup externally in shader before calling the function
    
    // Setup framebuffer
    unsigned int fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
    
    // Create an irradiance cubemap, and re-scale capture FBO to irradiance scale
    glGenTextures(1, &irradiance.id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradiance.id);
    for (unsigned int i = 0; i < 6; i++) 
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, size, size, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Create projection (transposed) and different views for each face
    Matrix fboProjection = MatrixPerspective(90.0*DEG2RAD, 1.0, 0.01, 1000.0);
    //MatrixTranspose(&fboProjection);
    Matrix fboViews[6] = {
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ -1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, -1.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, -1.0f }, (Vector3){ 0.0f, -1.0f, 0.0f })
    };

    // Solve diffuse integral by convolution to create an irradiance cubemap
    glUseProgram(shader.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap.id);
    SetShaderValueMatrix(shader, shader.locs[LOC_MATRIX_PROJECTION], fboProjection);

    // Note: don't forget to configure the viewport to the capture dimensions
    glViewport(0, 0, size, size);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    for (unsigned int i = 0; i < 6; i++)
    {
        SetShaderValueMatrix(shader, shader.locs[LOC_MATRIX_VIEW], fboViews[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradiance.id, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        GenDrawCube();
    }

    // Unbind framebuffer and textures
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Reset viewport dimensions to default
    glViewport(0, 0, screenWidth, screenHeight);

    irradiance.width = size;
    irradiance.height = size;
#endif
    return irradiance;
}

// Generate prefilter texture using cubemap data
// TODO: OpenGL ES 2.0 does not support GL_RGB16F texture format, neither GL_DEPTH_COMPONENT24
Texture2D GenTexturePrefilter(Shader shader, Texture2D cubemap, int size)
{
    Texture2D prefilter = { 0 };
    
#if defined(GRAPHICS_API_OPENGL_33) // || defined(GRAPHICS_API_OPENGL_ES2)
    // NOTE: SetShaderDefaultLocations() already setups locations for projection and view Matrix in shader
    // Other locations should be setup externally in shader before calling the function
    // TODO: Locations should be taken out of this function... too shader dependant...
    int roughnessLoc = GetShaderLocation(shader, "roughness");
    
    // Setup framebuffer
    unsigned int fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
    
    // Create a prefiltered HDR environment map
    glGenTextures(1, &prefilter.id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefilter.id);
    for (unsigned int i = 0; i < 6; i++) 
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, size, size, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Generate mipmaps for the prefiltered HDR texture
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    
    // Create projection (transposed) and different views for each face
    Matrix fboProjection = MatrixPerspective(90.0*DEG2RAD, 1.0, 0.01, 1000.0);
    //MatrixTranspose(&fboProjection);
    Matrix fboViews[6] = {
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ -1.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, -1.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, 1.0f }, (Vector3){ 0.0f, -1.0f, 0.0f }),
        MatrixLookAt((Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 0.0f, -1.0f }, (Vector3){ 0.0f, -1.0f, 0.0f })
    };

    // Prefilter HDR and store data into mipmap levels
    glUseProgram(shader.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap.id);
    SetShaderValueMatrix(shader, shader.locs[LOC_MATRIX_PROJECTION], fboProjection);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    #define MAX_MIPMAP_LEVELS   5   // Max number of prefilter texture mipmaps

    for (unsigned int mip = 0; mip < MAX_MIPMAP_LEVELS; mip++)
    {
        // Resize framebuffer according to mip-level size.
        unsigned int mipWidth  = size*powf(0.5f, mip);
        unsigned int mipHeight = size*powf(0.5f, mip);
        
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        float roughness = (float)mip/(float)(MAX_MIPMAP_LEVELS - 1);
        glUniform1f(roughnessLoc, roughness);

        for (unsigned int i = 0; i < 6; ++i)
        {
            SetShaderValueMatrix(shader, shader.locs[LOC_MATRIX_VIEW], fboViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilter.id, mip);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            GenDrawCube();
        }
    }

    // Unbind framebuffer and textures
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Reset viewport dimensions to default
    glViewport(0, 0, screenWidth, screenHeight);

    prefilter.width = size;
    prefilter.height = size;
#endif
    return prefilter;
}

// Generate BRDF texture using cubemap data
// TODO: OpenGL ES 2.0 does not support GL_RGB16F texture format, neither GL_DEPTH_COMPONENT24
Texture2D GenTextureBRDF(Shader shader, Texture2D cubemap, int size)
{
    Texture2D brdf = { 0 };
#if defined(GRAPHICS_API_OPENGL_33) // || defined(GRAPHICS_API_OPENGL_ES2)
    // Generate BRDF convolution texture
    glGenTextures(1, &brdf.id);
    glBindTexture(GL_TEXTURE_2D, brdf.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, size, size, 0, GL_RG, GL_FLOAT, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Render BRDF LUT into a quad using FBO
    unsigned int fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdf.id, 0);
    
    glViewport(0, 0, size, size);
    glUseProgram(shader.id);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GenDrawQuad();

    // Unbind framebuffer and textures
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Reset viewport dimensions to default
    glViewport(0, 0, screenWidth, screenHeight);
   
    brdf.width = size;
    brdf.height = size;
#endif
    return brdf;
}

// Begin blending mode (alpha, additive, multiplied)
// NOTE: Only 3 blending modes supported, default blend mode is alpha
void BeginBlendMode(int mode)
{
    if ((blendMode != mode) && (mode < 3))
    {
        rlglDraw();

        switch (mode)
        {
            case BLEND_ALPHA: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case BLEND_ADDITIVE: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break; // Alternative: glBlendFunc(GL_ONE, GL_ONE);
            case BLEND_MULTIPLIED: glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
            default: break;
        }

        blendMode = mode;
    }
}

// End blending mode (reset to default: alpha blending)
void EndBlendMode(void)
{
    BeginBlendMode(BLEND_ALPHA);
}

#if defined(SUPPORT_VR_SIMULATOR)
// Get VR device information for some standard devices
VrDeviceInfo GetVrDeviceInfo(int vrDeviceType)
{
    VrDeviceInfo hmd = { 0 };                // Current VR device info
    
    switch (vrDeviceType)
    {
        case HMD_DEFAULT_DEVICE:
        case HMD_OCULUS_RIFT_CV1:
        {
            // Oculus Rift CV1 parameters
            // NOTE: CV1 represents a complete HMD redesign compared to previous versions,
            // new Fresnel-hybrid-asymmetric lenses have been added and, consequently,
            // previous parameters (DK2) and distortion shader (DK2) doesn't work any more.
            // I just defined a set of parameters for simulator that approximate to CV1 stereo rendering
            // but result is not the same obtained with Oculus PC SDK.
            hmd.hResolution = 2160;                 // HMD horizontal resolution in pixels
            hmd.vResolution = 1200;                 // HMD vertical resolution in pixels
            hmd.hScreenSize = 0.133793f;            // HMD horizontal size in meters
            hmd.vScreenSize = 0.0669f;              // HMD vertical size in meters
            hmd.vScreenCenter = 0.04678f;           // HMD screen center in meters
            hmd.eyeToScreenDistance = 0.041f;       // HMD distance between eye and display in meters
            hmd.lensSeparationDistance = 0.07f;     // HMD lens separation distance in meters
            hmd.interpupillaryDistance = 0.07f;     // HMD IPD (distance between pupils) in meters
            hmd.lensDistortionValues[0] = 1.0f;     // HMD lens distortion constant parameter 0
            hmd.lensDistortionValues[1] = 0.22f;    // HMD lens distortion constant parameter 1
            hmd.lensDistortionValues[2] = 0.24f;    // HMD lens distortion constant parameter 2
            hmd.lensDistortionValues[3] = 0.0f;     // HMD lens distortion constant parameter 3
            hmd.chromaAbCorrection[0] = 0.996f;     // HMD chromatic aberration correction parameter 0
            hmd.chromaAbCorrection[1] = -0.004f;    // HMD chromatic aberration correction parameter 1
            hmd.chromaAbCorrection[2] = 1.014f;     // HMD chromatic aberration correction parameter 2
            hmd.chromaAbCorrection[3] = 0.0f;       // HMD chromatic aberration correction parameter 3
            
            TraceLog(LOG_INFO, "Initializing VR Simulator (Oculus Rift CV1)");
        } break;
        case HMD_OCULUS_RIFT_DK2:
        {
            // Oculus Rift DK2 parameters
            hmd.hResolution = 1280;                 // HMD horizontal resolution in pixels
            hmd.vResolution = 800;                  // HMD vertical resolution in pixels
            hmd.hScreenSize = 0.14976f;             // HMD horizontal size in meters
            hmd.vScreenSize = 0.09356f;             // HMD vertical size in meters
            hmd.vScreenCenter = 0.04678f;           // HMD screen center in meters
            hmd.eyeToScreenDistance = 0.041f;       // HMD distance between eye and display in meters
            hmd.lensSeparationDistance = 0.0635f;   // HMD lens separation distance in meters
            hmd.interpupillaryDistance = 0.064f;    // HMD IPD (distance between pupils) in meters
            hmd.lensDistortionValues[0] = 1.0f;     // HMD lens distortion constant parameter 0
            hmd.lensDistortionValues[1] = 0.22f;    // HMD lens distortion constant parameter 1
            hmd.lensDistortionValues[2] = 0.24f;    // HMD lens distortion constant parameter 2
            hmd.lensDistortionValues[3] = 0.0f;     // HMD lens distortion constant parameter 3
            hmd.chromaAbCorrection[0] = 0.996f;     // HMD chromatic aberration correction parameter 0
            hmd.chromaAbCorrection[1] = -0.004f;    // HMD chromatic aberration correction parameter 1
            hmd.chromaAbCorrection[2] = 1.014f;     // HMD chromatic aberration correction parameter 2
            hmd.chromaAbCorrection[3] = 0.0f;       // HMD chromatic aberration correction parameter 3
            
            TraceLog(LOG_INFO, "Initializing VR Simulator (Oculus Rift DK2)");
        } break; 
        case HMD_OCULUS_GO:
        {
            // TODO: Provide device display and lens parameters
        }
        case HMD_VALVE_HTC_VIVE:
        {
            // TODO: Provide device display and lens parameters
        }
        case HMD_SONY_PSVR:
        {
            // TODO: Provide device display and lens parameters
        }
        default: break;
    }
    
    return hmd;
}

// Init VR simulator for selected device parameters
// NOTE: It modifies the global variable: VrStereoConfig vrConfig
void InitVrSimulator(VrDeviceInfo info)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    // Initialize framebuffer and textures for stereo rendering
    // NOTE: Screen size should match HMD aspect ratio
    vrConfig.stereoFbo = rlLoadRenderTexture(screenWidth, screenHeight);
    
#if defined(SUPPORT_DISTORTION_SHADER)
    // Load distortion shader
    unsigned int vertexShaderId = CompileShader(distortionVShaderStr, GL_VERTEX_SHADER);
    unsigned int fragmentShaderId = CompileShader(distortionFShaderStr, GL_FRAGMENT_SHADER);
    
    vrConfig.distortionShader.id = LoadShaderProgram(vertexShaderId, fragmentShaderId);
    if (vrConfig.distortionShader.id > 0) SetShaderDefaultLocations(&vrConfig.distortionShader);
#endif

    // Set VR configutarion parameters, including distortion shader
    SetStereoConfig(info);

    vrSimulatorReady = true;
#endif

#if defined(GRAPHICS_API_OPENGL_11)
    TraceLog(LOG_WARNING, "VR Simulator not supported on OpenGL 1.1");
#endif
}

// Close VR simulator for current device
void CloseVrSimulator(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (vrSimulatorReady)
    {
        rlDeleteRenderTextures(vrConfig.stereoFbo); // Unload stereo framebuffer and texture
        #if defined(SUPPORT_DISTORTION_SHADER)
        UnloadShader(vrConfig.distortionShader);    // Unload distortion shader
        #endif
    }
#endif
}

// Detect if VR simulator is running
bool IsVrSimulatorReady(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    return vrSimulatorReady;
#else
    return false;
#endif
}

// Set VR distortion shader for stereoscopic rendering
// TODO: Review VR system to be more flexible, move distortion shader to user side
void SetVrDistortionShader(Shader shader)
{
    vrConfig.distortionShader = shader;

    //SetStereoConfig(info);  // TODO: Must be reviewed to set new distortion shader uniform values...
}

// Enable/Disable VR experience (device or simulator)
void ToggleVrMode(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    vrSimulatorReady = !vrSimulatorReady;

    if (!vrSimulatorReady)
    {
        vrStereoRender = false;
        
        // Reset viewport and default projection-modelview matrices
        rlViewport(0, 0, screenWidth, screenHeight);
        projection = MatrixOrtho(0.0, screenWidth, screenHeight, 0.0, 0.0, 1.0);
        modelview = MatrixIdentity();
    }
    else vrStereoRender = true;
#endif
}

// Update VR tracking (position and orientation) and camera
// NOTE: Camera (position, target, up) gets update with head tracking information
void UpdateVrTracking(Camera *camera)
{
    // TODO: Simulate 1st person camera system
}

// Begin Oculus drawing configuration
void BeginVrDrawing(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (vrSimulatorReady)
    {
        // Setup framebuffer for stereo rendering
        rlEnableRenderTexture(vrConfig.stereoFbo.id);

        // NOTE: If your application is configured to treat the texture as a linear format (e.g. GL_RGBA)
        // and performs linear-to-gamma conversion in GLSL or does not care about gamma-correction, then:
        //     - Require OculusBuffer format to be OVR_FORMAT_R8G8B8A8_UNORM_SRGB
        //     - Do NOT enable GL_FRAMEBUFFER_SRGB
        //glEnable(GL_FRAMEBUFFER_SRGB);

        //glViewport(0, 0, buffer.width, buffer.height);        // Useful if rendering to separate framebuffers (every eye)
        rlClearScreenBuffers();             // Clear current framebuffer(s)
        
        vrStereoRender = true;
    }
#endif
}

// End Oculus drawing process (and desktop mirror)
void EndVrDrawing(void)
{
#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
    if (vrSimulatorReady)
    {
        vrStereoRender = false;         // Disable stereo render
        
        rlDisableRenderTexture();       // Unbind current framebuffer

        rlClearScreenBuffers();         // Clear current framebuffer

        // Set viewport to default framebuffer size (screen size)
        rlViewport(0, 0, screenWidth, screenHeight);

        // Let rlgl reconfigure internal matrices
        rlMatrixMode(RL_PROJECTION);                            // Enable internal projection matrix
        rlLoadIdentity();                                       // Reset internal projection matrix
        rlOrtho(0.0, screenWidth, screenHeight, 0.0, 0.0, 1.0); // Recalculate internal projection matrix
        rlMatrixMode(RL_MODELVIEW);                             // Enable internal modelview matrix
        rlLoadIdentity();                                       // Reset internal modelview matrix

#if defined(SUPPORT_DISTORTION_SHADER)
        // Draw RenderTexture (stereoFbo) using distortion shader
        currentShader = vrConfig.distortionShader;
#else
        currentShader = GetShaderDefault();
#endif

        rlEnableTexture(vrConfig.stereoFbo.texture.id);

        rlPushMatrix();
            rlBegin(RL_QUADS);
                rlColor4ub(255, 255, 255, 255);
                rlNormal3f(0.0f, 0.0f, 1.0f);

                // Bottom-left corner for texture and quad
                rlTexCoord2f(0.0f, 1.0f);
                rlVertex2f(0.0f, 0.0f);

                // Bottom-right corner for texture and quad
                rlTexCoord2f(0.0f, 0.0f);
                rlVertex2f(0.0f, vrConfig.stereoFbo.texture.height);

                // Top-right corner for texture and quad
                rlTexCoord2f(1.0f, 0.0f);
                rlVertex2f(vrConfig.stereoFbo.texture.width, vrConfig.stereoFbo.texture.height);

                // Top-left corner for texture and quad
                rlTexCoord2f(1.0f, 1.0f);
                rlVertex2f(vrConfig.stereoFbo.texture.width, 0.0f);
            rlEnd();
        rlPopMatrix();

        rlDisableTexture();

        // Update and draw render texture fbo with distortion to backbuffer
        UpdateBuffersDefault();
        DrawBuffersDefault();
        
        // Restore defaultShader
        currentShader = defaultShader;

        // Reset viewport and default projection-modelview matrices
        rlViewport(0, 0, screenWidth, screenHeight);
        projection = MatrixOrtho(0.0, screenWidth, screenHeight, 0.0, 0.0, 1.0);
        modelview = MatrixIdentity();
        
        rlDisableDepthTest();
    }
#endif
}
#endif          // SUPPORT_VR_SIMULATOR

//----------------------------------------------------------------------------------
// Module specific Functions Definition
//----------------------------------------------------------------------------------

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)
// Convert image data to OpenGL texture (returns OpenGL valid Id)
// NOTE: Expected compressed image data and POT image
static void LoadTextureCompressed(unsigned char *data, int width, int height, int compressedFormat, int mipmapCount)
{
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int blockSize = 0;      // Bytes every block
    int offset = 0;

    if ((compressedFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT) ||
        (compressedFormat == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) ||
#if defined(GRAPHICS_API_OPENGL_ES2)
        (compressedFormat == GL_ETC1_RGB8_OES) ||
#endif
        (compressedFormat == GL_COMPRESSED_RGB8_ETC2)) blockSize = 8;
    else blockSize = 16;

    // Load the mipmap levels
    for (int level = 0; level < mipmapCount && (width || height); level++)
    {
        unsigned int size = 0;

        size = ((width + 3)/4)*((height + 3)/4)*blockSize;

        glCompressedTexImage2D(GL_TEXTURE_2D, level, compressedFormat, width, height, 0, size, data + offset);

        offset += size;
        width  /= 2;
        height /= 2;

        // Security check for NPOT textures
        if (width < 1) width = 1;
        if (height < 1) height = 1;
    }
}

// Compile custom shader and return shader id
static unsigned int CompileShader(const char *shaderStr, int type)
{
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &shaderStr, NULL);

    GLint success = 0;
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (success != GL_TRUE)
    {
        TraceLog(LOG_WARNING, "[SHDR ID %i] Failed to compile shader...", shader);
        int maxLength = 0;
        int length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

#ifdef _MSC_VER
        char *log = malloc(maxLength);
#else
        char log[maxLength];
#endif
        glGetShaderInfoLog(shader, maxLength, &length, log);

        TraceLog(LOG_INFO, "%s", log);

#ifdef _MSC_VER
        free(log);
#endif
    }
    else TraceLog(LOG_INFO, "[SHDR ID %i] Shader compiled successfully", shader);

    return shader;
}

// Load custom shader strings and return program id
static unsigned int LoadShaderProgram(unsigned int vShaderId, unsigned int fShaderId)
{
    unsigned int program = 0;

#if defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

    GLint success = 0;
    program = glCreateProgram();

    glAttachShader(program, vShaderId);
    glAttachShader(program, fShaderId);

    // NOTE: Default attribute shader locations must be binded before linking
    glBindAttribLocation(program, 0, DEFAULT_ATTRIB_POSITION_NAME);
    glBindAttribLocation(program, 1, DEFAULT_ATTRIB_TEXCOORD_NAME);
    glBindAttribLocation(program, 2, DEFAULT_ATTRIB_NORMAL_NAME);
    glBindAttribLocation(program, 3, DEFAULT_ATTRIB_COLOR_NAME);
    glBindAttribLocation(program, 4, DEFAULT_ATTRIB_TANGENT_NAME);
    glBindAttribLocation(program, 5, DEFAULT_ATTRIB_TEXCOORD2_NAME);

    // NOTE: If some attrib name is no found on the shader, it locations becomes -1

    glLinkProgram(program);

    // NOTE: All uniform variables are intitialised to 0 when a program links

    glGetProgramiv(program, GL_LINK_STATUS, &success);

    if (success == GL_FALSE)
    {
        TraceLog(LOG_WARNING, "[SHDR ID %i] Failed to link shader program...", program);

        int maxLength = 0;
        int length;

        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

#ifdef _MSC_VER
        char *log = malloc(maxLength);
#else
        char log[maxLength];
#endif
        glGetProgramInfoLog(program, maxLength, &length, log);

        TraceLog(LOG_INFO, "%s", log);

#ifdef _MSC_VER
        free(log);
#endif
        glDeleteProgram(program);

        program = 0;
    }
    else TraceLog(LOG_INFO, "[SHDR ID %i] Shader program loaded successfully", program);
#endif
    return program;
}


// Load default shader (just vertex positioning and texture coloring)
// NOTE: This shader program is used for batch buffers (lines, triangles, quads)
static Shader LoadShaderDefault(void)
{
    Shader shader = { 0 };
    
    // NOTE: All locations must be reseted to -1 (no location)
    for (int i = 0; i < MAX_SHADER_LOCATIONS; i++) shader.locs[i] = -1;

    // Vertex shader directly defined, no external file required
    char defaultVShaderStr[] =
#if defined(GRAPHICS_API_OPENGL_21)
    "#version 120                       \n"
#elif defined(GRAPHICS_API_OPENGL_ES2)
    "#version 100                       \n"
#endif
#if defined(GRAPHICS_API_OPENGL_ES2) || defined(GRAPHICS_API_OPENGL_21)
    "attribute vec3 vertexPosition;     \n"
    "attribute vec2 vertexTexCoord;     \n"
    "attribute vec4 vertexColor;        \n"
    "varying vec2 fragTexCoord;         \n"
    "varying vec4 fragColor;            \n"
#elif defined(GRAPHICS_API_OPENGL_33)
    "#version 330                       \n"
    "in vec3 vertexPosition;            \n"
    "in vec2 vertexTexCoord;            \n"
    "in vec4 vertexColor;               \n"
    "out vec2 fragTexCoord;             \n"
    "out vec4 fragColor;                \n"
#endif
    "uniform mat4 mvp;                  \n"
    "void main()                        \n"
    "{                                  \n"
    "    fragTexCoord = vertexTexCoord; \n"
    "    fragColor = vertexColor;       \n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0); \n"
    "}                                  \n";

    // Fragment shader directly defined, no external file required
    char defaultFShaderStr[] =
#if defined(GRAPHICS_API_OPENGL_21)
    "#version 120                       \n"
#elif defined(GRAPHICS_API_OPENGL_ES2)
    "#version 100                       \n"
    "precision mediump float;           \n"     // precision required for OpenGL ES2 (WebGL)
#endif
#if defined(GRAPHICS_API_OPENGL_ES2) || defined(GRAPHICS_API_OPENGL_21)
    "varying vec2 fragTexCoord;         \n"
    "varying vec4 fragColor;            \n"
#elif defined(GRAPHICS_API_OPENGL_33)
    "#version 330       \n"
    "in vec2 fragTexCoord;              \n"
    "in vec4 fragColor;                 \n"
    "out vec4 finalColor;               \n"
#endif
    "uniform sampler2D texture0;        \n"
    "uniform vec4 colDiffuse;           \n"
    "void main()                        \n"
    "{                                  \n"
#if defined(GRAPHICS_API_OPENGL_ES2) || defined(GRAPHICS_API_OPENGL_21)
    "    vec4 texelColor = texture2D(texture0, fragTexCoord); \n" // NOTE: texture2D() is deprecated on OpenGL 3.3 and ES 3.0
    "    gl_FragColor = texelColor*colDiffuse*fragColor;      \n"
#elif defined(GRAPHICS_API_OPENGL_33)
    "    vec4 texelColor = texture(texture0, fragTexCoord);   \n"
    "    finalColor = texelColor*colDiffuse*fragColor;        \n"
#endif
    "}                                  \n";

    // NOTE: Compiled vertex/fragment shaders are kept for re-use
    defaultVShaderId = CompileShader(defaultVShaderStr, GL_VERTEX_SHADER);     // Compile default vertex shader
    defaultFShaderId = CompileShader(defaultFShaderStr, GL_FRAGMENT_SHADER);   // Compile default fragment shader
    
    shader.id = LoadShaderProgram(defaultVShaderId, defaultFShaderId);

    if (shader.id > 0) 
    {
        TraceLog(LOG_INFO, "[SHDR ID %i] Default shader loaded successfully", shader.id);

        // Set default shader locations: attributes locations
        shader.locs[LOC_VERTEX_POSITION] = glGetAttribLocation(shader.id, "vertexPosition");
        shader.locs[LOC_VERTEX_TEXCOORD01] = glGetAttribLocation(shader.id, "vertexTexCoord");
        shader.locs[LOC_VERTEX_COLOR] = glGetAttribLocation(shader.id, "vertexColor");

        // Set default shader locations: uniform locations
        shader.locs[LOC_MATRIX_MVP]  = glGetUniformLocation(shader.id, "mvp");
        shader.locs[LOC_COLOR_DIFFUSE] = glGetUniformLocation(shader.id, "colDiffuse");
        shader.locs[LOC_MAP_DIFFUSE] = glGetUniformLocation(shader.id, "texture0");
        
        // NOTE: We could also use below function but in case DEFAULT_ATTRIB_* points are
        // changed for external custom shaders, we just use direct bindings above
        //SetShaderDefaultLocations(&shader);
    }
    else TraceLog(LOG_WARNING, "[SHDR ID %i] Default shader could not be loaded", shader.id);

    return shader;
}

// Get location handlers to for shader attributes and uniforms
// NOTE: If any location is not found, loc point becomes -1
static void SetShaderDefaultLocations(Shader *shader)
{
    // NOTE: Default shader attrib locations have been fixed before linking:
    //          vertex position location    = 0
    //          vertex texcoord location    = 1
    //          vertex normal location      = 2
    //          vertex color location       = 3
    //          vertex tangent location     = 4
    //          vertex texcoord2 location   = 5

    // Get handles to GLSL input attibute locations
    shader->locs[LOC_VERTEX_POSITION] = glGetAttribLocation(shader->id, DEFAULT_ATTRIB_POSITION_NAME);
    shader->locs[LOC_VERTEX_TEXCOORD01] = glGetAttribLocation(shader->id, DEFAULT_ATTRIB_TEXCOORD_NAME);
    shader->locs[LOC_VERTEX_TEXCOORD02] = glGetAttribLocation(shader->id, DEFAULT_ATTRIB_TEXCOORD2_NAME);
    shader->locs[LOC_VERTEX_NORMAL] = glGetAttribLocation(shader->id, DEFAULT_ATTRIB_NORMAL_NAME);
    shader->locs[LOC_VERTEX_TANGENT] = glGetAttribLocation(shader->id, DEFAULT_ATTRIB_TANGENT_NAME);
    shader->locs[LOC_VERTEX_COLOR] = glGetAttribLocation(shader->id, DEFAULT_ATTRIB_COLOR_NAME);

    // Get handles to GLSL uniform locations (vertex shader)
    shader->locs[LOC_MATRIX_MVP]  = glGetUniformLocation(shader->id, "mvp");
    shader->locs[LOC_MATRIX_PROJECTION]  = glGetUniformLocation(shader->id, "projection");
    shader->locs[LOC_MATRIX_VIEW]  = glGetUniformLocation(shader->id, "view");

    // Get handles to GLSL uniform locations (fragment shader)
    shader->locs[LOC_COLOR_DIFFUSE] = glGetUniformLocation(shader->id, "colDiffuse");
    shader->locs[LOC_MAP_DIFFUSE] = glGetUniformLocation(shader->id, "texture0");
    shader->locs[LOC_MAP_SPECULAR] = glGetUniformLocation(shader->id, "texture1");
    shader->locs[LOC_MAP_NORMAL] = glGetUniformLocation(shader->id, "texture2");
}

// Unload default shader
static void UnloadShaderDefault(void)
{
    glUseProgram(0);

    glDetachShader(defaultShader.id, defaultVShaderId);
    glDetachShader(defaultShader.id, defaultFShaderId);
    glDeleteShader(defaultVShaderId);
    glDeleteShader(defaultFShaderId);
    glDeleteProgram(defaultShader.id);
}

// Load default internal buffers (lines, triangles, quads)
static void LoadBuffersDefault(void)
{
    // [CPU] Allocate and initialize float array buffers to store vertex data (lines, triangles, quads)
    //--------------------------------------------------------------------------------------------

    // Lines - Initialize arrays (vertex position and color data)
    lines.vertices = (float *)malloc(sizeof(float)*3*2*MAX_LINES_BATCH);        // 3 float by vertex, 2 vertex by line
    lines.colors = (unsigned char *)malloc(sizeof(unsigned char)*4*2*MAX_LINES_BATCH);  // 4 float by color, 2 colors by line
    lines.texcoords = NULL;
    lines.indices = NULL;

    for (int i = 0; i < (3*2*MAX_LINES_BATCH); i++) lines.vertices[i] = 0.0f;
    for (int i = 0; i < (4*2*MAX_LINES_BATCH); i++) lines.colors[i] = 0;

    lines.vCounter = 0;
    lines.cCounter = 0;
    lines.tcCounter = 0;

    // Triangles - Initialize arrays (vertex position and color data)
    triangles.vertices = (float *)malloc(sizeof(float)*3*3*MAX_TRIANGLES_BATCH);        // 3 float by vertex, 3 vertex by triangle
    triangles.colors = (unsigned char *)malloc(sizeof(unsigned char)*4*3*MAX_TRIANGLES_BATCH);  // 4 float by color, 3 colors by triangle
    triangles.texcoords = NULL;
    triangles.indices = NULL;

    for (int i = 0; i < (3*3*MAX_TRIANGLES_BATCH); i++) triangles.vertices[i] = 0.0f;
    for (int i = 0; i < (4*3*MAX_TRIANGLES_BATCH); i++) triangles.colors[i] = 0;

    triangles.vCounter = 0;
    triangles.cCounter = 0;
    triangles.tcCounter = 0;

    // Quads - Initialize arrays (vertex position, texcoord, color data and indexes)
    quads.vertices = (float *)malloc(sizeof(float)*3*4*MAX_QUADS_BATCH);        // 3 float by vertex, 4 vertex by quad
    quads.texcoords = (float *)malloc(sizeof(float)*2*4*MAX_QUADS_BATCH);       // 2 float by texcoord, 4 texcoord by quad
    quads.colors = (unsigned char *)malloc(sizeof(unsigned char)*4*4*MAX_QUADS_BATCH);  // 4 float by color, 4 colors by quad
#if defined(GRAPHICS_API_OPENGL_33)
    quads.indices = (unsigned int *)malloc(sizeof(int)*6*MAX_QUADS_BATCH);      // 6 int by quad (indices)
#elif defined(GRAPHICS_API_OPENGL_ES2)
    quads.indices = (unsigned short *)malloc(sizeof(short)*6*MAX_QUADS_BATCH);  // 6 int by quad (indices)
#endif

    for (int i = 0; i < (3*4*MAX_QUADS_BATCH); i++) quads.vertices[i] = 0.0f;
    for (int i = 0; i < (2*4*MAX_QUADS_BATCH); i++) quads.texcoords[i] = 0.0f;
    for (int i = 0; i < (4*4*MAX_QUADS_BATCH); i++) quads.colors[i] = 0;

    int k = 0;

    // Indices can be initialized right now
    for (int i = 0; i < (6*MAX_QUADS_BATCH); i+=6)
    {
        quads.indices[i] = 4*k;
        quads.indices[i+1] = 4*k+1;
        quads.indices[i+2] = 4*k+2;
        quads.indices[i+3] = 4*k;
        quads.indices[i+4] = 4*k+2;
        quads.indices[i+5] = 4*k+3;

        k++;
    }

    quads.vCounter = 0;
    quads.tcCounter = 0;
    quads.cCounter = 0;

    TraceLog(LOG_INFO, "[CPU] Default buffers initialized successfully (lines, triangles, quads)");
    //--------------------------------------------------------------------------------------------

    // [GPU] Upload vertex data and initialize VAOs/VBOs (lines, triangles, quads)
    // NOTE: Default buffers are linked to use currentShader (defaultShader)
    //--------------------------------------------------------------------------------------------

    // Upload and link lines vertex buffers
    if (vaoSupported)
    {
        // Initialize Lines VAO
        glGenVertexArrays(1, &lines.vaoId);
        glBindVertexArray(lines.vaoId);
    }

    // Lines - Vertex buffers binding and attributes enable
    // Vertex position buffer (shader-location = 0)
    glGenBuffers(2, &lines.vboId[0]);
    glBindBuffer(GL_ARRAY_BUFFER, lines.vboId[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*2*MAX_LINES_BATCH, lines.vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_POSITION]);
    glVertexAttribPointer(currentShader.locs[LOC_VERTEX_POSITION], 3, GL_FLOAT, 0, 0, 0);

    // Vertex color buffer (shader-location = 3)
    glGenBuffers(2, &lines.vboId[1]);
    glBindBuffer(GL_ARRAY_BUFFER, lines.vboId[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unsigned char)*4*2*MAX_LINES_BATCH, lines.colors, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_COLOR]);
    glVertexAttribPointer(currentShader.locs[LOC_VERTEX_COLOR], 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);

    if (vaoSupported) TraceLog(LOG_INFO, "[VAO ID %i] Default buffers VAO initialized successfully (lines)", lines.vaoId);
    else TraceLog(LOG_INFO, "[VBO ID %i][VBO ID %i] Default buffers VBOs initialized successfully (lines)", lines.vboId[0], lines.vboId[1]);

    // Upload and link triangles vertex buffers
    if (vaoSupported)
    {
        // Initialize Triangles VAO
        glGenVertexArrays(1, &triangles.vaoId);
        glBindVertexArray(triangles.vaoId);
    }

    // Triangles - Vertex buffers binding and attributes enable
    // Vertex position buffer (shader-location = 0)
    glGenBuffers(1, &triangles.vboId[0]);
    glBindBuffer(GL_ARRAY_BUFFER, triangles.vboId[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*3*MAX_TRIANGLES_BATCH, triangles.vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_POSITION]);
    glVertexAttribPointer(currentShader.locs[LOC_VERTEX_POSITION], 3, GL_FLOAT, 0, 0, 0);

    // Vertex color buffer (shader-location = 3)
    glGenBuffers(1, &triangles.vboId[1]);
    glBindBuffer(GL_ARRAY_BUFFER, triangles.vboId[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unsigned char)*4*3*MAX_TRIANGLES_BATCH, triangles.colors, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_COLOR]);
    glVertexAttribPointer(currentShader.locs[LOC_VERTEX_COLOR], 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);

    if (vaoSupported) TraceLog(LOG_INFO, "[VAO ID %i] Default buffers VAO initialized successfully (triangles)", triangles.vaoId);
    else TraceLog(LOG_INFO, "[VBO ID %i][VBO ID %i] Default buffers VBOs initialized successfully (triangles)", triangles.vboId[0], triangles.vboId[1]);

    // Upload and link quads vertex buffers
    if (vaoSupported)
    {
        // Initialize Quads VAO
        glGenVertexArrays(1, &quads.vaoId);
        glBindVertexArray(quads.vaoId);
    }

    // Quads - Vertex buffers binding and attributes enable
    // Vertex position buffer (shader-location = 0)
    glGenBuffers(1, &quads.vboId[0]);
    glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*4*MAX_QUADS_BATCH, quads.vertices, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_POSITION]);
    glVertexAttribPointer(currentShader.locs[LOC_VERTEX_POSITION], 3, GL_FLOAT, 0, 0, 0);

    // Vertex texcoord buffer (shader-location = 1)
    glGenBuffers(1, &quads.vboId[1]);
    glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*4*MAX_QUADS_BATCH, quads.texcoords, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_TEXCOORD01]);
    glVertexAttribPointer(currentShader.locs[LOC_VERTEX_TEXCOORD01], 2, GL_FLOAT, 0, 0, 0);

    // Vertex color buffer (shader-location = 3)
    glGenBuffers(1, &quads.vboId[2]);
    glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unsigned char)*4*4*MAX_QUADS_BATCH, quads.colors, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_COLOR]);
    glVertexAttribPointer(currentShader.locs[LOC_VERTEX_COLOR], 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);

    // Fill index buffer
    glGenBuffers(1, &quads.vboId[3]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quads.vboId[3]);
#if defined(GRAPHICS_API_OPENGL_33)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int)*6*MAX_QUADS_BATCH, quads.indices, GL_STATIC_DRAW);
#elif defined(GRAPHICS_API_OPENGL_ES2)
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(short)*6*MAX_QUADS_BATCH, quads.indices, GL_STATIC_DRAW);
#endif

    if (vaoSupported) TraceLog(LOG_INFO, "[VAO ID %i] Default buffers VAO initialized successfully (quads)", quads.vaoId);
    else TraceLog(LOG_INFO, "[VBO ID %i][VBO ID %i][VBO ID %i][VBO ID %i] Default buffers VBOs initialized successfully (quads)", quads.vboId[0], quads.vboId[1], quads.vboId[2], quads.vboId[3]);

    // Unbind the current VAO
    if (vaoSupported) glBindVertexArray(0);
    //--------------------------------------------------------------------------------------------
}

// Update default internal buffers (VAOs/VBOs) with vertex array data
// NOTE: If there is not vertex data, buffers doesn't need to be updated (vertexCount > 0)
// TODO: If no data changed on the CPU arrays --> No need to re-update GPU arrays (change flag required)
static void UpdateBuffersDefault(void)
{
    // Update lines vertex buffers
    if (lines.vCounter > 0)
    {
        // Activate Lines VAO
        if (vaoSupported) glBindVertexArray(lines.vaoId);

        // Lines - vertex positions buffer
        glBindBuffer(GL_ARRAY_BUFFER, lines.vboId[0]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*2*MAX_LINES_BATCH, lines.vertices, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*lines.vCounter, lines.vertices);    // target - offset (in bytes) - size (in bytes) - data pointer

        // Lines - colors buffer
        glBindBuffer(GL_ARRAY_BUFFER, lines.vboId[1]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*4*2*MAX_LINES_BATCH, lines.colors, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(unsigned char)*4*lines.cCounter, lines.colors);
    }

    // Update triangles vertex buffers
    if (triangles.vCounter > 0)
    {
        // Activate Triangles VAO
        if (vaoSupported) glBindVertexArray(triangles.vaoId);

        // Triangles - vertex positions buffer
        glBindBuffer(GL_ARRAY_BUFFER, triangles.vboId[0]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*3*MAX_TRIANGLES_BATCH, triangles.vertices, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*triangles.vCounter, triangles.vertices);

        // Triangles - colors buffer
        glBindBuffer(GL_ARRAY_BUFFER, triangles.vboId[1]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*4*3*MAX_TRIANGLES_BATCH, triangles.colors, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(unsigned char)*4*triangles.cCounter, triangles.colors);
    }

    // Update quads vertex buffers
    if (quads.vCounter > 0)
    {
        // Activate Quads VAO
        if (vaoSupported) glBindVertexArray(quads.vaoId);

        // Quads - vertex positions buffer
        glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[0]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3*4*MAX_QUADS_BATCH, quads.vertices, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*3*quads.vCounter, quads.vertices);

        // Quads - texture coordinates buffer
        glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[1]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*2*4*MAX_QUADS_BATCH, quads.texcoords, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float)*2*quads.vCounter, quads.texcoords);

        // Quads - colors buffer
        glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[2]);
        //glBufferData(GL_ARRAY_BUFFER, sizeof(float)*4*4*MAX_QUADS_BATCH, quads.colors, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(unsigned char)*4*quads.vCounter, quads.colors);

        // Another option would be using buffer mapping...
        //quads.vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);
        // Now we can modify vertices
        //glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    //--------------------------------------------------------------

    // Unbind the current VAO
    if (vaoSupported) glBindVertexArray(0);
}

// Draw default internal buffers vertex data
// NOTE: We draw in this order: lines, triangles, quads
static void DrawBuffersDefault(void)
{
    Matrix matProjection = projection;
    Matrix matModelView = modelview;
    
    int eyesCount = 1;
#if defined(SUPPORT_VR_SIMULATOR)
    if (vrStereoRender) eyesCount = 2;
#endif

    for (int eye = 0; eye < eyesCount; eye++)
    {
        #if defined(SUPPORT_VR_SIMULATOR)
        if (eyesCount == 2) SetStereoView(eye, matProjection, matModelView);
        #endif

        // Set current shader and upload current MVP matrix
        if ((lines.vCounter > 0) || (triangles.vCounter > 0) || (quads.vCounter > 0))
        {
            glUseProgram(currentShader.id);

            // Create modelview-projection matrix
            Matrix matMVP = MatrixMultiply(modelview, projection);

            glUniformMatrix4fv(currentShader.locs[LOC_MATRIX_MVP], 1, false, MatrixToFloat(matMVP));
            glUniform4f(currentShader.locs[LOC_COLOR_DIFFUSE], 1.0f, 1.0f, 1.0f, 1.0f);
            glUniform1i(currentShader.locs[LOC_MAP_DIFFUSE], 0);

            // NOTE: Additional map textures not considered for default buffers drawing
        }

        // Draw lines buffers
        if (lines.vCounter > 0)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, whiteTexture);

            if (vaoSupported)
            {
                glBindVertexArray(lines.vaoId);
            }
            else
            {
                // Bind vertex attrib: position (shader-location = 0)
                glBindBuffer(GL_ARRAY_BUFFER, lines.vboId[0]);
                glVertexAttribPointer(currentShader.locs[LOC_VERTEX_POSITION], 3, GL_FLOAT, 0, 0, 0);
                glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_POSITION]);

                // Bind vertex attrib: color (shader-location = 3)
                glBindBuffer(GL_ARRAY_BUFFER, lines.vboId[1]);
                glVertexAttribPointer(currentShader.locs[LOC_VERTEX_COLOR], 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
                glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_COLOR]);
            }

            glDrawArrays(GL_LINES, 0, lines.vCounter);

            if (!vaoSupported) glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Draw triangles buffers
        if (triangles.vCounter > 0)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, whiteTexture);

            if (vaoSupported)
            {
                glBindVertexArray(triangles.vaoId);
            }
            else
            {
                // Bind vertex attrib: position (shader-location = 0)
                glBindBuffer(GL_ARRAY_BUFFER, triangles.vboId[0]);
                glVertexAttribPointer(currentShader.locs[LOC_VERTEX_POSITION], 3, GL_FLOAT, 0, 0, 0);
                glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_POSITION]);

                // Bind vertex attrib: color (shader-location = 3)
                glBindBuffer(GL_ARRAY_BUFFER, triangles.vboId[1]);
                glVertexAttribPointer(currentShader.locs[LOC_VERTEX_COLOR], 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
                glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_COLOR]);
            }

            glDrawArrays(GL_TRIANGLES, 0, triangles.vCounter);

            if (!vaoSupported) glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Draw quads buffers
        if (quads.vCounter > 0)
        {
            int quadsCount = 0;
            int numIndicesToProcess = 0;
            int indicesOffset = 0;

            if (vaoSupported)
            {
                glBindVertexArray(quads.vaoId);
            }
            else
            {
                // Bind vertex attrib: position (shader-location = 0)
                glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[0]);
                glVertexAttribPointer(currentShader.locs[LOC_VERTEX_POSITION], 3, GL_FLOAT, 0, 0, 0);
                glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_POSITION]);

                // Bind vertex attrib: texcoord (shader-location = 1)
                glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[1]);
                glVertexAttribPointer(currentShader.locs[LOC_VERTEX_TEXCOORD01], 2, GL_FLOAT, 0, 0, 0);
                glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_TEXCOORD01]);

                // Bind vertex attrib: color (shader-location = 3)
                glBindBuffer(GL_ARRAY_BUFFER, quads.vboId[2]);
                glVertexAttribPointer(currentShader.locs[LOC_VERTEX_COLOR], 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
                glEnableVertexAttribArray(currentShader.locs[LOC_VERTEX_COLOR]);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quads.vboId[3]);
            }

            //TraceLog(LOG_DEBUG, "Draws required per frame: %i", drawsCounter);

            for (int i = 0; i < drawsCounter; i++)
            {
                quadsCount = draws[i].vertexCount/4;
                numIndicesToProcess = quadsCount*6;  // Get number of Quads*6 index by Quad

                //TraceLog(LOG_DEBUG, "Quads to render: %i - Vertex Count: %i", quadsCount, draws[i].vertexCount);

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, draws[i].textureId);

                // NOTE: The final parameter tells the GPU the offset in bytes from the start of the index buffer to the location of the first index to process
    #if defined(GRAPHICS_API_OPENGL_33)
                glDrawElements(GL_TRIANGLES, numIndicesToProcess, GL_UNSIGNED_INT, (GLvoid *)(sizeof(GLuint)*indicesOffset));
    #elif defined(GRAPHICS_API_OPENGL_ES2)
                glDrawElements(GL_TRIANGLES, numIndicesToProcess, GL_UNSIGNED_SHORT, (GLvoid *)(sizeof(GLushort)*indicesOffset));
    #endif
                //GLenum err;
                //if ((err = glGetError()) != GL_NO_ERROR) TraceLog(LOG_INFO, "OpenGL error: %i", (int)err);    //GL_INVALID_ENUM!

                indicesOffset += draws[i].vertexCount/4*6;
            }

            if (!vaoSupported)
            {
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            }

            glBindTexture(GL_TEXTURE_2D, 0);    // Unbind textures
        }

        if (vaoSupported) glBindVertexArray(0); // Unbind VAO

        glUseProgram(0);    // Unbind shader program
    }

    // Reset draws counter
    drawsCounter = 1;
    draws[0].textureId = whiteTexture;
    draws[0].vertexCount = 0;

    // Reset vertex counters for next frame
    lines.vCounter = 0;
    lines.cCounter = 0;
    triangles.vCounter = 0;
    triangles.cCounter = 0;
    quads.vCounter = 0;
    quads.tcCounter = 0;
    quads.cCounter = 0;

    // Reset depth for next draw
    currentDepth = -1.0f;

    // Restore projection/modelview matrices
    projection = matProjection;
    modelview = matModelView;
}

// Unload default internal buffers vertex data from CPU and GPU
static void UnloadBuffersDefault(void)
{
    // Unbind everything
    if (vaoSupported) glBindVertexArray(0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Delete VBOs from GPU (VRAM)
    glDeleteBuffers(1, &lines.vboId[0]);
    glDeleteBuffers(1, &lines.vboId[1]);
    glDeleteBuffers(1, &triangles.vboId[0]);
    glDeleteBuffers(1, &triangles.vboId[1]);
    glDeleteBuffers(1, &quads.vboId[0]);
    glDeleteBuffers(1, &quads.vboId[1]);
    glDeleteBuffers(1, &quads.vboId[2]);
    glDeleteBuffers(1, &quads.vboId[3]);

    if (vaoSupported)
    {
        // Delete VAOs from GPU (VRAM)
        glDeleteVertexArrays(1, &lines.vaoId);
        glDeleteVertexArrays(1, &triangles.vaoId);
        glDeleteVertexArrays(1, &quads.vaoId);
    }

    // Free vertex arrays memory from CPU (RAM)
    free(lines.vertices);
    free(lines.colors);

    free(triangles.vertices);
    free(triangles.colors);

    free(quads.vertices);
    free(quads.texcoords);
    free(quads.colors);
    free(quads.indices);
}

// Renders a 1x1 XY quad in NDC
static void GenDrawQuad(void)
{
    unsigned int quadVAO = 0;
    unsigned int quadVBO = 0;
    
    float vertices[] = {
        // Positions        // Texture Coords
        -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    };

    // Set up plane VAO
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);

    // Fill buffer
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);

    // Link vertex attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void *)(3*sizeof(float)));

    // Draw quad
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    
    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &quadVAO);
}

// Renders a 1x1 3D cube in NDC
static void GenDrawCube(void)
{
    unsigned int cubeVAO = 0;
    unsigned int cubeVBO = 0;

    float vertices[] = {
        -1.0f, -1.0f, -1.0f,  0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
        1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
        1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
        -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
        1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 1.0f , 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f
    };

    // Set up cube VAO
    glGenVertexArrays(1, &cubeVAO);
    glGenBuffers(1, &cubeVBO);

    // Fill buffer
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Link vertex attributes
    glBindVertexArray(cubeVAO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void *)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*sizeof(float), (void *)(6*sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Draw cube
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    
    glDeleteBuffers(1, &cubeVBO);
    glDeleteVertexArrays(1, &cubeVAO);
}

#if defined(SUPPORT_VR_SIMULATOR)
// Configure stereo rendering (including distortion shader) with HMD device parameters
// NOTE: It modifies the global variable: VrStereoConfig vrConfig
static void SetStereoConfig(VrDeviceInfo hmd)
{
    // Compute aspect ratio
    float aspect = ((float)hmd.hResolution*0.5f)/(float)hmd.vResolution;

    // Compute lens parameters
    float lensShift = (hmd.hScreenSize*0.25f - hmd.lensSeparationDistance*0.5f)/hmd.hScreenSize;
    float leftLensCenter[2] = { 0.25f + lensShift, 0.5f };
    float rightLensCenter[2] = { 0.75f - lensShift, 0.5f };
    float leftScreenCenter[2] = { 0.25f, 0.5f };
    float rightScreenCenter[2] = { 0.75f, 0.5f };

    // Compute distortion scale parameters
    // NOTE: To get lens max radius, lensShift must be normalized to [-1..1]
    float lensRadius = fabsf(-1.0f - 4.0f*lensShift);
    float lensRadiusSq = lensRadius*lensRadius;
    float distortionScale = hmd.lensDistortionValues[0] +
                            hmd.lensDistortionValues[1]*lensRadiusSq +
                            hmd.lensDistortionValues[2]*lensRadiusSq*lensRadiusSq +
                            hmd.lensDistortionValues[3]*lensRadiusSq*lensRadiusSq*lensRadiusSq;

    TraceLog(LOG_DEBUG, "VR: Distortion Scale: %f", distortionScale);

    float normScreenWidth = 0.5f;
    float normScreenHeight = 1.0f;
    float scaleIn[2] = { 2.0f/normScreenWidth, 2.0f/normScreenHeight/aspect };
    float scale[2] = { normScreenWidth*0.5f/distortionScale, normScreenHeight*0.5f*aspect/distortionScale };

    TraceLog(LOG_DEBUG, "VR: Distortion Shader: LeftLensCenter = { %f, %f }", leftLensCenter[0], leftLensCenter[1]);
    TraceLog(LOG_DEBUG, "VR: Distortion Shader: RightLensCenter = { %f, %f }", rightLensCenter[0], rightLensCenter[1]);
    TraceLog(LOG_DEBUG, "VR: Distortion Shader: Scale = { %f, %f }", scale[0], scale[1]);
    TraceLog(LOG_DEBUG, "VR: Distortion Shader: ScaleIn = { %f, %f }", scaleIn[0], scaleIn[1]);

#if defined(SUPPORT_DISTORTION_SHADER)
    // Update distortion shader with lens and distortion-scale parameters
    SetShaderValue(vrConfig.distortionShader, GetShaderLocation(vrConfig.distortionShader, "leftLensCenter"), leftLensCenter, 2);
    SetShaderValue(vrConfig.distortionShader, GetShaderLocation(vrConfig.distortionShader, "rightLensCenter"), rightLensCenter, 2);
    SetShaderValue(vrConfig.distortionShader, GetShaderLocation(vrConfig.distortionShader, "leftScreenCenter"), leftScreenCenter, 2);
    SetShaderValue(vrConfig.distortionShader, GetShaderLocation(vrConfig.distortionShader, "rightScreenCenter"), rightScreenCenter, 2);

    SetShaderValue(vrConfig.distortionShader, GetShaderLocation(vrConfig.distortionShader, "scale"), scale, 2);
    SetShaderValue(vrConfig.distortionShader, GetShaderLocation(vrConfig.distortionShader, "scaleIn"), scaleIn, 2);
    SetShaderValue(vrConfig.distortionShader, GetShaderLocation(vrConfig.distortionShader, "hmdWarpParam"), hmd.lensDistortionValues, 4);
    SetShaderValue(vrConfig.distortionShader, GetShaderLocation(vrConfig.distortionShader, "chromaAbParam"), hmd.chromaAbCorrection, 4);
#endif

    // Fovy is normally computed with: 2*atan2(hmd.vScreenSize, 2*hmd.eyeToScreenDistance)
    // ...but with lens distortion it is increased (see Oculus SDK Documentation)
    //float fovy = 2.0f*atan2(hmd.vScreenSize*0.5f*distortionScale, hmd.eyeToScreenDistance);     // Really need distortionScale?
    float fovy = 2.0f*(float)atan2(hmd.vScreenSize*0.5f, hmd.eyeToScreenDistance);

    // Compute camera projection matrices
    float projOffset = 4.0f*lensShift;      // Scaled to projection space coordinates [-1..1]
    Matrix proj = MatrixPerspective(fovy, aspect, 0.01, 1000.0);
    vrConfig.eyesProjection[0] = MatrixMultiply(proj, MatrixTranslate(projOffset, 0.0f, 0.0f));
    vrConfig.eyesProjection[1] = MatrixMultiply(proj, MatrixTranslate(-projOffset, 0.0f, 0.0f));

    // Compute camera transformation matrices
    // NOTE: Camera movement might seem more natural if we model the head.
    // Our axis of rotation is the base of our head, so we might want to add
    // some y (base of head to eye level) and -z (center of head to eye protrusion) to the camera positions.
    vrConfig.eyesViewOffset[0] = MatrixTranslate(-hmd.interpupillaryDistance*0.5f, 0.075f, 0.045f);
    vrConfig.eyesViewOffset[1] = MatrixTranslate(hmd.interpupillaryDistance*0.5f, 0.075f, 0.045f);

    // Compute eyes Viewports
    vrConfig.eyesViewport[0] = (Rectangle){ 0, 0, hmd.hResolution/2, hmd.vResolution };
    vrConfig.eyesViewport[1] = (Rectangle){ hmd.hResolution/2, 0, hmd.hResolution/2, hmd.vResolution };
}

// Set internal projection and modelview matrix depending on eyes tracking data
static void SetStereoView(int eye, Matrix matProjection, Matrix matModelView)
{
    Matrix eyeProjection = matProjection;
    Matrix eyeModelView = matModelView;

    // Setup viewport and projection/modelview matrices using tracking data
    rlViewport(eye*screenWidth/2, 0, screenWidth/2, screenHeight);

    // Apply view offset to modelview matrix
    eyeModelView = MatrixMultiply(matModelView, vrConfig.eyesViewOffset[eye]);

    // Set current eye projection matrix
    eyeProjection = vrConfig.eyesProjection[eye];

    SetMatrixModelview(eyeModelView);
    SetMatrixProjection(eyeProjection);
}
#endif      // defined(SUPPORT_VR_SIMULATOR)

#endif //defined(GRAPHICS_API_OPENGL_33) || defined(GRAPHICS_API_OPENGL_ES2)

#if defined(GRAPHICS_API_OPENGL_11)
// Mipmaps data is generated after image data
static int GenerateMipmaps(unsigned char *data, int baseWidth, int baseHeight)
{
    int mipmapCount = 1;                // Required mipmap levels count (including base level)
    int width = baseWidth;
    int height = baseHeight;
    int size = baseWidth*baseHeight*4;  // Size in bytes (will include mipmaps...), RGBA only

    // Count mipmap levels required
    while ((width != 1) && (height != 1))
    {
        if (width != 1) width /= 2;
        if (height != 1) height /= 2;

        TraceLog(LOG_DEBUG, "Next mipmap size: %i x %i", width, height);

        mipmapCount++;

        size += (width*height*4);       // Add mipmap size (in bytes)
    }

    TraceLog(LOG_DEBUG, "Total mipmaps required: %i", mipmapCount);
    TraceLog(LOG_DEBUG, "Total size of data required: %i", size);

    unsigned char *temp = realloc(data, size);

    if (temp != NULL) data = temp;
    else TraceLog(LOG_WARNING, "Mipmaps required memory could not be allocated");

    width = baseWidth;
    height = baseHeight;
    size = (width*height*4);

    // Generate mipmaps
    // NOTE: Every mipmap data is stored after data
    Color *image = (Color *)malloc(width*height*sizeof(Color));
    Color *mipmap = NULL;
    int offset = 0;
    int j = 0;

    for (int i = 0; i < size; i += 4)
    {
        image[j].r = data[i];
        image[j].g = data[i + 1];
        image[j].b = data[i + 2];
        image[j].a = data[i + 3];
        j++;
    }

    TraceLog(LOG_DEBUG, "Mipmap base (%ix%i)", width, height);

    for (int mip = 1; mip < mipmapCount; mip++)
    {
        mipmap = GenNextMipmap(image, width, height);

        offset += (width*height*4); // Size of last mipmap
        j = 0;

        width /= 2;
        height /= 2;
        size = (width*height*4);    // Mipmap size to store after offset

        // Add mipmap to data
        for (int i = 0; i < size; i += 4)
        {
            data[offset + i] = mipmap[j].r;
            data[offset + i + 1] = mipmap[j].g;
            data[offset + i + 2] = mipmap[j].b;
            data[offset + i + 3] = mipmap[j].a;
            j++;
        }

        free(image);

        image = mipmap;
        mipmap = NULL;
    }

    free(mipmap);       // free mipmap data

    return mipmapCount;
}

// Manual mipmap generation (basic scaling algorithm)
static Color *GenNextMipmap(Color *srcData, int srcWidth, int srcHeight)
{
    int x2, y2;
    Color prow, pcol;

    int width = srcWidth/2;
    int height = srcHeight/2;

    Color *mipmap = (Color *)malloc(width*height*sizeof(Color));

    // Scaling algorithm works perfectly (box-filter)
    for (int y = 0; y < height; y++)
    {
        y2 = 2*y;

        for (int x = 0; x < width; x++)
        {
            x2 = 2*x;

            prow.r = (srcData[y2*srcWidth + x2].r + srcData[y2*srcWidth + x2 + 1].r)/2;
            prow.g = (srcData[y2*srcWidth + x2].g + srcData[y2*srcWidth + x2 + 1].g)/2;
            prow.b = (srcData[y2*srcWidth + x2].b + srcData[y2*srcWidth + x2 + 1].b)/2;
            prow.a = (srcData[y2*srcWidth + x2].a + srcData[y2*srcWidth + x2 + 1].a)/2;

            pcol.r = (srcData[(y2+1)*srcWidth + x2].r + srcData[(y2+1)*srcWidth + x2 + 1].r)/2;
            pcol.g = (srcData[(y2+1)*srcWidth + x2].g + srcData[(y2+1)*srcWidth + x2 + 1].g)/2;
            pcol.b = (srcData[(y2+1)*srcWidth + x2].b + srcData[(y2+1)*srcWidth + x2 + 1].b)/2;
            pcol.a = (srcData[(y2+1)*srcWidth + x2].a + srcData[(y2+1)*srcWidth + x2 + 1].a)/2;

            mipmap[y*width + x].r = (prow.r + pcol.r)/2;
            mipmap[y*width + x].g = (prow.g + pcol.g)/2;
            mipmap[y*width + x].b = (prow.b + pcol.b)/2;
            mipmap[y*width + x].a = (prow.a + pcol.a)/2;
        }
    }

    TraceLog(LOG_DEBUG, "Mipmap generated successfully (%ix%i)", width, height);

    return mipmap;
}
#endif

#if defined(RLGL_STANDALONE)
// Show trace log messages (LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_DEBUG)
void TraceLog(int msgType, const char *text, ...)
{
    va_list args;
    va_start(args, text);

    switch (msgType)
    {
        case LOG_INFO: fprintf(stdout, "INFO: "); break;
        case LOG_ERROR: fprintf(stdout, "ERROR: "); break;
        case LOG_WARNING: fprintf(stdout, "WARNING: "); break;
        case LOG_DEBUG: fprintf(stdout, "DEBUG: "); break;
        default: break;
    }

    vfprintf(stdout, text, args);
    fprintf(stdout, "\n");

    va_end(args);

    if (msgType == LOG_ERROR) exit(1);
}
#endif

void rlSetMarker(const char *text) {
    if(debugMarkerSupported)
        glInsertEventMarkerEXT(0, text);            //0 terminated string
}
