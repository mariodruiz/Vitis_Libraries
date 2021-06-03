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
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <chrono>

// This file is required for OpenCL C++ wrapper APIs
#include "cgInstr.hpp"
#include "cgSolverKernel.hpp"
#include "utils.hpp"
#include "binFiles.hpp"

using namespace std;

int main(int argc, char** argv) {
    if (argc < 6 || argc > 7) {
        cout << "Usage: " << argv[0]
             << " <XCLBIN File> <Max Iteration> <Tolerence> <Matrix Dim> <Matrix Path> [device id]" << endl;
        return EXIT_FAILURE;
    }

    uint32_t l_index = 1;

    string binaryFile = argv[l_index++];

    int l_maxIter = atoi(argv[l_index++]);
    CG_dataType l_tol = atof(argv[l_index++]);
    int vecSize = atoi(argv[l_index++]);
    int matrixSize = vecSize * vecSize;
    int instrSize = CG_instrBytes * (1 + l_maxIter);

    // I/O Data Vectors
    host_buffer_t<uint8_t> h_instr(instrSize);
    host_buffer_t<CG_dataType> h_A(matrixSize);
    host_buffer_t<CG_dataType> h_x(vecSize);
    host_buffer_t<CG_dataType> h_b(vecSize);
    host_buffer_t<CG_dataType> h_pk(vecSize);
    host_buffer_t<CG_dataType> h_Apk(vecSize);
    host_buffer_t<CG_dataType> h_xk(vecSize);
    host_buffer_t<CG_dataType> h_rk(vecSize);

    for (int i = 0; i < vecSize; i++) {
        h_xk[i] = 0;
        h_Apk[i] = 0;
    }

    string filepath = argv[l_index++];
    readBin(filepath + "A.mat", h_A.size() * sizeof(CG_dataType), h_A);
    readBin(filepath + "x.mat", h_x.size() * sizeof(CG_dataType), h_x);
    readBin(filepath + "b.mat", h_b.size() * sizeof(CG_dataType), h_b);

    CG_dataType l_dot = 0;
    for (int i = 0; i < vecSize; i++) {
        h_rk[i] = h_b[i];
        h_pk[i] = h_b[i];
        l_dot += h_b[i] * h_b[i];
    }

    /*
    cout << "The vector x:" << endl;
    for (auto x : h_x) cout << x << '\t';
    cout << endl;
    */

    xf::hpc::MemInstr<CG_instrBytes> l_memInstr;
    xf::hpc::cg::CGSolverInstr<CG_dataType> l_cgInstr;
    l_cgInstr.setMaxIter(l_maxIter);
    l_cgInstr.setTols(l_dot * l_tol * l_tol);
    l_cgInstr.setRZ(l_dot);
    l_cgInstr.setVecSize(vecSize);
    l_cgInstr.store(h_instr.data(), l_memInstr);
    cout << "Square of the norm(b) is: " << l_dot << endl;

    int l_deviceId = 0;
    if (argc > l_index) l_deviceId = atoi(argv[l_index++]);

    FPGA fpga(l_deviceId);
    fpga.xclbin(binaryFile);

    CGKernelControl l_kernelControl(&fpga);
    l_kernelControl.getCU("krnl_control");
    l_kernelControl.setMem(h_instr);

    CGKernelGemv<CG_dataType, CG_parEntries> l_kernelGemv(&fpga);
    l_kernelGemv.getCU("krnl_gemv");
    l_kernelGemv.setMem(h_A, h_pk, h_Apk);

    CGKernelUpdatePk<CG_dataType> l_kernelUpdatePk(&fpga);
    l_kernelUpdatePk.getCU("krnl_update_pk");
    l_kernelUpdatePk.setMem(h_pk, h_rk);

    CGKernelUpdateRk<CG_dataType> l_kernelUpdateRk(&fpga);
    CGKernelUpdateXk<CG_dataType> l_kernelUpdateXk(&fpga);
    l_kernelUpdateXk.getCU("krnl_update_xk");
    l_kernelUpdateRk.getCU("krnl_update_rk");
    l_kernelUpdateXk.setMem(h_xk, h_pk);
    l_kernelUpdateRk.setMem(h_rk, h_Apk);

    vector<Kernel> l_kernels;
    l_kernels.push_back(l_kernelControl);
    l_kernels.push_back(l_kernelGemv);
    l_kernels.push_back(l_kernelUpdatePk);
    l_kernels.push_back(l_kernelUpdateXk);
    l_kernels.push_back(l_kernelUpdateRk);

    Kernel::run(l_kernels);

    l_kernelControl.getMem();
    l_kernelUpdateXk.getMem();
    l_kernelUpdateRk.getMem();
    l_kernelUpdatePk.getMem();

    writeBin(filepath + "rk.mat", h_rk.size() * sizeof(CG_dataType), h_rk);
    writeBin(filepath + "xk.mat", h_xk.size() * sizeof(CG_dataType), h_xk);
    writeBin(filepath + "pk.mat", h_pk.size() * sizeof(CG_dataType), h_pk);

    int l_lastIter = 0;

    for (int i = 0; i < l_maxIter; i++) {
        l_lastIter = i;
        l_cgInstr.load(h_instr.data() + (i + 1) * CG_instrBytes, l_memInstr);
        // cout << l_cgInstr << endl;
        if (l_cgInstr.getMaxIter() == 0) {
            break;
        }
    }

    cout << "The solver finished at iteration " << l_lastIter << endl;
    int err = 0;
    compare(h_x.size(), h_x.data(), h_xk.data(), err);
    if (err == 0)
        return EXIT_SUCCESS;
    else {
        cout << "There are in total " << err << " mismatches in the solution." << endl;
        return EXIT_FAILURE;
    }
}
