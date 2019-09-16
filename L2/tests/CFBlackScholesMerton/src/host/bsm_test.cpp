/*
 * Copyright 2019 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
* @file bsm_test.cpp
* @brief Testbench to generate randomized input data and launch on kernel.
* Results are compared to a full precision model.
*/

#include <stdio.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "bsm_model.hpp"
#include "xcl2.hpp"

/// @def Controls the data type used in the kernel
#define KERNEL_DT float

// Temporary copy of this macro definition until new xcl2.hpp is used
#define OCL_CHECK(error, call)                                                                   \
    call;                                                                                        \
    if (error != CL_SUCCESS) {                                                                   \
        printf("%s:%d Error calling " #call ", error code is: %d\n", __FILE__, __LINE__, error); \
        exit(EXIT_FAILURE);                                                                      \
    }

/// @brief Main entry point to test
///
/// This is a command-line application to test the kernel.  It supports software
/// and hardware emulation as well as
/// running on an Alveo target.
///
/// Usage: ./bsm_test ./xclbin/<kernel_name> <number of prices>
///
/// @param[in] argc Standard C++ argument count
/// @param[in] argv Standard C++ input arguments
int main(int argc, char* argv[]) {
    std::cout << std::endl << std::endl;
    std::cout << "*************" << std::endl;
    std::cout << "BSM Demo v1.0" << std::endl;
    std::cout << "*************" << std::endl;
    std::cout << std::endl;

    // Test parameters
    static const unsigned int call = 1;

    unsigned int argIdx = 1;
    std::string xclbin_file(argv[argIdx++]);
    unsigned int num = std::atoi(argv[argIdx++]);

    // Vectors for parameter storage.  These use an aligned allocator in order
    // to avoid an additional copy of the host memory into the device
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > s(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > v(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > r(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > t(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > k(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > q(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > price(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > delta(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > gamma(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > vega(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > theta(num);
    std::vector<KERNEL_DT, aligned_allocator<KERNEL_DT> > rho(num);

    // Host results (always double precision)
    double* host_price = new double[num];
    double* host_delta = new double[num];
    double* host_gamma = new double[num];
    double* host_vega = new double[num];
    double* host_theta = new double[num];
    double* host_rho = new double[num];

    // Generate randomized data and reference price & greeks
    std::cout << "Generating randomized data and reference results..." << std::endl;
    for (unsigned int i = 0; i < num; i++) {
        double s_temp = random_range(10, 200);
        double v_temp = random_range(0.1, 1.0);
        double r_temp = random_range(0.001, 0.2);
        double t_temp = random_range(0.5, 3);
        double k_temp = random_range(10, 200);
        double q_temp = random_range(0.0, 0.2);

        s[i] = s_temp;
        v[i] = v_temp;
        r[i] = r_temp;
        t[i] = t_temp;
        k[i] = k_temp;
        q[i] = q_temp;

        bsm_model(s_temp, v_temp, r_temp, t_temp, k_temp, q_temp, call, host_price[i], host_delta[i], host_gamma[i],
                  host_vega[i], host_theta[i], host_rho[i]);
    }

    // OPENCL HOST CODE AREA START
    // get_xil_devices() is a utility API which will find the xilinx
    // platforms and will return list of devices connected to Xilinx platform
    std::cout << "Connecting to device and loading kernel..." << std::endl;
    std::vector<cl::Device> devices = xcl::get_xil_devices();
    cl::Device device = devices[0];
    cl_int err;

    OCL_CHECK(err, cl::Context context(device, NULL, NULL, NULL, &err));
    OCL_CHECK(err, cl::CommandQueue cq(context, device, CL_QUEUE_PROFILING_ENABLE, &err));

    // Load the binary file (using function from xcl2.cpp)
    cl::Program::Binaries bins = xcl::import_binary_file(xclbin_file);

    devices.resize(1);
    OCL_CHECK(err, cl::Program program(context, devices, bins, NULL, &err));
    OCL_CHECK(err, cl::Kernel krnl_cfBSMEngine(program, "bsm_kernel", &err));

    // Allocate Buffer in Global Memory
    // Buffers are allocated using CL_MEM_USE_HOST_PTR for efficient memory and
    // Device-to-host communication
    std::cout << "Allocating buffers..." << std::endl;
    OCL_CHECK(err, cl::Buffer buffer_s(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, num * sizeof(KERNEL_DT),
                                       s.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_v(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, num * sizeof(KERNEL_DT),
                                       v.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_r(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, num * sizeof(KERNEL_DT),
                                       r.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_t(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, num * sizeof(KERNEL_DT),
                                       t.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_k(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, num * sizeof(KERNEL_DT),
                                       k.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_q(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, num * sizeof(KERNEL_DT),
                                       q.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_price(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, num * sizeof(KERNEL_DT),
                                           price.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_delta(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, num * sizeof(KERNEL_DT),
                                           delta.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_gamma(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, num * sizeof(KERNEL_DT),
                                           gamma.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_vega(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, num * sizeof(KERNEL_DT),
                                          vega.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_theta(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, num * sizeof(KERNEL_DT),
                                           theta.data(), &err));
    OCL_CHECK(err, cl::Buffer buffer_rho(context, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, num * sizeof(KERNEL_DT),
                                         rho.data(), &err));

    // Set the arguments
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(0, buffer_s));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(1, buffer_v));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(2, buffer_r));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(3, buffer_t));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(4, buffer_k));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(5, buffer_q));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(6, call));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(7, num));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(8, buffer_price));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(9, buffer_delta));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(10, buffer_gamma));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(11, buffer_vega));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(12, buffer_theta));
    OCL_CHECK(err, err = krnl_cfBSMEngine.setArg(13, buffer_rho));

    // Copy input data to device global memory
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_s}, 0));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_v}, 0));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_r}, 0));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_t}, 0));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_k}, 0));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_q}, 0));

    // Launch the Kernel
    std::cout << "Launching kernel..." << std::endl;
    uint64_t nstimestart, nstimeend;
    cl::Event event;
    OCL_CHECK(err, err = cq.enqueueTask(krnl_cfBSMEngine, NULL, &event));
    OCL_CHECK(err, err = cq.finish());
    OCL_CHECK(err, err = event.getProfilingInfo<uint64_t>(CL_PROFILING_COMMAND_START, &nstimestart));
    OCL_CHECK(err, err = event.getProfilingInfo<uint64_t>(CL_PROFILING_COMMAND_END, &nstimeend));
    auto duration_nanosec = nstimeend - nstimestart;
    std::cout << "  Duration returned by profile API is " << (duration_nanosec * (1.0e-6)) << " ms **** " << std::endl;

    // Copy Result from Device Global Memory to Host Local Memory
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_price}, CL_MIGRATE_MEM_OBJECT_HOST));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_delta}, CL_MIGRATE_MEM_OBJECT_HOST));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_gamma}, CL_MIGRATE_MEM_OBJECT_HOST));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_vega}, CL_MIGRATE_MEM_OBJECT_HOST));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_theta}, CL_MIGRATE_MEM_OBJECT_HOST));
    OCL_CHECK(err, err = cq.enqueueMigrateMemObjects({buffer_rho}, CL_MIGRATE_MEM_OBJECT_HOST));
    cq.finish();
    // OPENCL HOST CODE AREA END

    // Check results
    double max_price_diff = 0.0f;
    double max_delta_diff = 0.0f;
    double max_gamma_diff = 0.0f;
    double max_vega_diff = 0.0f;
    double max_theta_diff = 0.0f;
    double max_rho_diff = 0.0f;

    for (unsigned int i = 0; i < num; i++) {
        double temp = 0.0f;
        // std::cout << price[i] << " " << host_price[i] << "     diff = " <<
        // price[i] - host_price[i] << std::endl;
        if (std::abs(temp = (price[i] - host_price[i])) > std::abs(max_price_diff)) max_price_diff = temp;
        if (std::abs(temp = (delta[i] - host_delta[i])) > std::abs(max_delta_diff)) max_delta_diff = temp;
        if (std::abs(temp = (gamma[i] - host_gamma[i])) > std::abs(max_gamma_diff)) max_gamma_diff = temp;
        if (std::abs(temp = (vega[i] - host_vega[i])) > std::abs(max_vega_diff)) max_vega_diff = temp;
        if (std::abs(temp = (theta[i] - host_theta[i])) > std::abs(max_theta_diff)) max_theta_diff = temp;
        if (std::abs(temp = (rho[i] - host_rho[i])) > std::abs(max_rho_diff)) max_rho_diff = temp;
    }

    std::cout << "Kernel done!" << std::endl;
    std::cout << "Comparing results..." << std::endl;
    std::cout << "Processed " << num;
    if (call) {
        std::cout << " call options:" << std::endl;
    } else {
        std::cout << " put options:" << std::endl;
    }
    std::cout << "Throughput = " << (1.0 * num) / (duration_nanosec * 1.0e-9) / 1.0e6 << " Mega options/sec"
              << std::endl;

    std::cout << std::endl;
    std::cout << "  Largest host-kernel price difference = " << max_price_diff << std::endl;
    std::cout << "  Largest host-kernel delta difference = " << max_delta_diff << std::endl;
    std::cout << "  Largest host-kernel gamma difference = " << max_gamma_diff << std::endl;
    std::cout << "  Largest host-kernel vega difference  = " << max_vega_diff << std::endl;
    std::cout << "  Largest host-kernel theta difference = " << max_theta_diff << std::endl;
    std::cout << "  Largest host-kernel rho difference   = " << max_rho_diff << std::endl;

    return 0;
}
