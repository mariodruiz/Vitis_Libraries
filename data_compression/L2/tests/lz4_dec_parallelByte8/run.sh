#!/bin/bash
EXE_FILE=$1
LIB_PROJ_ROOT=$2
XCLBIN_FILE=$3
echo "XCL_MODE=${XCL_EMULATION_MODE}"
if [ "${XCL_EMULATION_MODE}" != "hw_emu" ] 
then
    cp $LIB_PROJ_ROOT/common/data/sample.txt .
  
    echo -e "\n\n----------Comparing files after Decompression---------\n"
    cmd1=$(diff sample.txt sample.txt.lz4.*)
    if [ $? -eq 0 ]
     then
        echo "PASS: files are the same"
    else
        echo "ERROR: files are different"
        echo "$cmd1"
   fi     
fi
