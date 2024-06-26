#pragma once

#include "products/image_products.h"
#include "nlohmann/json.hpp"
#include "common/calibration.h"
#include "common/projection/thinplatespline.h"

#include "logger.h"

namespace lrit
{

    class GenericxRITCalibrator : public satdump::ImageProducts::CalibratorBase
    {
    private:
        nlohmann::json calib_cfg;
        std::vector<double> wavenumbers;
        std::vector<satdump::ImageProducts::calib_type_t> calib_type_map;
        std::vector<int> new_max_val;
        int product_bit_depth;

        std::vector<std::pair<std::shared_ptr<satdump::projection::VizGeorefSpline2D>, std::unordered_map<int, float>>> lut_for_channels;

    public:
        GenericxRITCalibrator(nlohmann::json calib, satdump::ImageProducts *products) : satdump::ImageProducts::CalibratorBase(calib, products)
        {
            calib_cfg = calib;
            wavenumbers = calib["wavenumbers"].get<std::vector<double>>();
            for (size_t i = 0; i < d_products->images.size(); i++)
            {
                calib_type_map.push_back(d_products->get_calibration_type(i));

                product_bit_depth = pow(2, d_products->bit_depth) - 1;
                if (!calib_cfg["bits_for_calib"].is_null())
                    new_max_val.push_back(pow(2, calib_cfg["bits_for_calib"].get<int>()) - 1);
                else
                    new_max_val.push_back(product_bit_depth);
            }

            for (size_t i = 0; i < d_products->images.size(); i++)
            {
                try
                {
                    if (calib_cfg.contains("to_complete") && calib_cfg["to_complete"].get<bool>())
                    {
                        std::vector<std::pair<int, float>> llut = calib_cfg[d_products->images[i].channel_name];
                        std::shared_ptr<satdump::projection::VizGeorefSpline2D> spline = std::make_shared<satdump::projection::VizGeorefSpline2D>(1);
                        bool is_first = true;
                        for (auto &v : llut)
                        {
                            if (v.second != 0 || is_first)
                            {
                                double rp_v[1] = {v.second};
                                spline->add_point(v.first, v.first, rp_v);
                                is_first = false;
                                logger->info("Point %d %f", v.first, v.second);
                            }
                        }

                        int s = spline->solve();
                        if (s != 3)
                            lut_for_channels.push_back({nullptr, std::unordered_map<int, float>()});
                        else
                            lut_for_channels.push_back({spline, std::unordered_map<int, float>()});
                    }
                    else
                    {
                        std::vector<std::pair<int, float>> llut = calib_cfg[d_products->images[i].channel_name];
                        std::unordered_map<int, float> flut;
                        for (auto &v : llut)
                            flut.emplace(v.first, v.second);
                        lut_for_channels.push_back({nullptr, flut});
                    }
                }
                catch (std::exception &)
                {
                    lut_for_channels.push_back({nullptr, std::unordered_map<int, float>()});
                }
            }
        }

        void init()
        {
        }

        double compute(int channel, int /*pos_x*/, int /*pos_y*/, int px_val)
        {
            if (new_max_val[channel] != product_bit_depth)
                px_val = double(px_val / (double)product_bit_depth) * new_max_val[channel];

            try
            {
                if (calib_type_map[channel] == d_products->CALIB_REFLECTANCE)
                {
                    if (px_val == product_bit_depth)
                        return CALIBRATION_INVALID_VALUE;
                    else if (lut_for_channels[channel].second.count(px_val))
                        return lut_for_channels[channel].second[px_val];
                    else if (lut_for_channels[channel].first)
                    {
                        double point = 0;
                        lut_for_channels[channel].first->get_point(px_val, px_val, &point);
                        return point;
                    }
                    else
                        return CALIBRATION_INVALID_VALUE;
                }
                else if (calib_type_map[channel] == d_products->CALIB_RADIANCE)
                {
                    if (px_val == 0)
                        return CALIBRATION_INVALID_VALUE;
                    else if (lut_for_channels[channel].second.count(px_val))
                        return temperature_to_radiance(lut_for_channels[channel].second[px_val], wavenumbers[channel]);
                    else if (lut_for_channels[channel].first)
                    {
                        double point = 0;
                        lut_for_channels[channel].first->get_point(px_val, px_val, &point);
                        return temperature_to_radiance(point, wavenumbers[channel]);
                    }
                    else
                        return CALIBRATION_INVALID_VALUE;
                }
                else
                    return CALIBRATION_INVALID_VALUE;
            }
            catch (std::exception &)
            {
                return CALIBRATION_INVALID_VALUE;
            }
        }
    };
}