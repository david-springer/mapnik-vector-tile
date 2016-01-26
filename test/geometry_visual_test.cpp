// catch
#include "catch.hpp"

// mapnik-vector-tile
#include "vector_tile_strategy.hpp"
#include "vector_tile_processor.hpp"
#include "vector_tile_geometry_decoder.hpp"

// mapnik
#include <mapnik/geometry_is_simple.hpp>
#include <mapnik/geometry_is_valid.hpp>
#include <mapnik/geometry_strategy.hpp>
#include <mapnik/geometry_transform.hpp>
#include <mapnik/json/geometry_parser.hpp>
#include <mapnik/save_map.hpp>
#include <mapnik/util/file_io.hpp>
#include <mapnik/util/fs.hpp>
#include <mapnik/util/geometry_to_geojson.hpp>
#include <mapnik/view_strategy.hpp>

// boost
#include <boost/filesystem/operations.hpp>

// test utils
#include "encoding_util.hpp"
#include "test_utils.hpp"
#include "geometry_equal.hpp"

// std
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
//#include <functional>

// protozero
#include <protozero/pbf_reader.hpp>

void clip_geometry(mapnik::Map const& map,
                   std::string const& file,
                   mapnik::box2d<double> const& bbox,
                   int simplify_distance,
                   bool strictly_simple,
                   mapnik::vector_tile_impl::polygon_fill_type fill_type,
                   bool mpu,
                   bool process_all)
{
    unsigned tile_size = 4096;
    int buffer_size = 0;
    std::string geojson_string;
    
    mapnik::vector_tile_impl::processor ren(map);
    // TODO - test these booleans https://github.com/mapbox/mapnik-vector-tile/issues/165
    ren.set_strictly_simple(strictly_simple);
    ren.set_simplify_distance(simplify_distance);
    ren.set_fill_type(fill_type);
    ren.set_process_all_rings(process_all);
    ren.set_multi_polygon_union(mpu);
    
    mapnik::vector_tile_impl::tile out_tile = ren.create_tile(bbox, tile_size, buffer_size);
    vector_tile::Tile tile = out_tile.get_tile();
    mapnik::geometry::geometry<double> geom4326;
    mapnik::geometry::geometry<double> geom4326_pbf;
    
    std::string buffer;
    out_tile.serialize_to_string(buffer);
    //std::string buffer2 = tile.SerializePartialAsString();
    //CHECK(buffer2 == buffer);
    if (!buffer.empty() && tile.layers_size() > 0)
    {
        protozero::pbf_reader tile_reader(buffer);
        if(!tile_reader.next(3))
        {
            throw std::runtime_error("tile buffer contains no layer!");
        }
        protozero::pbf_reader layer_reader = tile_reader.get_message();
        vector_tile::Tile_Layer const& layer = tile.layers(0);
        if (layer.features_size() > 0)
        {
            if (!layer_reader.next(2))
            {
                throw std::runtime_error("layer buffer contains no features!");
            }
            protozero::pbf_reader feature_reader = layer_reader.get_message();
            int32_t geometry_type = 0; // vector_tile::Tile_GeomType_UNKNOWN
            std::pair<protozero::pbf_reader::const_uint32_iterator, protozero::pbf_reader::const_uint32_iterator> geom_itr;
            while (feature_reader.next())
            {
                if (feature_reader.tag() == 4)
                {
                    geom_itr = feature_reader.get_packed_uint32();
                }
                else if (feature_reader.tag() == 3)
                {
                    geometry_type = feature_reader.get_enum();
                }
            }
            vector_tile::Tile_Feature const& f = layer.features(0);
            double sx = static_cast<double>(tile_size) / bbox.width();
            double sy = static_cast<double>(tile_size) / bbox.height();
            double i_x = bbox.minx();
            double i_y = bbox.maxy();
            mapnik::vector_tile_impl::Geometry<double> geoms(f, i_x, i_y, 1.0 * sx, -1.0 * sy);
            geom4326 = mapnik::vector_tile_impl::decode_geometry(geoms, f.type(), 2);
            //mapnik::vector_tile_impl::Geometry<double> geoms2(f, 0.0, 0.0, 1.0, 1.0);
            //geom4326 = std::move(mapnik::vector_tile_impl::decode_geometry(geoms2, f.type(), 2));
            
            mapnik::vector_tile_impl::GeometryPBF<double> geoms2_pbf(geom_itr, i_x, i_y, 1.0 * sx, -1.0 * sy);
            geom4326_pbf = mapnik::vector_tile_impl::decode_geometry(geoms2_pbf, geometry_type, 2);
            //mapnik::vector_tile_impl::GeometryPBF<double> geoms2_pbf(geom_itr, 0.0, 0.0, 1.0, 1.0);
            //geom4326_pbf = std::move(mapnik::vector_tile_impl::decode_geometry(geoms2_pbf, geometry_type, 2));
            
            assert_g_equal(geom4326, geom4326_pbf);
            std::string reason;
            std::string is_valid = "false";
            std::string is_simple = "false";
            if (mapnik::geometry::is_valid(geom4326, reason))
            {
                is_valid = "true";
            }
            if (mapnik::geometry::is_simple(geom4326))
            {
                is_simple = "true";
            }
            unsigned int n_err = 0;
            mapnik::util::to_geojson(geojson_string,geom4326);

            geojson_string = geojson_string.substr(0, geojson_string.size()-1);
            geojson_string += ",\"properties\":{\"is_valid\":"+is_valid+", \"is_simple\":"+is_simple+", \"message\":\""+reason+"\"}}";
        }
        else
        {
            geojson_string = "{\"type\": \"Point\", \"coordinates\":[], \"properties\":{\"message\":\"Tile layer had no features\"}}";
        }
    }
    else
    {
        geojson_string = "{\"type\": \"Point\", \"coordinates\":[], \"properties\":{\"message\":\"Tile had no layers\"}}";
    }

    std::string fixture_name = mapnik::util::basename(file);
    fixture_name = fixture_name.substr(0, fixture_name.size()-5);
    if (!mapnik::util::exists("./test/geometry-test-data/output"))
    {
        boost::filesystem::create_directory(("./test/geometry-test-data/output"));
    }
    if (!mapnik::util::exists("./test/geometry-test-data/output/"+fixture_name))
    {
        boost::filesystem::create_directory(("./test/geometry-test-data/output/"+fixture_name));
    }

    std::stringstream file_stream;
    file_stream << "./test/geometry-test-data/output/" << fixture_name << "/"
        << bbox.minx() << ","
        << bbox.miny() << ","
        << bbox.maxx()<< ","
        << bbox.maxy() << ","
        << "simplify_distance=" << simplify_distance << ","
        << "strictly_simple=" << strictly_simple << ","
        << "fill_type=" << fill_type << ","
        << "mpu=" << mpu << ","
        << "par=" << process_all << ".geojson";
    std::string file_path = file_stream.str();
    if (!mapnik::util::exists(file_path) || (std::getenv("UPDATE") != nullptr))
    {
        std::ofstream out(file_path);
        out << geojson_string;
    }
    else
    {
        mapnik::util::file input(file_path);
        if (!input.open())
        {
            throw std::runtime_error("failed to open test geojson");
        }
        std::string expected_string(input.data().get(), input.size());
        mapnik::geometry::geometry<double> geom_expected;
        CHECK(expected_string == geojson_string);
        if (mapnik::json::from_geojson(expected_string, geom_expected))
        {
            assert_g_equal(geom_expected, geom4326);
        }
    }
}

mapnik::box2d<double> middle_fifty(mapnik::box2d<double> const& bbox)
{
    double width = std::fabs(bbox.maxx() - bbox.minx());
    double height = std::fabs(bbox.maxy() - bbox.miny());
    mapnik::box2d<double> new_bbox(
        bbox.minx() + width*0.25,
        bbox.miny() + height*0.25,
        bbox.maxx() - width*0.25,
        bbox.maxy() - height*0.25
    );
    return new_bbox;
}

mapnik::box2d<double> top_left(mapnik::box2d<double> const& bbox)
{
    double width = std::fabs(bbox.maxx() - bbox.minx());
    double height = std::fabs(bbox.maxy() - bbox.miny());
    mapnik::box2d<double> new_bbox(
        bbox.minx(),
        bbox.miny(),
        bbox.maxx() - width*0.5,
        bbox.maxy() - height*0.5
    );
    return new_bbox;
}

mapnik::box2d<double> top_right(mapnik::box2d<double> const& bbox)
{
    double width = std::fabs(bbox.maxx() - bbox.minx());
    double height = std::fabs(bbox.maxy() - bbox.miny());
    mapnik::box2d<double> new_bbox(
        bbox.minx() + width*0.5,
        bbox.miny(),
        bbox.maxx(),
        bbox.maxy() - height*0.5
    );
    return new_bbox;
}

mapnik::box2d<double> bottom_left(mapnik::box2d<double> const& bbox)
{
    double width = std::fabs(bbox.maxx() - bbox.minx());
    double height = std::fabs(bbox.maxy() - bbox.miny());
    mapnik::box2d<double> new_bbox(
        bbox.minx(),
        bbox.miny() + height*0.5,
        bbox.maxx() + width*0.5,
        bbox.maxy()
    );
    return new_bbox;
}

mapnik::box2d<double> bottom_right(mapnik::box2d<double> const& bbox)
{
    double width = std::fabs(bbox.maxx() - bbox.minx());
    double height = std::fabs(bbox.maxy() - bbox.miny());
    mapnik::box2d<double> new_bbox(
        bbox.minx() + width*0.5,
        bbox.miny() + height*0.5,
        bbox.maxx(),
        bbox.maxy()
    );
    return new_bbox;
}

mapnik::box2d<double> zoomed_out(mapnik::box2d<double> const& bbox)
{
    double width = std::fabs(bbox.maxx() - bbox.minx());
    double height = std::fabs(bbox.maxy() - bbox.miny());
    mapnik::box2d<double> new_bbox(
        bbox.minx() - width,
        bbox.miny() - height,
        bbox.maxx() + width,
        bbox.maxy() + height
    );
    return new_bbox;
}

TEST_CASE("geometries visual tests")
{
    std::vector<std::string> geometries = mapnik::util::list_directory("./test/geometry-test-data/input");
    std::vector<std::string> benchmarks = mapnik::util::list_directory("./test/geometry-test-data/benchmark");
    if (std::getenv("BENCHMARK") != nullptr)
    {
        geometries.insert(geometries.end(), benchmarks.begin(), benchmarks.end());
    }
    for (std::string const& file: geometries)
    {
        mapnik::datasource_ptr ds = testing::build_geojson_fs_ds(file);
        mapnik::box2d<double> bbox = ds->envelope();
        mapnik::Map map(256, 256, "+init=epsg:4326");
        mapnik::layer lyr("layer","+init=epsg:4326");
        lyr.set_datasource(ds);
        map.add_layer(lyr);
        for (int simplification_distance : std::vector<int>({0, 4, 8}))
        {
            for (bool strictly_simple : std::vector<bool>({false, true}))
            {
                std::vector<mapnik::vector_tile_impl::polygon_fill_type> types;
                types.emplace_back(mapnik::vector_tile_impl::even_odd_fill); 
                types.emplace_back(mapnik::vector_tile_impl::non_zero_fill);
                types.emplace_back(mapnik::vector_tile_impl::positive_fill); 
                types.emplace_back(mapnik::vector_tile_impl::negative_fill);
                for (auto const& type : types)
                {
                    for (bool mpu : std::vector<bool>({false, true}))
                    {
                        for (bool process_all : std::vector<bool>({false, true}))
                        {
                            clip_geometry(map, file, bbox, simplification_distance, strictly_simple, type, mpu, process_all);
                            clip_geometry(map, file, middle_fifty(bbox), simplification_distance, strictly_simple, type, mpu, process_all);
                            clip_geometry(map, file, top_left(bbox), simplification_distance, strictly_simple, type, mpu, process_all);
                            clip_geometry(map, file, top_right(bbox), simplification_distance, strictly_simple, type, mpu, process_all);
                            clip_geometry(map, file, bottom_left(bbox), simplification_distance, strictly_simple, type, mpu, process_all);
                            clip_geometry(map, file, bottom_right(bbox), simplification_distance, strictly_simple, type, mpu, process_all);
                            clip_geometry(map, file, zoomed_out(bbox), simplification_distance, strictly_simple, type, mpu, process_all);
                        }
                    }
                }
            }
        }
    }
}
