// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#include "ScreenshotBuilder.h"
#include "GameWindow.h"
#include <algorithm>
#include <cstdint>
#include <xuocore/uodata.h>
#include <time.h>
#include "CrossUO.h"
#include "Globals.h"
#include "Managers/ConfigManager.h"
#include "Platform.h"
#include "Renderer/RenderAPI.h"
#include "Renderer/RenderCommands.h"

#define STBIWDEF static inline
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <external/stb_image_write.h>

CScreenshotBuilder g_ScreenshotBuilder;

void CScreenshotBuilder::GetScenePixels(int x, int y, int width, int height)
{
    m_Pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
    m_Width = width;
    m_Height = height;

    const auto windowSize = g_GameWindow.GetSize();
    auto window_width = uint32_t(windowSize.Width);
    auto window_height = uint32_t(windowSize.Height);
    m_WaitingForGPUData = RenderAdd_GetFrameBufferPixels(
        g_renderCmdList,
        GetFrameBufferPixelsCmd{ x,
                                 y,
                                 uint32_t(width),
                                 uint32_t(height),
                                 window_width,
                                 window_height,
                                 &m_Pixels[0],
                                 m_Pixels.size() * sizeof(decltype(m_Pixels)::value_type) });
}

CScreenshotBuilder::CScreenshotBuilder()
    : m_WaitingForGPUData(false)
    , m_Width(0)
    , m_Height(0)
{
}

CScreenshotBuilder::~CScreenshotBuilder()
= default;

void CScreenshotBuilder::SaveScreen()
{
    SaveScreen(0, 0, g_GameWindow.GetSize().Width, g_GameWindow.GetSize().Height);
}

void CScreenshotBuilder::WritePixelsToDisk()
{
    const auto directory = m_Directory;
    fs_path_create(directory);

    auto t = time(nullptr);
    auto now = *localtime(&t);
    char buf[100]{};
    sprintf_s(
        buf,
        "%04d%02d%02d_%02d%02d%02d",
        now.tm_year + 1900,
        now.tm_mon + 1,
        now.tm_mday,
        now.tm_hour,
        now.tm_min,
        now.tm_sec);
    const astr_t filename = buf;
    static const char *const extensions[] = { ".bmp", ".png", ".tga", ".jpg" };
    const char *extension = extensions[m_Format];
    auto path = fs_path_join(directory, filename + extension);
    unsigned suffix = 0;
    while (fs_path_exists(path))
    {
        path = fs_path_join(directory, filename + "-" + std::to_string(++suffix) + extension);
    }

    int result = 0;
    auto *data = (void *)&m_Pixels[0];
    for (uint32_t &i : m_Pixels)
    {
        i |= 0xFF000000;
    }

    stbi_flip_vertically_on_write(true);
    switch (m_Format)
    {
        case SF_PNG:
        {
            result = stbi_write_png(fs_path_ascii(path), m_Width, m_Height, 4, data, m_Width * 4);
            break;
        }
        case SF_TGA:
        {
            result = stbi_write_tga(fs_path_ascii(path), m_Width, m_Height, 4, data);
            break;
        }
        case SF_JPG:
        {
            result = stbi_write_jpg(fs_path_ascii(path), m_Width, m_Height, 4, data, 100);
            break;
        }
        default:
        {
            result = stbi_write_bmp(fs_path_ascii(path), m_Width, m_Height, 4, data);
            break;
        }
    }
    if (result == 0)
    {
        g_Game.CreateTextMessageF(3, 0, "Failed to write screenshot");
        return;
    }

    if (g_GameState >= GS_GAME)
    {
        g_Game.CreateTextMessageF(3, 0, "Screenshot saved to: %s", fs_path_ascii(path));
    }
}

void CScreenshotBuilder::SaveScreen(int x, int y, int width, int height)
{
    Request(x, y, width, height, ExeFilePath("screenshots"), g_ConfigManager.ScreenshotFormat);
}

bool CScreenshotBuilder::Capture(const fs_path &directory, uint8_t format, bool gameViewOnly)
{
    const auto window = g_GameWindow.GetSize();
    if (!gameViewOnly)
    {
        return Request(0, 0, window.Width, window.Height, directory, format);
    }

    const int x = std::max(0, g_ConfigManager.GameWindowX);
    const int y = std::max(0, g_ConfigManager.GameWindowY);
    const int width = std::min<int>(g_ConfigManager.GameWindowWidth, window.Width - x);
    const int height = std::min<int>(g_ConfigManager.GameWindowHeight, window.Height - y);
    return Request(x, y, width, height, directory, format);
}

bool CScreenshotBuilder::Request(
    int x, int y, int width, int height, const fs_path &directory, uint8_t format)
{
    if (width <= 0 || height <= 0 || fs_path_empty(directory))
    {
        return false;
    }

    if (m_Requested || m_WaitingForGPUData)
    {
        g_Game.CreateTextMessageF(3, 0, "A screenshot command is already in progress.");
        return false;
    }

    m_Directory = directory;
    m_Format = format <= SF_JPG ? format : SF_PNG;
    m_RequestX = x;
    m_RequestY = y;
    m_RequestWidth = width;
    m_RequestHeight = height;
    m_Requested = true;
    return true;
}

void CScreenshotBuilder::CaptureFrame()
{
    if (!m_Requested)
    {
        return;
    }
    m_Requested = false;
    const auto window = g_GameWindow.GetSize();
    const int x = std::max(0, m_RequestX);
    const int y = std::max(0, m_RequestY);
    const int width = std::min(window.Width - x, m_RequestWidth - (x - m_RequestX));
    const int height = std::min(window.Height - y, m_RequestHeight - (y - m_RequestY));
    if (width > 0 && height > 0)
    {
        GetScenePixels(x, y, width, height);
    }
    if (!m_WaitingForGPUData)
    {
        g_Game.CreateTextMessageF(3, 0, "Could not read screenshot pixels.");
    }
}

void CScreenshotBuilder::GPUDataReady()
{
    if (m_WaitingForGPUData)
    {
        WritePixelsToDisk();
    }
    m_WaitingForGPUData = false;
}
