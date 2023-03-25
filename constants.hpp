#pragma once

#ifndef SUPPORT_WIDE_MODES
#define SUPPORT_WIDE_MODES 0
#endif

#if !SUPPORT_WIDE_MODES
// Support for normal modes, require <300MHz overclock, 
// work on pretty much any screen, up to 32 sprites.
constexpr int MAX_SPRITES = 32;
constexpr int MAX_FRAME_WIDTH = 720;
constexpr int MAX_FRAME_HEIGHT = 576;
constexpr int MAX_SPRITE_DATA_BYTES = 2048;
constexpr int MAX_SPRITE_WIDTH = 64;
constexpr int MAX_SPRITE_HEIGHT = 64;
constexpr int MAX_PATCHES_PER_LINE = 10;
constexpr int NUM_LINE_BUFFERS = 4;
constexpr int NUM_TMDS_BUFFERS = 8;
#else
// Support for modes up to 720p30, require extreme overclocks
// doesn't work on all screens.  Only 16 sprites
constexpr int MAX_SPRITES = 16;
constexpr int MAX_FRAME_WIDTH = 1280;
constexpr int MAX_FRAME_HEIGHT = 720;
constexpr int MAX_SPRITE_DATA_BYTES = 2048;
constexpr int MAX_SPRITE_WIDTH = 64;
constexpr int MAX_SPRITE_HEIGHT = 64;
constexpr int MAX_PATCHES_PER_LINE = 10;
constexpr int NUM_LINE_BUFFERS = 4;
constexpr int NUM_TMDS_BUFFERS = 7;
#endif