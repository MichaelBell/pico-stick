#pragma once

#ifndef SUPPORT_WIDE_MODES
#define SUPPORT_WIDE_MODES 0
#endif

constexpr int PALETTE_SIZE = 32;
constexpr int NUM_SCROLL_GROUPS = 8;

#if !SUPPORT_WIDE_MODES
// Support for normal modes, require <300MHz overclock, 
// work on pretty much any screen, up to 80 sprites (if they are small)
constexpr int MAX_SPRITES = 80;
constexpr int MAX_FRAME_WIDTH = 720;
constexpr int MAX_FRAME_HEIGHT = 576;
constexpr int MAX_SPRITE_DATA_BYTES = 57344;
constexpr int MAX_SPRITE_WIDTH = 64;
constexpr int MAX_SPRITE_HEIGHT = 32;
constexpr int MAX_PATCHES_PER_LINE = 10;
constexpr int NUM_LINE_BUFFERS = 4;
constexpr int NUM_TMDS_BUFFERS = 8;
#else
// Support for modes up to 720p30, require extreme overclocks
// doesn't work on all screens.  Only 32 sprites and 20kB active sprite data
constexpr int MAX_SPRITES = 32;
constexpr int MAX_FRAME_WIDTH = 1280;
constexpr int MAX_FRAME_HEIGHT = 720;
constexpr int MAX_SPRITE_DATA_BYTES = 20480;
constexpr int MAX_SPRITE_WIDTH = 64;
constexpr int MAX_SPRITE_HEIGHT = 32;
constexpr int MAX_PATCHES_PER_LINE = 10;
constexpr int NUM_LINE_BUFFERS = 4;
constexpr int NUM_TMDS_BUFFERS = 7;
#endif