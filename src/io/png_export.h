#pragma once
#include <string>

// Write the RGBA pixel buffer (OpenGL bottom-up layout) to a PNG file.
// Flips rows to produce a top-down image.
bool png_export_pixels(const std::string& path, int w, int h, const unsigned char* rgba);
