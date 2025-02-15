==================================
Xilinx GZip Streaming Compression
==================================

GZip example resides in ``L2/tests/gzipc`` directory. 

Follow build instructions to generate host executable and binary.

The binary host file generated is named as "**xil_gzip**" and it is present in ``./build`` directory.

Executable Usage
----------------

1. To execute single file for compression 	          : ``./build/xil_gzip -cx ./build/xclbin_<xsa_name>_<TARGET mode>/compress.xclbin -c <file_name>``
2. To execute multiple files for compression    : ``./build/xil_gzip -cx ./build/xclbin_<xsa_name>_<TARGET mode>/compress.xclbin -l <files.list>``

	- ``<files.list>``: Contains various file names with current path

The usage of the generated executable is as follows:

.. code-block:: bash
 
   Usage: application.exe -[-h-c-l-cx-B]
          --help,           -h      Print Help Options
          --xclbin,         -cx     XCLBIN                                               Default: [compress]
          --compress,       -c      Compress
          --file_list,      -l      List of Input Files
          --block_size,     -B      Compress Block Size [0-32: 1-64: 2-1024: 3-4096]     Default: [0]
 
Results
-------

Resource Utilization 
~~~~~~~~~~~~~~~~~~~~~

Table below presents resource utilization of Xilinx GZip Compress/Decompress
kernels. The final Fmax achieved is 285MHz 


========== ===== ====== ===== ===== ===== 
Flow       LUT   LUTMem REG   BRAM  URAM 
========== ===== ====== ===== ===== ===== 
Compress   49.7K 3K     51.7K  67    72    
========== ===== ====== ===== ===== ===== 

Performance Data
~~~~~~~~~~~~~~~~

Table below presents kernel throughput achieved for a single compute
unit. 

============================= =========================
Topic                         Results
============================= =========================
Compression Throughput        2 GB/s
Average Compression Ratio     2.67x (Silesia Benchmark)
============================= =========================

Standard GZip Support
---------------------

This application is compatible with standard Gzip/Zlib application (compress/decompres).  
