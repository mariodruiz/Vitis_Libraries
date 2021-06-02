#ifndef _DSPLIB_TEST_HPP_
#define _DSPLIB_TEST_HPP_

// This file holds the definition of the test harness for the Widget API Cast graph.

#include <adf.h>
#include <vector>
#include "utils.hpp"

#include "uut_config.h"
#include "test_stim.hpp"

#define Q(x) #x
#define QUOTE(x) Q(x)

#ifndef UUT_GRAPH
#define UUT_GRAPH widget_api_cast_graph
#endif

#include QUOTE(UUT_GRAPH.hpp)

using namespace adf;

namespace xf {
namespace dsp {
namespace aie {
namespace testcase {

class test_graph : public graph {
   private:
   public:
    port<input> in[NUM_INPUTS];
    port<output> out[NUM_OUTPUT_CLONES];

    // Constructor
    test_graph() {
        printf("========================\n");
        printf("== Widget test.hpp parameters: ");
        printf(QUOTE(UUT_GRAPH));
        printf("\n");
        printf("========================\n");
        printf("Data type         = ");
        printf(QUOTE(DATA_TYPE));
        printf("\n");
        printf("IN_API            = %d \n", IN_API);
        printf("OUT_API           = %d \n", OUT_API);
        printf("NUM_INPUTS        = %d \n", NUM_INPUTS);
        printf("WINDOW_VSIZE      = %d \n", WINDOW_VSIZE);
        printf("NUM_OUTPUT_CLONES = %d \n", NUM_OUTPUT_CLONES);
        namespace dsplib = xf::dsp::aie;

        // Widget sub-graph
        dsplib::widget::api_cast::UUT_GRAPH<DATA_TYPE, IN_API, OUT_API, NUM_INPUTS, WINDOW_VSIZE, NUM_OUTPUT_CLONES>
            widgetGraph;

        // Make connections
        for (int i = 0; i < NUM_INPUTS; i++) {
            connect<>(in[i], widgetGraph.in[i]);
        }
        for (int i = 0; i < NUM_OUTPUT_CLONES; i++) {
            connect<>(widgetGraph.out[i], out[i]);
        }

#ifdef USING_UUT
        // For cases which use 3 or 4 output windows, contention and poor QoR is seen if the processor is at the edge of
        // the array since
        // the windows then share banks and contention occurs.
        location<kernel>(*widgetGraph.getKernels()) = tile(1, 1);
#endif

#ifdef USING_UUT
#define CASC_LEN 1
// Report out for AIE Synthesizer QoR harvest
// Nothing to report
#endif
        printf("========================\n");
    };
};
}
}
}
};

#endif // _DSPLIB_TEST_HPP_

/*  (c) Copyright 2020 Xilinx, Inc. All rights reserved.

    This file contains confidential and proprietary information
    of Xilinx, Inc. and is protected under U.S. and
    international copyright and other intellectual property
    laws.

    DISCLAIMER
    This disclaimer is not a license and does not grant any
    rights to the materials distributed herewith. Except as
    otherwise provided in a valid license issued to you by
    Xilinx, and to the maximum extent permitted by applicable
    law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
    WITH ALL FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES
    AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
    BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
    INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
    (2) Xilinx shall not be liable (whether in contract or tort,
    including negligence, or under any other theory of
    liability) for any loss or damage of any kind or nature
    related to, arising under or in connection with these
    materials, including for any direct, or any indirect,
    special, incidental, or consequential loss or damage
    (including loss of data, profits, goodwill, or any type of
    loss or damage suffered as a result of any action brought
    by a third party) even if such damage or loss was
    reasonably foreseeable or Xilinx had been advised of the
    possibility of the same.

    CRITICAL APPLICATIONS
    Xilinx products are not designed or intended to be fail-
    safe, or for use in any application requiring fail-safe
    performance, such as life-support or safety devices or
    systems, Class III medical devices, nuclear facilities,
    applications related to the deployment of airbags, or any
    other applications that could lead to death, personal
    injury, or severe property or environmental damage
    (individually and collectively, "Critical
    Applications"). Customer assumes the sole risk and
    liability of any use of Xilinx products in Critical
    Applications, subject only to applicable laws and
    regulations governing limitations on product liability.

    THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
    PART OF THIS FILE AT ALL TIMES.                       */
