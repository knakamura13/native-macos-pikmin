#pragma once

inline const char* pc_build_window_title(const char* fallback, const char* configured)
{
    return configured && *configured ? configured : fallback;
}
