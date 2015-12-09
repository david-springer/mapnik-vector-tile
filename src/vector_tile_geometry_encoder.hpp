#ifndef __MAPNIK_VECTOR_TILE_GEOMETRY_ENCODER_H__
#define __MAPNIK_VECTOR_TILE_GEOMETRY_ENCODER_H__

// vector tile
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "vector_tile.pb.h"
#pragma GCC diagnostic pop

#include <mapnik/geometry.hpp>
#include "vector_tile_config.hpp"
#include <protozero/varint.hpp>

#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iostream>

namespace mapnik { namespace vector_tile_impl {

inline bool encode_geometry(mapnik::geometry::point<std::int64_t> const& pt,
                        vector_tile::Tile_Feature & current_feature,
                        int32_t & start_x,
                        int32_t & start_y)
{
    current_feature.add_geometry(9); // move_to | (1 << 3)
    int32_t dx = pt.x - start_x;
    int32_t dy = pt.y - start_y;
    // Manual zigzag encoding.
    current_feature.add_geometry(protozero::encode_zigzag32(dx));
    current_feature.add_geometry(protozero::encode_zigzag32(dy));
    start_x = pt.x;
    start_y = pt.y;
    return true;
}

template <typename Geometry>
inline std::size_t repeated_point_count(Geometry const& geom)
{
    if (geom.size() < 2)
    {
        return 0;
    }
    std::size_t count = 0;
    auto itr = geom.cbegin();

    for (auto prev_itr = itr++; itr != geom.end(); ++prev_itr, ++itr)
    {
        if (itr->x == prev_itr->x && itr->y == prev_itr->y)
        {
            count++;
        }
    }
    return count;
}

inline unsigned encode_length(unsigned len)
{
    return (len << 3u) | 2u;
}

inline bool encode_geometry(mapnik::geometry::line_string<std::int64_t> const& line,
                        vector_tile::Tile_Feature & current_feature,
                        int32_t & start_x,
                        int32_t & start_y)
{
    std::size_t line_size = line.size();
    line_size -= repeated_point_count(line);

    if (line_size < 2)
    {
        return false;
    }

    unsigned line_to_length = static_cast<unsigned>(line_size) - 1;

    auto pt = line.begin();
    current_feature.add_geometry(9); // move_to | (1 << 3)
    current_feature.add_geometry(protozero::encode_zigzag32(pt->x - start_x));
    current_feature.add_geometry(protozero::encode_zigzag32(pt->y - start_y));
    start_x = pt->x;
    start_y = pt->y;
    current_feature.add_geometry(encode_length(line_to_length));
    for (; pt != line.end(); ++pt)
    {
        int32_t dx = pt->x - start_x;
        int32_t dy = pt->y - start_y;
        if (dx == 0 && dy == 0)
        {
            continue;
        }
        current_feature.add_geometry(protozero::encode_zigzag32(dx));
        current_feature.add_geometry(protozero::encode_zigzag32(dy));
        start_x = pt->x;
        start_y = pt->y;
    }
    return true;
}

inline bool encode_geometry(mapnik::geometry::linear_ring<std::int64_t> const& ring,
                        vector_tile::Tile_Feature & current_feature,
                        int32_t & start_x,
                        int32_t & start_y)
{
    std::size_t ring_size = ring.size();
    ring_size -= repeated_point_count(ring);
    if (ring_size < 3)
    {
        return false;
    }
    auto last_itr = ring.end();
    if (ring.front() == ring.back())
    {
        --last_itr;
        --ring_size;
        if (ring_size < 3)
        {
            return false;
        }
    }

    unsigned line_to_length = static_cast<unsigned>(ring_size) - 1;
    auto pt = ring.begin();
    current_feature.add_geometry(9); // move_to | (1 << 3)
    current_feature.add_geometry(protozero::encode_zigzag32(pt->x - start_x));
    current_feature.add_geometry(protozero::encode_zigzag32(pt->y - start_y));
    start_x = pt->x;
    start_y = pt->y;
    current_feature.add_geometry(encode_length(line_to_length));
    for (; pt != last_itr; ++pt)
    {
        int32_t dx = pt->x - start_x;
        int32_t dy = pt->y - start_y;
        if (dx == 0 && dy == 0)
        {
            continue;
        }
        current_feature.add_geometry(protozero::encode_zigzag32(dx));
        current_feature.add_geometry(protozero::encode_zigzag32(dy));
        start_x = pt->x;
        start_y = pt->y;
    }
    current_feature.add_geometry(15); // close_path
    return true;
}

inline bool encode_geometry(mapnik::geometry::polygon<std::int64_t> const& poly,
                            vector_tile::Tile_Feature & current_feature,
                            int32_t & start_x,
                            int32_t & start_y)
{
    if (!encode_geometry(poly.exterior_ring, current_feature, start_x, start_y))
    {
        return false;
    }
    for (auto const& ring : poly.interior_rings)
    {
        encode_geometry(ring, current_feature, start_x, start_y);
    }
    return true;
}

inline bool encode_geometry(mapnik::geometry::multi_point<std::int64_t> const& geom,
                            vector_tile::Tile_Feature & current_feature,
                            int32_t & start_x,
                            int32_t & start_y)
{
    std::size_t geom_size = geom.size();
    if (geom_size <= 0)
    {
        return false;
    }
    current_feature.add_geometry(1u | (geom_size << 3)); // move_to | (len << 3)
    for (auto const& pt : geom)
    {
        int32_t dx = pt.x - start_x;
        int32_t dy = pt.y - start_y;
        // Manual zigzag encoding.
        current_feature.add_geometry(protozero::encode_zigzag32(dx));
        current_feature.add_geometry(protozero::encode_zigzag32(dy));
        start_x = pt.x;
        start_y = pt.y;
    }
    return true;
}

inline bool encode_geometry(mapnik::geometry::multi_line_string<std::int64_t> const& geom,
                            vector_tile::Tile_Feature & current_feature,
                            int32_t & start_x,
                            int32_t & start_y)
{
    bool success = false;
    for (auto const& poly : geom)
    {
        if (encode_geometry(poly, current_feature, start_x, start_y))
        {
            success = true;
        }
    }
    return success;
}

inline bool encode_geometry(mapnik::geometry::multi_polygon<std::int64_t> const& geom,
                            vector_tile::Tile_Feature & current_feature,
                            int32_t & start_x,
                            int32_t & start_y)
{
    bool success = false;
    for (auto const& poly : geom)
    {
        if (encode_geometry(poly, current_feature, start_x, start_y))
        {
            success = true;
        }
    }
    return success;
}

}} // end ns

#endif // __MAPNIK_VECTOR_TILE_GEOMETRY_ENCODER_H__
