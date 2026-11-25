// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#pragma once

#include <common/fs.h>
#include <stdint.h>
#include <vector>

class CScreenshotBuilder
{
public:
    CScreenshotBuilder();
    virtual ~CScreenshotBuilder();

    void SaveScreen();
    void SaveScreen(int x, int y, int width, int height);

    bool Capture(const fs_path &directory, uint8_t format, bool gameViewOnly);
    void CaptureFrame();
    void GPUDataReady();

private:
    bool Request(int x, int y, int width, int height, const fs_path &directory, uint8_t format);
    void GetScenePixels(int x, int y, int width, int height);
    void WritePixelsToDisk();

    bool m_WaitingForGPUData;
    bool m_Requested = false;
    int m_RequestX = 0;
    int m_RequestY = 0;
    int m_RequestWidth = 0;
    int m_RequestHeight = 0;
    uint32_t m_Width;
    uint32_t m_Height;
    std::vector<uint32_t> m_Pixels;
    fs_path m_Directory;
    uint8_t m_Format = 0;
};

extern CScreenshotBuilder g_ScreenshotBuilder;
