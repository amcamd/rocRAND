// Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>
#include <utility>
#include <algorithm>

#include <boost/program_options.hpp>

#include <hip/hip_runtime.h>
#include <rocrand.h>

#define HIP_CHECK(condition)         \
  {                                  \
    hipError_t error = condition;    \
    if(error != hipSuccess){         \
        std::cout << "HIP error: " << error << " line: " << __LINE__ << std::endl; \
        exit(error); \
    } \
  }

#define ROCRAND_CHECK(condition)                 \
  {                                              \
    rocrand_status _status = condition;           \
    if(_status != ROCRAND_STATUS_SUCCESS) {       \
        std::cout << "ROCRAND error: " << _status << " line: " << __LINE__ << std::endl; \
        exit(_status); \
    } \
  }

#ifndef DEFAULT_RAND_N
const size_t DEFAULT_RAND_N = 1024 * 1024 * 128;
#endif

typedef rocrand_rng_type rng_type_t;

template<typename T>
using generate_func_type = std::function<rocrand_status(rocrand_generator, T *, size_t)>;

template<typename T>
void run_benchmark(const boost::program_options::variables_map& vm,
                   const rng_type_t rng_type,
                   generate_func_type<T> generate_func)
{
    const size_t size = vm["size"].as<size_t>();
    const size_t trials = vm["trials"].as<size_t>();

    T * data;
    HIP_CHECK(hipMalloc((void **)&data, size * sizeof(T)));

    rocrand_generator generator;
    ROCRAND_CHECK(rocrand_create_generator(&generator, rng_type));

    const size_t dimensions = vm["dimensions"].as<size_t>();
    rocrand_status status = rocrand_set_quasi_random_generator_dimensions(generator, dimensions);
    if (status != ROCRAND_STATUS_TYPE_ERROR) // If the RNG is not quasi-random
    {
        ROCRAND_CHECK(status);
    }

    // Warm-up
    for (size_t i = 0; i < 5; i++)
    {
        ROCRAND_CHECK(generate_func(generator, data, size));
    }
    HIP_CHECK(hipDeviceSynchronize());

    // Measurement
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < trials; i++)
    {
        ROCRAND_CHECK(generate_func(generator, data, size));
    }
    HIP_CHECK(hipDeviceSynchronize());
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    std::cout << std::fixed << std::setprecision(3)
              << "      "
              << "Throughput = "
              << std::setw(8) << (trials * size * sizeof(T)) /
                    (elapsed.count() / 1e3 * (1 << 30))
              << " GB/s, Samples = "
              << std::setw(8) << (trials * size) /
                    (elapsed.count() / 1e3 * (1 << 30))
              << " GSample/s, AvgTime (1 trial) = "
              << std::setw(8) << elapsed.count() / trials
              << " ms, Time (all) = "
              << std::setw(8) << elapsed.count()
              << " ms, Size = " << size
              << std::endl;

    ROCRAND_CHECK(rocrand_destroy_generator(generator));
    HIP_CHECK(hipFree(data));
}

void run_benchmarks(const boost::program_options::variables_map& vm,
                    const rng_type_t rng_type,
                    const std::string& distribution)
{
    if (distribution == "uniform-uint")
    {
        run_benchmark<unsigned int>(vm, rng_type,
            [](rocrand_generator gen, unsigned int * data, size_t size) {
                return rocrand_generate(gen, data, size);
            }
        );
    }
    if (distribution == "uniform-float")
    {
        run_benchmark<float>(vm, rng_type,
            [](rocrand_generator gen, float * data, size_t size) {
                return rocrand_generate_uniform(gen, data, size);
            }
        );
    }
    if (distribution == "uniform-double")
    {
        run_benchmark<double>(vm, rng_type,
            [](rocrand_generator gen, double * data, size_t size) {
                return rocrand_generate_uniform_double(gen, data, size);
            }
        );
    }
    if (distribution == "normal-float")
    {
        run_benchmark<float>(vm, rng_type,
            [](rocrand_generator gen, float * data, size_t size) {
                return rocrand_generate_normal(gen, data, size, 0.0f, 1.0f);
            }
        );
    }
    if (distribution == "normal-double")
    {
        run_benchmark<double>(vm, rng_type,
            [](rocrand_generator gen, double * data, size_t size) {
                return rocrand_generate_normal_double(gen, data, size, 0.0, 1.0);
            }
        );
    }
    if (distribution == "log-normal-float")
    {
        run_benchmark<float>(vm, rng_type,
            [](rocrand_generator gen, float * data, size_t size) {
                return rocrand_generate_log_normal(gen, data, size, 0.0f, 1.0f);
            }
        );
    }
    if (distribution == "log-normal-double")
    {
        run_benchmark<double>(vm, rng_type,
            [](rocrand_generator gen, double * data, size_t size) {
                return rocrand_generate_log_normal_double(gen, data, size, 0.0, 1.0);
            }
        );
    }
    if (distribution == "poisson")
    {
        const auto lambdas = vm["lambda"].as<std::vector<double>>();
        for (double lambda : lambdas)
        {
            std::cout << "    " << "lambda "
                 << std::fixed << std::setprecision(1) << lambda << std::endl;
            run_benchmark<unsigned int>(vm, rng_type,
                [lambda](rocrand_generator gen, unsigned int * data, size_t size) {
                    return rocrand_generate_poisson(gen, data, size, lambda);
                }
            );
        }
    }
}

const std::vector<std::string> all_engines = {
    "xorwow",
    "mrg32k3a",
    // "mtgp32",
    "philox",
    "sobol32",
};

const std::vector<std::string> all_distributions = {
    "uniform-uint",
    // "uniform-long-long",
    "uniform-float",
    "uniform-double",
    "normal-float",
    "normal-double",
    "log-normal-float",
    "log-normal-double",
    "poisson"
};

int main(int argc, char *argv[])
{
    namespace po = boost::program_options;
    po::options_description options("options");

    const std::string distribution_desc =
        "space-separated list of distributions:" +
        std::accumulate(all_distributions.begin(), all_distributions.end(), std::string(),
            [](std::string a, std::string b) {
                return a + "\n   " + b;
            }
        ) +
        "\nor all";
    const std::string engine_desc =
        "space-separated list of random number engines:" +
        std::accumulate(all_engines.begin(), all_engines.end(), std::string(),
            [](std::string a, std::string b) {
                return a + "\n   " + b;
            }
        ) +
        "\nor all";
    options.add_options()
        ("help", "show usage instructions")
        ("size", po::value<size_t>()->default_value(DEFAULT_RAND_N), "number of values")
        ("dimensions", po::value<size_t>()->default_value(1), "number of dimensions of quasi-random values")
        ("trials", po::value<size_t>()->default_value(20), "number of trials")
        ("dis", po::value<std::vector<std::string>>()->multitoken()->default_value({ "uniform-uint" }, "uniform-uint"),
            distribution_desc.c_str())
        ("engine", po::value<std::vector<std::string>>()->multitoken()->default_value({ "philox" }, "philox"),
            engine_desc.c_str())
        ("lambda", po::value<std::vector<double>>()->multitoken()->default_value({ 100.0 }, "100.0"),
            "space-separated list of lambdas of Poisson distribution")
    ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, options), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << options << std::endl;
        return 0;
    }

    std::vector<std::string> engines;
    {
        auto es = vm["engine"].as<std::vector<std::string>>();
        if (std::find(es.begin(), es.end(), "all") != es.end())
        {
            engines = all_engines;
        }
        else
        {
            for (auto e : all_engines)
            {
                if (std::find(es.begin(), es.end(), e) != es.end())
                    engines.push_back(e);
            }
        }
    }

    std::vector<std::string> distributions;
    {
        auto ds = vm["dis"].as<std::vector<std::string>>();
        if (std::find(ds.begin(), ds.end(), "all") != ds.end())
        {
            distributions = all_distributions;
        }
        else
        {
            for (auto d : all_distributions)
            {
                if (std::find(ds.begin(), ds.end(), d) != ds.end())
                    distributions.push_back(d);
            }
        }
    }

    std::cout << "rocRAND:" << std::endl << std::endl;
    for (auto engine : engines)
    {
        std::cout << engine << ":" << std::endl;
        rng_type_t rng_type;
        if (engine == "xorwow")
            rng_type = ROCRAND_RNG_PSEUDO_XORWOW;
        else if (engine == "mrg32k3a")
            rng_type = ROCRAND_RNG_PSEUDO_MRG32K3A;
        else if (engine == "philox")
            rng_type = ROCRAND_RNG_PSEUDO_PHILOX4_32_10;
        else if (engine == "sobol32")
            rng_type = ROCRAND_RNG_QUASI_SOBOL32;

        for (auto distribution : distributions)
        {
            std::cout << "  " << distribution << ":" << std::endl;
            run_benchmarks(vm, rng_type, distribution);
        }
        std::cout << std::endl;
    }

    return 0;
}
