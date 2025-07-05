/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2025                                                               *
 * For conditions of distribution and use, see copyright notice in LICENSE.md            *
 ****************************************************************************************/

#ifndef __SGCT__FMT__H__
#define __SGCT__FMT__H__

#ifndef __APPLE__

#include <filesystem>
#include <fmt/core.h>

template <>
struct fmt::formatter<std::filesystem::path> {
    constexpr auto parse(fmt::format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const std::filesystem::path& path, fmt::format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", path.string());
    }
};

#endif // not APPLE

#endif // __SGCT__FMT__H__
