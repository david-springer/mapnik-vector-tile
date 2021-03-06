// test utils
#include "decoding_util.hpp"

// mapnik-vector-tile
#include "vector_tile_geometry_decoder.hpp"

template <typename T>
mapnik::vector_tile_impl::GeometryPBF<T> feature_to_pbf_geometry(std::string const& feature_string, double scale_x, double scale_y)
{
    protozero::pbf_reader feature_pbf(feature_string);
    feature_pbf.next(4);
    return mapnik::vector_tile_impl::GeometryPBF<T>(feature_pbf.get_packed_uint32(),0.0,0.0,scale_x,scale_y);
}

template mapnik::vector_tile_impl::GeometryPBF<double> feature_to_pbf_geometry<double>(std::string const& feature_string, double scale_x, double scale_y);
template mapnik::vector_tile_impl::GeometryPBF<std::int64_t> feature_to_pbf_geometry<std::int64_t>(std::string const& feature_string, double scale_x, double scale_y);
