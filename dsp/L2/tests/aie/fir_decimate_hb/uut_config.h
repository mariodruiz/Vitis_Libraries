//------------------------------------------------------------------------------
// UUT DEFAULT CONFIGURATION
#ifndef DATA_TYPE
#define DATA_TYPE cint16
#endif
#ifndef COEFF_TYPE
#define COEFF_TYPE int16
#endif
#ifndef FIR_LEN
#define FIR_LEN 32
#endif
#ifndef SHIFT
#define SHIFT 16
#endif
#ifndef ROUND_MODE
#define ROUND_MODE 0
#endif
#ifndef INPUT_WINDOW_VSIZE
#define INPUT_WINDOW_VSIZE 256
#endif
#ifndef CASC_LEN
#define CASC_LEN 1
#endif
#ifndef DUAL_IP
#define DUAL_IP 0
#endif
#ifndef USE_COEFF_RELOAD
#define USE_COEFF_RELOAD 0
#endif

#ifndef USE_CHAIN
#define USE_CHAIN 0
#endif

#ifndef INPUT_FILE
#define INPUT_FILE "data/input.txt"
#endif
#ifndef OUTPUT_FILE
#define OUTPUT_FILE "data/output.txt"
#endif

#ifndef NUM_ITER
#define NUM_ITER 1
#endif

#ifndef DECIMATE_FACTOR
#define DECIMATE_FACTOR 2
#endif

#define INPUT_SAMPLES INPUT_WINDOW_VSIZE* NUM_ITER
#define INPUT_MARGIN(x, y) CEIL(x, (32 / sizeof(y)))
#define OUTPUT_SAMPLES INPUT_WINDOW_VSIZE* NUM_ITER / DECIMATE_FACTOR

#ifndef COEFF_SEED
#define COEFF_SEED 0xC0FFEE
#endif

// END OF UUT CONFIGURATION
//------------------------------------------------------------------------------

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
