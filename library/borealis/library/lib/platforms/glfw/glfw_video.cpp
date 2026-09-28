/*
    Copyright 2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/platforms/glfw/glfw_video.hpp>

// nanovg implementation
#ifdef BOREALIS_USE_OPENGL
#include <glad/glad.h>
#ifdef USE_GL2
#define NANOVG_GL2_IMPLEMENTATION
#elif defined(USE_GLES2)
#define NANOVG_GLES2_IMPLEMENTATION
#elif defined(USE_GLES3)
#define NANOVG_GLES3_IMPLEMENTATION
#else
#define NANOVG_GL3_IMPLEMENTATION
#endif /* USE_GL2 */
#include <nanovg_gl.h>
#elif defined(BOREALIS_USE_METAL)
static void* METAL_CONTEXT = nullptr;
#include <borealis/platforms/glfw/driver/metal.hpp>
#elif defined(BOREALIS_USE_D3D11)
#include <nanovg_d3d11.h>

#include <borealis/platforms/driver/d3d11.hpp>
std::unique_ptr<brls::D3D11Context> D3D11_CONTEXT;
#endif

#if defined(__linux__) || defined(_WIN32)
#include "stb_image.h"
#ifdef USE_LIBROMFS
#include <romfs/romfs.hpp>
#else
#include <unistd.h>
#endif
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace brls
{

static double scaleFactor = 1.0;

static int mini(int x, int y)
{
    return x < y ? x : y;
}

static int maxi(int x, int y)
{
    return x > y ? x : y;
}

static GLFWmonitor* getCurrentMonitor(GLFWwindow* window)
{
    int nmonitors, i;
    int wx, wy, ww, wh;
    int mx, my, mw, mh;
    int overlap, bestoverlap;
    GLFWmonitor* bestmonitor;
    GLFWmonitor** monitors;
    const GLFWvidmode* mode;

    bestoverlap = 0;
    bestmonitor = NULL;

    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    monitors = glfwGetMonitors(&nmonitors);

    for (i = 0; i < nmonitors; i++)
    {
        mode = glfwGetVideoMode(monitors[i]);
        glfwGetMonitorPos(monitors[i], &mx, &my);
        if (!mode) continue;
        mw = mode->width;
        mh = mode->height;

        overlap = maxi(0, mini(wx + ww, mx + mw) - maxi(wx, mx)) * maxi(0, mini(wy + wh, my + mh) - maxi(wy, my));

        if (bestoverlap < overlap)
        {
            bestoverlap = overlap;
            bestmonitor = monitors[i];
        }
    }

    return bestmonitor;
}

static GLFWmonitor* getAvailableMonitor(int index, int x, int y, int w, int h)
{
    int count;
    auto** monitors      = glfwGetMonitors(&count);
    GLFWmonitor* monitor = nullptr;
    if (monitors && index >= 0 && index < count)
        monitor = monitors[index];
    else
        return nullptr;

    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return nullptr;
    int monitorX, monitorY;
    glfwGetMonitorPos(monitor, &monitorX, &monitorY);

    if (x < monitorX || y < monitorY || x + w > mode->width + monitorX || y + h > mode->height + monitorY)
        return nullptr;
    return monitor;
}

static void glfwWindowFramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    if (!width || !height)
        return;
#ifdef BOREALIS_USE_OPENGL
    glViewport(0, 0, width, height);
    int wWidth, wHeight;
    glfwGetWindowSize(window, &wWidth, &wHeight);
    scaleFactor = width * 1.0 / wWidth;
#elif defined(BOREALIS_USE_METAL)
    if (METAL_CONTEXT == nullptr)
    {
        return;
    }
    scaleFactor = GetMetalScaleFactor(METAL_CONTEXT);
    ResizeMetalDrawable(METAL_CONTEXT, width, height);
    int wWidth, wHeight;
    glfwGetWindowSize(window, &wWidth, &wHeight);
    // cocoa 画布大小和窗口一致
    width  = wWidth;
    height = wHeight;
#elif defined(BOREALIS_USE_D3D11)
    if (D3D11_CONTEXT == nullptr)
    {
        return;
    }
    scaleFactor = D3D11_CONTEXT->getScaleFactor();
    int wWidth = width, wHeight = height;
    D3D11_CONTEXT->onFramebufferSize(width, height);
#endif

    Application::onWindowResized(width, height);

    if (!VideoContext::FULLSCREEN)
    {
        VideoContext::sizeW = wWidth;
        VideoContext::sizeH = wHeight;
    }
}

static void glfwWindowPositionCallback(GLFWwindow* window, int windowXPos, int windowYPos)
{
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) return;
    Application::onWindowReposition(windowXPos, windowYPos);

    if (!VideoContext::FULLSCREEN)
    {
        VideoContext::posX = (float)windowXPos;
        VideoContext::posY = (float)windowYPos;
    }
}

GLFWVideoContext::GLFWVideoContext(const std::string& windowTitle, uint32_t windowWidth, uint32_t windowHeight, float windowX, float windowY)
{
    if (!glfwInit())
    {
        fatal("glfw: failed to initialize");
    }

    // Create window
#ifdef BOREALIS_USE_OPENGL
#if defined(USE_GLES2)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#elif defined(USE_GLES3)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#elif defined(USE_GL2)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_FALSE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
#elif defined(__SWITCH__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#elif defined(__linux__) || defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(USE_EGL)
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif
    glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GL_TRUE);
#endif
#else
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    // If the window appears outside the screen, using the default settings
    GLFWmonitor* monitor = nullptr;
    if (!std::isnan(windowX) && !std::isnan(windowY))
        monitor = getAvailableMonitor(VideoContext::monitorIndex, (int)windowX, (int)windowY, (int)windowWidth, (int)windowHeight);

    if (monitor == nullptr)
    {
        windowX      = NAN;
        windowY      = NAN;
        windowWidth  = Application::ORIGINAL_WINDOW_WIDTH;
        windowHeight = Application::ORIGINAL_WINDOW_HEIGHT;
        monitor      = glfwGetPrimaryMonitor();
    }
    // Windows can temporarily report no monitor while the display topology changes.
    GLFWvidmode fallbackMode{(int)windowWidth, (int)windowHeight, 8, 8, 8, 60};
#ifdef _WIN32
    DEVMODEW desktopMode{};
    desktopMode.dmSize = sizeof(desktopMode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &desktopMode)) {
        fallbackMode.width = desktopMode.dmPelsWidth;
        fallbackMode.height = desktopMode.dmPelsHeight;
        fallbackMode.refreshRate = desktopMode.dmDisplayFrequency;
    }
#endif
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode) {
        Logger::warning("No GLFW monitor mode available; using desktop window bounds");
        mode = &fallbackMode;
    }
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    glfwWindowHint(GLFW_AUTO_ICONIFY, 0);
    glfwWindowHint(GLFW_SOFT_FULLSCREEN, 1);
#endif

// create window
#if defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
#ifdef _WIN32
    // Windows 下先创建隐藏窗口，等第一帧真正呈现后再显示，
    // 否则窗口会以未绘制的黑色（或未初始化的画面）闪一下。
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    this->pendingFirstFrameShow = true;
#endif
    if (VideoContext::FULLSCREEN)
    {
#ifdef _WIN32
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        this->window = glfwCreateWindow(mode->width, mode->height, windowTitle.c_str(), nullptr, nullptr);
        int mx = 0, my = 0;
        if (monitor) glfwGetMonitorPos(monitor, &mx, &my);
        glfwSetWindowPos(this->window, mx, my);
#else
        this->window = glfwCreateWindow(mode->width, mode->height, windowTitle.c_str(), monitor, nullptr);
#endif
#ifdef _WIN32
        // glfw will disable screen sleep when in full-screen mode
        // We will cancel it here and let the application handle this issue internally
        // X11 and wayland may have similar issues
        SetThreadExecutionState(ES_CONTINUOUS);
#endif
    }
    else
#endif
    {
        this->window = glfwCreateWindow((int)windowWidth, (int)windowHeight, windowTitle.c_str(), nullptr, nullptr);
    }

    if (!this->window)
    {
        glfwTerminate();
        fatal("glfw: Failed to create window");
        return;
    }

#if defined(__linux__) || defined(_WIN32)
    // Set window icon
    GLFWimage images[1];
#ifdef USE_LIBROMFS
    try
    {
        auto& icon       = romfs::get("icon/icon.png");
        images[0].pixels = stbi_load_from_memory((stbi_uc*)icon.data(), icon.size(), &images[0].width, &images[0].height, 0, 4);
        glfwSetWindowIcon(this->window, 1, images);
    }
    catch (...)
    {
    }
#else
    const char* icon_path = BRLS_ASSET("icon/icon.png");
    if (access(icon_path, F_OK) != -1)
    {
        images[0].pixels = stbi_load(icon_path, &images[0].width, &images[0].height, 0, 4);
        glfwSetWindowIcon(this->window, 1, images);
    }
#endif
#endif

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    // Set window position
    if (!VideoContext::FULLSCREEN)
    {
        if (mode->width >= (int)windowWidth && mode->height >= (int)windowHeight)
        {
            if (!std::isnan(windowX) && !std::isnan(windowY))
            {
                glfwSetWindowPos(this->window, (int)windowX, (int)windowY);
            }
            else
            {
                // When there is no specified window position, center the window
                glfwSetWindowPos(this->window, (int)(mode->width - windowWidth) / 2,
                    (int)(mode->height - windowHeight) / 2);
            }
        }
        else
        {
            // When the window size is too large, reduce the size and center it
            float maxAllowedWidth  = (float)mode->width * 0.8f;
            float maxAllowedHeight = (float)mode->height * 0.8f;
            float scale     = std::min(maxAllowedWidth / (float)windowWidth, maxAllowedHeight / (float)windowHeight);
            float newWidth  = (float)windowWidth * scale;
            float newHeight = (float)windowHeight * scale;

            glfwSetWindowSize(this->window, (int)newWidth, (int)newHeight);

            glfwSetWindowPos(this->window, (mode->width - newWidth) / 2,
                (mode->height - newHeight) / 2);
        }
    }
#endif

    // Configure window
    glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_TRUE);
#if defined(__APPLE__) || defined(_WIN32)
    // Preserve brief touchpad/mouse clicks even at the idle frame rate.
    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
#endif
    glfwSetFramebufferSizeCallback(window, glfwWindowFramebufferSizeCallback);
    glfwSetWindowPosCallback(window, glfwWindowPositionCallback);

#ifdef BOREALIS_USE_OPENGL
    glfwMakeContextCurrent(window);
    // Load OpenGL routines using glad
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    Logger::info("glfw: GL Vendor: {}", (const char*)glGetString(GL_VENDOR));
    Logger::info("glfw: GL Renderer: {}", (const char*)glGetString(GL_RENDERER));
    Logger::info("glfw: GL Version: {}", (const char*)glGetString(GL_VERSION));
#endif
    Logger::info("glfw: GLFW Version: {}.{}.{}", GLFW_VERSION_MAJOR, GLFW_VERSION_MINOR, GLFW_VERSION_REVISION);

    // Initialize nanovg
#ifdef BOREALIS_USE_OPENGL
#ifdef USE_GLES2
    this->nvgContext = nvgCreateGLES2(0);
#elif defined(USE_GLES3)
    this->nvgContext = nvgCreateGLES3(0);
#elif defined(USE_GL2)
    this->nvgContext = nvgCreateGL2(NVG_STENCIL_STROKES | NVG_ANTIALIAS);
#else
    this->nvgContext = nvgCreateGL3(NVG_STENCIL_STROKES | NVG_ANTIALIAS);
#endif
#elif defined(BOREALIS_USE_METAL)
    Logger::info("glfw: USE_METAL");
    void* ctx        = CreateMetalContext(window);
    METAL_CONTEXT    = ctx;
    this->nvgContext = nvgCreateMTL(GetMetalLayer(ctx), NVG_STENCIL_STROKES | NVG_ANTIALIAS);
    scaleFactor      = GetMetalScaleFactor(ctx);
#elif defined(BOREALIS_USE_D3D11)
    Logger::info("glfw: USE_D3D11");
    D3D11_CONTEXT    = std::make_unique<D3D11Context>(this->window, windowWidth, windowHeight);
    this->nvgContext = nvgCreateD3D11(D3D11_CONTEXT->getDevice(), NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    scaleFactor      = D3D11_CONTEXT->getScaleFactor();
#endif
    if (!this->nvgContext)
    {
        Logger::error("glfw: unable to init nanovg");
        glfwTerminate();
        return;
    }

    setSwapInterval(VideoContext::swapInterval);

    // Setup window state
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    Application::setWindowSize(width, height);

    int wWidth, wHeight;
    glfwGetWindowSize(window, &wWidth, &wHeight);

    int xPos, yPos;
    glfwGetWindowPos(window, &xPos, &yPos);
    Application::setWindowPosition(xPos, yPos);

#if defined(BOREALIS_USE_D3D11)
    D3D11_CONTEXT->onFramebufferSize(width, height);
#elif defined(BOREALIS_USE_METAL)
#else
    scaleFactor = width * 1.0 / wWidth;
    glViewport(0, 0, width, height);
#endif

    if (!VideoContext::FULLSCREEN)
    {
        VideoContext::sizeW = wWidth;
        VideoContext::sizeH = wHeight;
        VideoContext::posX  = (float)xPos;
        VideoContext::posY  = (float)yPos;
    }

#ifdef __SWITCH__
    this->monitor    = glfwGetPrimaryMonitor();
    const char* name = glfwGetMonitorName(monitor);
    brls::Logger::info("glfw: Monitor: {}", name);
#endif
}

void GLFWVideoContext::beginFrame()
{
#ifdef __SWITCH__
    const GLFWvidmode* r = glfwGetVideoMode(monitor);

    if (oldWidth != r->width || oldHeight != r->height)
    {
        oldWidth  = r->width;
        oldHeight = r->height;

        glfwSetWindowSize(window, r->width, r->height);
    }
#elif defined(BOREALIS_USE_D3D11)
    D3D11_CONTEXT->beginFrame();
#endif
}

void GLFWVideoContext::endFrame()
{
#ifdef BOREALIS_USE_OPENGL
    glfwSwapBuffers(this->window);
#ifdef _WIN32
    if (this->pendingFirstFrameShow)
    {
        // 首帧已经提交到窗口，此时再显示，启动过程不会出现闪黑。
        this->pendingFirstFrameShow = false;
        glfwShowWindow(this->window);
        brls::Logger::info("glfw: window shown after the first presented frame");
    }
#endif
#elif defined(BOREALIS_USE_D3D11)
    D3D11_CONTEXT->endFrame();
#endif
}

void GLFWVideoContext::setSwapInterval(int interval)
{
    VideoContext::swapInterval = interval;
#ifdef BOREALIS_USE_D3D11
    D3D11_CONTEXT->setSwapInterval(interval);
#else
    glfwSwapInterval(interval);
#endif
}


void GLFWVideoContext::clear(NVGcolor color)
{

#ifdef BOREALIS_USE_OPENGL
#ifdef _WIN32
    // mpv and NanoVG share GL state. A frame clear must not inherit clipping or
    // write masks from the previous page, video, or stencil operation.
    if (glBindFramebuffer) glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(~0u);
#endif
    glClearColor(
        color.r,
        color.g,
        color.b,
        color.a);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#elif defined(BOREALIS_USE_METAL)
    nvgClearWithColor(nvgContext, nvgRGBAf(color.r, color.g, color.b, color.a));
#elif defined(BOREALIS_USE_D3D11)
    D3D11_CONTEXT->clear(nvgRGBAf(
        color.r,
        color.g,
        color.b,
        color.a));
#endif
}

void GLFWVideoContext::resetState()
{
#ifdef BOREALIS_USE_OPENGL
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
#endif
}

double GLFWVideoContext::getScaleFactor()
{
    return scaleFactor;
}

GLFWVideoContext::~GLFWVideoContext()
{
    try
    {
        if (this->nvgContext)
        {
#ifdef BOREALIS_USE_OPENGL
#ifdef USE_GLES2
            nvgDeleteGLES2(this->nvgContext);
#elif defined(USE_GLES3)
            nvgDeleteGLES3(this->nvgContext);
#elif defined(USE_GL2)
            nvgDeleteGL2(this->nvgContext);
#else
            nvgDeleteGL3(this->nvgContext);
#endif
#elif defined(BOREALIS_USE_METAL)
            nvgDeleteMTL(this->nvgContext);
            METAL_CONTEXT = nullptr;
#elif defined(BOREALIS_USE_D3D11)
            nvgDeleteD3D11(this->nvgContext);
            D3D11_CONTEXT = nullptr;
#endif
        }
    }
    catch (...)
    {
        Logger::error("Cannot delete nvg Context");
    }
    glfwDestroyWindow(this->window);
    glfwTerminate();
}

NVGcontext* GLFWVideoContext::getNVGContext()
{
    return this->nvgContext;
}

int GLFWVideoContext::getCurrentMonitorIndex()
{
    if (!this->window)
        return 0;

    int count;
    auto* monitor   = getCurrentMonitor(this->window);
    auto** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; i++)
    {
        if (monitor == monitors[i])
            return i;
    }
    return 0;
}

void GLFWVideoContext::fullScreen(bool fs)
{
    // Decoration changes can synchronously fire resize/position callbacks.
    const auto restoreW = sizeW, restoreH = sizeH;
    const auto restoreX = posX, restoreY = posY;
    const bool previousFullscreen = VideoContext::FULLSCREEN;
    VideoContext::FULLSCREEN = fs;

    brls::Logger::info("Set fullscreen: {}", fs);
    if (fs)
    {
        GLFWmonitor* monitor    = getCurrentMonitor(this->window);
        if (!monitor) monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) {
            VideoContext::FULLSCREEN = previousFullscreen;
            Logger::warning("Fullscreen deferred: no monitor mode is available");
            return;
        }

        VideoContext::monitorIndex = getCurrentMonitorIndex();
#ifdef _WIN32
        int mx = 0, my = 0;
        if (monitor) glfwGetMonitorPos(monitor, &mx, &my);
        glfwSetWindowAttrib(this->window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowAttrib(this->window, GLFW_FLOATING, GLFW_FALSE);
        glfwSetWindowMonitor(this->window, nullptr, mx, my, mode->width, mode->height, GLFW_DONT_CARE);
#else
        glfwSetWindowMonitor(this->window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
#endif
#ifdef _WIN32
        // glfw will disable screen sleep when in full-screen mode
        // We will cancel it here and let the application handle this issue internally
        // X11 and wayland may have similar issues
        if (!Application::getPlatform()->isScreenDimmingDisabled())
        {
            SetThreadExecutionState(ES_CONTINUOUS);
        }
#endif
    }
    else
    {
#ifdef _WIN32
        GLFWmonitor* monitor = getCurrentMonitor(this->window);
        if (!monitor) monitor = glfwGetPrimaryMonitor();
        glfwSetWindowAttrib(this->window, GLFW_DECORATED, GLFW_TRUE);
#else
        GLFWmonitor* monitor    = glfwGetWindowMonitor(this->window);
#endif
        // already in windowed mode
        if (monitor == nullptr)
            return;
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (!mode) return;
        int monitorX, monitorY;
        glfwGetMonitorPos(monitor, &monitorX, &monitorY);
        glfwRestoreWindow(this->window);

        if (restoreW == 0 || restoreH == 0 || restoreX < monitorX || restoreY < monitorY || restoreX + restoreW > mode->width + monitorX || restoreY + restoreH > mode->height + monitorY)
        {
            int width  = Application::ORIGINAL_WINDOW_WIDTH;
            int height = Application::ORIGINAL_WINDOW_HEIGHT;
            // If the window appears outside the screen, using the default settings
            glfwSetWindowMonitor(this->window, nullptr, monitorX + fabs(mode->width - width) / 2,
                monitorY + fabs(mode->height - height) / 2, width, height, GLFW_DONT_CARE);
        }
        else
        {
            // Set the window position and size
            glfwSetWindowMonitor(this->window, nullptr, (int)restoreX, (int)restoreY, (int)restoreW, (int)restoreH, mode->refreshRate);
        }
    }
#ifdef BOREALIS_USE_OPENGL
    glfwSwapInterval(VideoContext::swapInterval);
#endif
}

GLFWwindow* GLFWVideoContext::getGLFWWindow()
{
    return this->window;
}

} // namespace brls
