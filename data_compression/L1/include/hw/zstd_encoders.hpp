/*
 * (c) Copyright 2021 Xilinx, Inc. All rights reserved.
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
 *
 */
#ifndef _XFCOMPRESSION_ZSTD_ENCODERS_HPP_
#define _XFCOMPRESSION_ZSTD_ENCODERS_HPP_

/**
 * @file zstd_encoders.hpp
 * @brief Header for modules used in ZSTD compression kernel.
 *
 * This file is part of Vitis Data Compression Library.
 */

#include "hls_stream.h"
#include "ap_axi_sdata.h"
#include <ap_int.h>
#include <stdint.h>

#include "zstd_specs.hpp"
#include "compress_utils.hpp"

namespace xf {
namespace compression {
namespace details {

template <int MAX_BITS>
void zstdHuffBitPacker(hls::stream<DSVectorStream_dt<HuffmanCode_dt<MAX_BITS>, 1> >& hfEncodedStream,
                       hls::stream<IntVectorStream_dt<8, 2> >& hfBitStream) {
    // pack huffman codes from multiple input code streams
    bool done = false;
    IntVectorStream_dt<8, 2> outVal;
    ap_uint<32> outReg = 0;
    while (!done) {
        done = true;
        outVal.strobe = 2;
        uint8_t validBits = 0;
        uint16_t outCnt = 0;
    hf_bitPacking:
        for (auto inVal = hfEncodedStream.read(); inVal.strobe > 0; inVal = hfEncodedStream.read()) {
#pragma HLS PIPELINE II = 1
            done = false;
            outReg.range((uint8_t)(inVal.data[0].bitlen) + validBits - 1, validBits) = inVal.data[0].code;
            validBits += (uint8_t)(inVal.data[0].bitlen);
            if (validBits > 15) {
                validBits -= 16;
                outVal.data[0] = outReg.range(7, 0);
                outVal.data[1] = outReg.range(15, 8);
                // printf("%u. huff bitstream: %u\n", outCnt, (uint8_t) outVal.data[0]);
                // printf("%u. huff bitstream: %u\n", outCnt + 1, (uint8_t) outVal.data[1]);
                outReg >>= 16;
                hfBitStream << outVal;
                outCnt += 2;
            }
        }
        if (outCnt) {
            // mark end by adding 1-bit "1"
            outReg |= (uint32_t)1 << validBits;
            ++validBits;
        }
        // write remaining bits
        if (validBits) {
            outVal.strobe = (validBits + 7) >> 3;
            outVal.data[0] = outReg.range(7, 0);
            outVal.data[1] = outReg.range(15, 8); // dummy if strobe = 1
            // printf("%u. huff bitstream: %u\n", outCnt, (uint8_t) outVal.data[0]);
            // if (outVal.strobe > 1) printf("%u. huff bitstream: %u\n", outCnt + 1, (uint8_t) outVal.data[1]);
            hfBitStream << outVal;
            outCnt += outVal.strobe;
        } // exit(0);
        // printf("bitpacker written bytes: %u\n", outCnt);
        outVal.strobe = 0;
        hfBitStream << outVal;
    }
}

template <int MAX_BITS, int INSTANCES = 1>
void zstdHuffmanEncoder(hls::stream<IntVectorStream_dt<8, 1> >& inValStream,
                        hls::stream<bool>& rleFlagStream,
                        hls::stream<DSVectorStream_dt<HuffmanCode_dt<MAX_BITS>, 1> >& hfCodeStream,
                        hls::stream<DSVectorStream_dt<HuffmanCode_dt<MAX_BITS>, 1> >& hfEncodedStream,
                        hls::stream<ap_uint<16> >& hfLitMetaStream) {
    // read huffman table
    HuffmanCode_dt<MAX_BITS> hfcTable[256]; // use LUTs for implementation as registers
    DSVectorStream_dt<HuffmanCode_dt<MAX_BITS>, 1> outVal;
    bool done = false;

    while (true) {
        uint16_t hIdx = 0;
        // pre-read value to check strobe
        auto inVal = inValStream.read();
        if (inVal.strobe == 0) break;
        // Check if this literal block is RLE type
        bool isRle = rleFlagStream.read();
    // read this table only once
    read_hfc_table:
        for (auto hfCode = hfCodeStream.read(); hfCode.strobe > 0; hfCode = hfCodeStream.read()) {
#pragma HLS PIPELINE II = 1
            hfcTable[hIdx++] = hfCode.data[0];
        }
        uint8_t streamCnt = inVal.data[0];
        hfLitMetaStream << ((uint16_t)streamCnt & 0x000F);
    // Parallel read 8 * INSTANCES bits of input
    // Parallel encode to INSTACNCES output streams
    encode_lit_streams:
        for (uint8_t si = 0; si < streamCnt; ++si) {
            ap_uint<16> streamSize;
            uint16_t streamCmpSize = 0;
            uint8_t cmpBits = 0;
            // read present stream size
            inVal = inValStream.read();
            streamSize.range(7, 0) = inVal.data[0];
            inVal = inValStream.read();
            streamSize.range(15, 8) = inVal.data[0];
            // hfLitMetaStream << streamSize;
            // Since for RLE type literals, only one stream is present
            // in that stream, last literal must not be encoded, comes first in reversed stream
            if (isRle) {
                --streamSize;
                inValStream.read();
            }
            outVal.strobe = 1;
        huff_encode:
            for (uint16_t i = 0; i < (uint16_t)streamSize; ++i) {
#pragma HLS PIPELINE II = 1
                inVal = inValStream.read();
                outVal.data[0] = hfcTable[inVal.data[0]];
                hfEncodedStream << outVal;
                /*if (outVal.data[0].bitlen == 0) {
                        printf("Error: 0 bitlen for char_num: %u at idx: %u is 0\n", (uint8_t)inVal.data[0], i);
                        exit(0);
                }*/
                cmpBits += outVal.data[0].bitlen;
                if (cmpBits > 7) {
                    streamCmpSize += cmpBits >> 3;
                    cmpBits &= 7;
                }
            }
            // cmpBits cannot be greater than 7, 1 extra bit indicating end of stream
            if (cmpBits + 1) {
                ++streamCmpSize;
            }
            hfLitMetaStream << streamCmpSize;
            // printf("huf stream size orig: %u, cmp: %u\n", (uint16_t)streamSize, streamCmpSize);
            // end of sub-stream
            outVal.strobe = 0;
            hfEncodedStream << outVal;
        }
    }
    // end of file
    hfCodeStream.read();
    outVal.strobe = 0;
    hfEncodedStream << outVal;
}

inline uint8_t getOptimalTableLog(const uint8_t maxLog, uint16_t symbolSize, uint16_t currentMaxCode) {
#pragma HLS INLINE off
    auto maxBitsNeeded = bitsUsed31(symbolSize - 1) - 2;
    ap_uint<10> tableLog = maxLog;
    // fin table log
    uint8_t minBitsSrc = bitsUsed31(symbolSize) + 1;
    uint8_t minBitsSymbols = bitsUsed31(currentMaxCode) + 2;
    uint8_t minBitsNeeded = minBitsSrc < minBitsSymbols ? minBitsSrc : minBitsSymbols;

    if (maxBitsNeeded < tableLog) tableLog = maxBitsNeeded; // Accuracy can be reduced
    if (minBitsNeeded > tableLog) tableLog = minBitsNeeded; // Need a minimum to safely represent all symbol values
    if (tableLog < c_fseMinTableLog) tableLog = c_fseMinTableLog;
    if (tableLog > maxLog) tableLog = maxLog; // will be changed to c_fseMaxTableLog
    return (uint8_t)tableLog;
}

template <int MAX_FREQ_DWIDTH = 17>
void normalizeFreq(
    ap_uint<MAX_FREQ_DWIDTH>* inFreq,
    uint16_t symbolSize, // total : maximum value of symbols (max literal value, number of LL/ML/OF code values: seqcnt)
    uint16_t curMaxCodeVal, // maxSymbolValue : maximum code value (max bitlen value for literal huffman codes, max
                            // LL/ML/OF code value)
    uint8_t tableLog,
    int16_t* normTable) {
#pragma HLS INLINE off
    // core module to generate normalized table
    uint64_t rtbXvStepTable[8];
    uint8_t scale = 62 - tableLog;
    ap_uint<64> step = ((ap_uint<64>)1 << 62) / symbolSize; /* <-- one division here ! */
    uint64_t vStep = (uint64_t)1 << (scale - 20);
    int stillToDistribute = 1 << tableLog;
    uint32_t largest = 0;
    int16_t largestP = 0;
    uint32_t lowThreshold = (uint32_t)(symbolSize >> tableLog);
// printf("NormalizeFreq - tableLog: %u, total: %u, maxSymbolValue: %u\n", tableLog, symbolSize, curMaxCodeVal);
init_rtbTableSteps:
    for (int i = 0; i < 8; ++i) {
#pragma HLS PIPELINE off
        rtbXvStepTable[i] = vStep * c_rtbTable[i];
    }

norm_count:
    for (uint16_t s = 0; s <= curMaxCodeVal; ++s) {
#pragma HLS PIPELINE II = 1
        // if (inFreq[s] == symbolSize) return 0;   /* rle special case */
        auto freq = inFreq[s];
        int16_t proba = (int16_t)((freq * step) >> scale);
        ap_uint<1> incrProba = (((freq * step) - ((uint64_t)proba << scale)) > rtbXvStepTable[proba]);
        int16_t val = 0;
        int16_t valDecrement = 0;
        if (freq == 0) {
            val = 0;
            valDecrement = 0;
        } else if (freq <= lowThreshold) {
            val = -1;
            valDecrement = 1;
        } else {
            if (proba < 8) {
                // uint64_t restToBeat = vStep * c_rtbTable[proba];
                proba += incrProba;
            }
            if (proba > largestP) {
                largestP = proba;
                largest = s;
            }
            val = proba;
            valDecrement = proba;
        }
        normTable[s] = val;
        stillToDistribute -= valDecrement;
        //        printf("table: %d   rem: %d\n",normTable[s],stillToDistribute);
    }
    assert(-stillToDistribute < (normTable[largest] >> 1));
    // corner case, need another normalization method
    // FSE_normalizeM2(normTable, tableLog, inFreq, symbolSize, curMaxCodeVal);
    // printf("Warning: FSE Normalized Table corner case hit!!!\n");
    normTable[largest] += (short)stillToDistribute;
    // print normalized table
    // for (int i = 0; i <= curMaxCodeVal; ++i) printf("%d. normCount: %d\n", i, normTable[i]);
}

template <int MAX_FREQ_DWIDTH = 17>
void normalizedTableGen(hls::stream<IntVectorStream_dt<MAX_FREQ_DWIDTH, 1> >& inFreqStream,
                        hls::stream<IntVectorStream_dt<16, 1> > normTableStream[2]) {
    // generate normalized counter to be used for fse table generation
    /*
     * > Read the frequencies
     * > Get max frequency value, maxCode and sequence count
     * > Calculate optimal table log
     * > Calculate normalized distribution table
     */
    const uint8_t c_maxTableLog[4] = {c_fseMaxTableLogLL, c_fseMaxTableLogOF, c_fseMaxTableLogHF, c_fseMaxTableLogML};
    const uint8_t c_hfIdx = 2;
    IntVectorStream_dt<16, 1> outVal;
    ap_uint<MAX_FREQ_DWIDTH> inFreq[64]; // using max possible size for array
    int16_t normTable[64];
    uint16_t seqCnt = 0;
norm_tbl_outer_loop:
    while (true) {
        uint8_t lastSeq[3] = {0, 0, 0}; // ll, ml, of
        uint8_t lsk = 0;
        // first value is sequence count
        auto inVal = inFreqStream.read();
        if (inVal.strobe == 0) break;
        seqCnt = inVal.data[0];
    // last sequence
    read_last_seq:
        for (uint8_t i = 0; i < 3; ++i) {
#pragma HLS PIPELINE II = 1
            inVal = inFreqStream.read();
            lastSeq[i] = inVal.data[0];
        }
    calc_norm_outer:
        for (uint8_t k = 0; k < 4; ++k) {
        init_norm_table:
            for (uint8_t i = 0; i < 64; ++i) {
#pragma HLS PIPELINE off
                normTable[i] = 0;
            }
            uint16_t maxSymbol;
            ap_uint<MAX_FREQ_DWIDTH> maxFreq = 0;
            uint16_t symCnt = seqCnt;
            volatile uint16_t symCnt_1 = seqCnt;
            // read max literal value, before weight freq data
            if (c_hfIdx == k) {
                inVal = inFreqStream.read();
                symCnt = inVal.data[0];
                symCnt_1 = symCnt;
            }
            // number of frequencies to read
            inVal = inFreqStream.read();
            uint16_t freqCnt = inVal.data[0];
        // read input frequencies
        read_in_freq:
            for (uint16_t i = 0; i < freqCnt; ++i) {
#pragma HLS PIPELINE II = 1
                inVal = inFreqStream.read();
                inFreq[i] = inVal.data[0];
                if (inVal.data[0] > 0) maxSymbol = i;
                if (inVal.data[0] > maxFreq) maxFreq = inVal.data[0];
                // if (c_hfIdx == k) printf("weight: %u, freq: %u\n", i, (uint8_t)inVal.data[0]);
            }
            // get optimal table log
            uint8_t tableLog = getOptimalTableLog(c_maxTableLog[k], symCnt, maxSymbol);
            if (c_hfIdx != k) {
                if (inFreq[lastSeq[lsk]] > 1) {
                    inFreq[lastSeq[lsk]]--;
                    symCnt_1 -= 1;
                    // printf("%d. decIndx: %u\n", lsk, (uint16_t)lastSeq[lsk]);
                }
                ++lsk;
            }
            // generate normalized distribution table
            normalizeFreq<MAX_FREQ_DWIDTH>(inFreq, symCnt_1, maxSymbol, tableLog, normTable);
            // write normalized table to output
            outVal.strobe = 1;
            // write tableLog, max val at the end of table log
            normTable[63] = maxSymbol;
            normTable[62] = tableLog;
            normTable[61] = (int16_t)(c_hfIdx == k); // To be used later for optimization
        write_norm_table:
            for (uint16_t i = 0; i < 64; ++i) {
#pragma HLS PIPELINE II = 1
                outVal.data[0] = normTable[i];
                normTableStream[0] << outVal;
                normTableStream[1] << outVal;
            }
        }
    }
    outVal.strobe = 0;
    normTableStream[0] << outVal;
    normTableStream[1] << outVal;
}

void fseHeaderGen(hls::stream<IntVectorStream_dt<16, 1> >& normTableStream,
                  hls::stream<IntVectorStream_dt<8, 2> >& fseHeaderStream) {
    // generate normalized counter to be used for fse table generation
    int16_t normTable[64];
    IntVectorStream_dt<8, 2> outVal;

fse_header_gen_outer:
    while (true) {
        auto inVal = normTableStream.read();
        if (inVal.strobe == 0) break;
        normTable[0] = inVal.data[0];
    // printf("%u. normTbl: %d\n", 0, normTable[0]);
    read_norm_table:
        for (uint8_t i = 1; i < 64; ++i) {
#pragma HLS PIPELINE II = 1
            inVal = normTableStream.read();
            normTable[i] = inVal.data[0];
            // printf("%u. normTbl: %d\n", i, normTable[i]);
        }
        uint16_t maxCharSize = normTable[63] + 1;
        uint8_t tableLog = normTable[62];
        // printf("tableLog: %u\n\n", tableLog);

        uint16_t tableSize = 1 << tableLog;
        ap_uint<32> bitStream = 0;
        int8_t bitCount = 0;
        uint16_t symbol = 0;
        uint8_t outCount = 0;

        /* Table Size */
        bitStream += (tableLog - c_fseMinTableLog);
        bitCount += 4;

        /* Init */
        int remaining = tableSize + 1; /* +1 for extra accuracy */
        uint16_t threshold = tableSize;
        uint8_t nbBits = tableLog + 1;
        outVal.strobe = 2;
        uint16_t start = symbol;
        enum ReadNCountStates { PREV_IS_ZERO, REM_LT_THR, NON_ZERO_CNT };
        ReadNCountStates fsmState = NON_ZERO_CNT;
        ReadNCountStates fsmNextState = NON_ZERO_CNT;
        bool previousIs0 = false;
        bool skipZeroFreq = true;
    // printf("FSE Encoded header, maxCharSize: %u, remaining: %d\n", maxCharSize, remaining);
    gen_fse_header_bitstream:
        while ((symbol < maxCharSize) && (remaining > 1)) {
#pragma HLS PIPELINE II = 1
            if (fsmState == PREV_IS_ZERO) {
                if (skipZeroFreq) {
                    if (symbol < maxCharSize && !normTable[symbol]) {
                        ++symbol;
                        // printf("Here 0\n");
                    } else {
                        skipZeroFreq = false;
                    }
                } else {
                    if (symbol >= start + 24) {
                        start += 24;
                        bitStream += 0xFFFF << bitCount;
                        bitCount += 16;
                        // printf("Here 1\n");
                    } else if (symbol >= start + 3) {
                        start += 3;
                        bitStream += 3 << bitCount;
                        bitCount += 2;
                        // printf("Here 2\n");
                    } else {
                        fsmState = NON_ZERO_CNT;
                        bitStream += (uint32_t)(symbol - start) << bitCount;
                        bitCount += 2;
                        // printf("Here 3\n");
                    }
                }
            } else if (fsmState == REM_LT_THR) {
                --nbBits;
                threshold >>= 1;
                // printf("Here 5\n");
                if (remaining > threshold - 1) {
                    fsmState = fsmNextState;
                }
            } else {
                int16_t count = normTable[symbol++];
                int max = (2 * threshold) - (1 + remaining);
                remaining -= (count < 0) ? -count : count;
                ++count;
                if (count >= threshold) count += max;
                bitStream += count << bitCount;
                bitCount += nbBits;
                // printf("Here 4, remaining: %d, threshold: %d, nbBits: %d\n", remaining, threshold, nbBits);
                bitCount -= (uint8_t)(count < max);
                previousIs0 = (count == 1);
                start = symbol;      // set starting symbol for PREV_IS_ZERO state
                skipZeroFreq = true; // enable skipping of zero norm values for PREV_IS_ZERO state
                fsmNextState = (previousIs0 ? PREV_IS_ZERO : NON_ZERO_CNT);
                fsmState = ((remaining < threshold) ? REM_LT_THR : fsmNextState);
            }
            // write output bitstream 16-bits at a time
            if (bitCount >= 16) {
                outVal.data[0] = bitStream.range(7, 0);
                outVal.data[1] = bitStream.range(15, 8);
                // printf("%u. bitstream: %u\n", outCount, (uint8_t)bitStream);
                // printf("%u. bitstream: %u\n", outCount + 1, (uint8_t)(bitStream >> 8));
                fseHeaderStream << outVal;
                bitStream >>= 16;
                bitCount -= 16;
                outCount += 2;
            }
        }
        if (bitCount) {
            outVal.strobe = (uint8_t)((bitCount + 7) / 8);
            outVal.data[0] = bitStream.range(7, 0);
            outVal.data[1] = bitStream.range(15, 8);
            // printf("%u. bitstream: %u\n", outCount, (uint8_t)bitStream);
            // if(outVal.strobe > 1) printf("%u. bitstream: %u\n", outCount + 1, (uint8_t)bitStream.range(15, 8));
            fseHeaderStream << outVal;
            outCount += outVal.strobe;
        }
        // printf("out count: %u\n", outCount);
        outVal.strobe = 0;
        fseHeaderStream << outVal;
    }
    outVal.strobe = 0;
    fseHeaderStream << outVal;
}

void fseEncodingTableGen(hls::stream<IntVectorStream_dt<16, 1> >& normTableStream,
                         hls::stream<IntVectorStream_dt<36, 1> >& fseTableStream) {
    // generate normalized counter to be used for fse table generation
    const uint8_t c_hfIdx = 2; // index of data for literal bitlens
    int16_t normTable[64];
    uint8_t symTable[512];
    uint16_t tableU16[512];
    uint32_t intm[257];
    IntVectorStream_dt<36, 1> outVal;
    uint8_t cIdx = 0;
    while (true) {
        // read normalized counter
        auto inVal = normTableStream.read();
        if (inVal.strobe == 0) break;
        normTable[0] = inVal.data[0];
    fetg_read_norm_tbl:
        for (uint8_t i = 1; i < 64; ++i) {
#pragma HLS PIPELINE II = 1
            inVal = normTableStream.read();
            normTable[i] = inVal.data[0];
        }
        uint8_t tableLog = normTable[62];
        uint16_t maxSymbol = normTable[63];
        uint16_t tableSize = 1 << tableLog;
        uint32_t tableMask = tableSize - 1;
        const uint32_t step = (tableSize >> 1) + (tableSize >> 3) + 3;
        uint32_t highThreshold = tableSize - 1;

        intm[0] = 0;
    fse_gen_symbol_start_pos:
        for (uint32_t s = 1; s <= maxSymbol + 1; ++s) {
#pragma HLS PIPELINE II = 1
            if (normTable[s - 1] == -1) {
                intm[s] = intm[s - 1] + 1;
                symTable[highThreshold] = s - 1;
                --highThreshold;
            } else {
                intm[s] = intm[s - 1] + normTable[s - 1];
            }
        }
        intm[maxSymbol + 1] = tableSize + 1;

        // spread symbols
        uint16_t pos = 0;
    fse_spread_symbols_outer:
        for (uint32_t s = 0; s <= maxSymbol; ++s) {
        fse_spread_symbols:
            for (int16_t n = 0; n < normTable[s];) {
#pragma HLS PIPELINE II = 1
                if (pos > highThreshold) {
                    pos = (pos + step) & tableMask;
                } else {
                    symTable[pos] = s;
                    pos = (pos + step) & tableMask;
                    ++n;
                }
            }
        }
    // tableU16[-2] = tableLog;
    // tableU16[-1] = maxSymbol;
    build_table:
        for (uint16_t u = 0; u < tableSize; ++u) {
#pragma HLS PIPELINE II = 1
            auto s = symTable[u];
            tableU16[intm[s]++] = tableSize + u;
        }
        /*if (c_hfIdx != cIdx) {
            printf("Next state table\n");
            for (int i = 0; i < tableSize; ++i) printf("%d. nxtState: %u\n", i, tableU16[i]);
        }*/
        outVal.strobe = 1;
        // send tableLog and maxSymbol
        outVal.data[0].range(7, 0) = tableLog;
        outVal.data[0].range(35, 8) = maxSymbol;
        fseTableStream << outVal;

    // send state table, tableSize on reader side can be calculated using tableLog
    send_state_table:
        for (uint16_t i = 0; i < tableSize; ++i) {
#pragma HLS PIPELINE II = 1
            outVal.data[0] = tableU16[i];
            fseTableStream << outVal;
        }

        // printf("Find state and bit count table\n");
        uint16_t total = 0;
    build_sym_transformation_table:
        for (uint16_t s = 0; s <= maxSymbol; ++s) {
#pragma HLS PIPELINE II = 1
            int nv = normTable[s];
            uint8_t sym = 0;
            uint32_t nBits = 0;
            int16_t findState = 0;
            if (nv == 0) {
                nBits = ((tableLog + 1) << 16) - (1 << tableLog);
            } else if (nv == 1 || nv == -1) {
                nBits = (tableLog << 16) - (1 << tableLog);
                findState = total - 1;
                ++total;
            } else {
                uint8_t maxBitsOut = tableLog - bitsUsed31(nv - 1);
                uint32_t minStatePlus = (uint32_t)nv << maxBitsOut;
                nBits = (maxBitsOut << 16) - minStatePlus;
                findState = total - nv;
                total += nv;
            }
            outVal.data[0].range(19, 0) = nBits;
            outVal.data[0].range(35, 20) = findState;
            fseTableStream << outVal;
            // printf("%u. findState: %d, nBits: %u\n", s, findState, nBits);
        }
        outVal.strobe = 0;
        fseTableStream << outVal;
        cIdx++;
        if (cIdx == 4) cIdx = 0;
    }
    outVal.strobe = 0;
    fseTableStream << outVal;
}

void separateLitSeqTableStreams(hls::stream<IntVectorStream_dt<36, 1> >& fseTableStream,
                                hls::stream<IntVectorStream_dt<36, 1> >& fseLitTableStream,
                                hls::stream<IntVectorStream_dt<36, 1> >& fseSeqTableStream) {
    const uint8_t c_hfIdx = 2; // index of data for literal bitlens
    uint8_t cIdx = 0;
    IntVectorStream_dt<36, 1> fseTableVal;
sep_lit_seq_fset_outer:
    while (true) {
        fseTableVal = fseTableStream.read();
        if (fseTableVal.strobe == 0) break;
    sep_lit_seq_fseTableStreams:
        while (fseTableVal.strobe > 0) {
#pragma HLS PIPELINE II = 1
            if (cIdx == c_hfIdx) {
                fseLitTableStream << fseTableVal;
            } else {
                fseSeqTableStream << fseTableVal;
            }
            fseTableVal = fseTableStream.read();
        }
        // write strobe 0 value
        if (cIdx == c_hfIdx) {
            fseLitTableStream << fseTableVal;
        } else {
            fseSeqTableStream << fseTableVal;
        }
        ++cIdx;
        if (cIdx == 4) cIdx = 0;
    }
    fseTableVal.strobe = 0;
    fseLitTableStream << fseTableVal;
    fseSeqTableStream << fseTableVal;
}

template <int MAX_FREQ_DWIDTH = 17>
void fseTableGen(hls::stream<IntVectorStream_dt<MAX_FREQ_DWIDTH, 1> >& inFreqStream,
                 hls::stream<IntVectorStream_dt<8, 2> >& fseHeaderStream,
                 hls::stream<IntVectorStream_dt<36, 1> >& fseLitTableStream,
                 hls::stream<IntVectorStream_dt<36, 1> >& fseSeqTableStream) {
    // internal streams
    hls::stream<IntVectorStream_dt<16, 1> > normTableStream[2];
    hls::stream<IntVectorStream_dt<36, 1> > fseTableStream("fseTableStream");
#pragma HLS DATAFLOW
    // generate normalized counter table
    normalizedTableGen<MAX_FREQ_DWIDTH>(inFreqStream, normTableStream);
    // generate FSE header
    fseHeaderGen(normTableStream[0], fseHeaderStream);
    // generate FSE encoding tables
    fseEncodingTableGen(normTableStream[1], fseTableStream);
    // separate lit-seq fse table streams
    separateLitSeqTableStreams(fseTableStream, fseLitTableStream, fseSeqTableStream);
}

inline bool readFseTable(hls::stream<IntVectorStream_dt<36, 1> >& fseTableStream,
                         ap_uint<36>* fseStateBitsTable,
                         uint16_t* fseNextStateTable,
                         uint8_t& tableLog,
                         uint16_t& maxFreqLL) {
    // read FSE table values from input table stream
    auto fseVal = fseTableStream.read();
    if (fseVal.strobe == 0) return true;
    tableLog = fseVal.data[0].range(7, 0);
    maxFreqLL = fseVal.data[0].range(23, 8);

    uint16_t tableSize = (1 << tableLog);
    uint16_t fIdx = 0;
read_fse_tables:
    for (fseVal = fseTableStream.read(); fseVal.strobe > 0; fseVal = fseTableStream.read()) {
#pragma HLS PIPELINE II = 1
        if (fIdx < tableSize) {
            fseNextStateTable[fIdx] = (int16_t)fseVal.data[0].range(15, 0);
        } else {
            fseStateBitsTable[fIdx - tableSize] = fseVal.data[0];
        }
        ++fIdx;
    }
    return false;
}

template <int BITSTREAM_DWIDTH = 64>
inline void fseEncodeSymbol(uint8_t symbol,
                            ap_uint<36>* fseStateBitsTable,
                            uint16_t* fseNextStateTable,
                            uint16_t& fseState,
                            ap_uint<BITSTREAM_DWIDTH>& bitstream,
                            uint8_t& bitCount,
                            bool isInit) {
    // encode a symbol using fse table
    constexpr uint32_t c_oneBy15Lsh = ((uint32_t)1 << 15);
    ap_uint<36> stateBitVal = fseStateBitsTable[symbol];
    uint32_t deltaBits = (uint32_t)(stateBitVal.range(19, 0));
    int16_t findState = (int16_t)(stateBitVal.range(35, 20));
    uint32_t nbBits;
    uint16_t stVal;
    // printf("symbol: %u, deltaBits: %u\n", (uint16_t)symbol, deltaBits);
    if (isInit) {
        nbBits = (deltaBits + c_oneBy15Lsh) >> 16;
        stVal = (nbBits << 16) - deltaBits;
        // printf("Init ");
    } else {
        nbBits = (uint32_t)(deltaBits + fseState) >> 16;
        stVal = fseState;
        // write bits to bitstream
        bitstream |= ((ap_uint<BITSTREAM_DWIDTH>)(stVal & c_bitMask[nbBits]) << bitCount);
        bitCount += nbBits;
        // printf("Next ");
    }
    // store current state
    uint32_t nxIdx = (stVal >> nbBits) + findState;
    // printf("state: %u, stVal: %u, nBits: %u, findState: %d", nxIdx, stVal, nbBits, findState);
    // if (nxIdx > 512) {printf("\nError: Invalid state index!!\n"); exit(0);}
    fseState = fseNextStateTable[nxIdx];
    // printf(", newState: %d, bCnt: %u\n", fseState, bitCount);
}

void fseEncodeLitHeader(hls::stream<IntVectorStream_dt<4, 1> >& hufWeightStream,
                        hls::stream<IntVectorStream_dt<36, 1> >& fseLitTableStream,
                        hls::stream<IntVectorStream_dt<8, 2> >& encodedOutStream) {
    // fse encoding of huffman header for encoded literals
    constexpr uint32_t c_oneBy15Lsh = ((uint32_t)1 << 15);
    IntVectorStream_dt<8, 2> outVal;
    ap_uint<36> fseStateBitsTable[256];
    uint16_t fseNextStateTable[256];
    ap_uint<4> hufWeights[256];
    int blk_n = 0;
fse_lit_encode_outer:
    while (true) {
        blk_n++;
        uint8_t tableLog;
        uint16_t maxSymbol;
        uint16_t maxFreq;
        // read fse table
        uint16_t fIdx = 0;
        // read FSE encoding tables
        bool done = readFseTable(fseLitTableStream, fseStateBitsTable, fseNextStateTable, tableLog, maxFreq);
        if (done) break;
    // read details::c_maxLitV + 1 data from weight stream
    read_hf_weights:
        for (auto inWeight = hufWeightStream.read(); inWeight.strobe > 0; inWeight = hufWeightStream.read()) {
#pragma HLS PIPELINE II = 1
            hufWeights[fIdx] = inWeight.data[0];
            if (inWeight.data[0] > 0) maxSymbol = fIdx;
            ++fIdx;
        }
        uint16_t preStateVal[2];
        bool isInit[2] = {true, true};
        bool stateIdx = 0; // 0 for even, 1 for odd
        uint8_t bitCount = 0;
        ap_uint<32> bitstream = 0;
        outVal.strobe = 2;
        int outCnt = 0;
        // encode weights stored in reverse order
        stateIdx = maxSymbol & 1;
    // printf("Literal huffman header FSE encoding bitstream, maxSymbol: %u\n", maxSymbol);
    fse_lit_encode:
        for (int16_t w = maxSymbol - 1; w > -1; --w) {
#pragma HLS PIPELINE II = 1
            uint8_t symbol = hufWeights[w];
            fseEncodeSymbol<32>(symbol, fseStateBitsTable, fseNextStateTable, preStateVal[stateIdx], bitstream,
                                bitCount, isInit[stateIdx]);
            isInit[stateIdx] = false;
            // write bitstream to output
            if (bitCount > 15) {
                // write to output stream
                outVal.data[0] = bitstream.range(7, 0);
                outVal.data[1] = bitstream.range(15, 8);
                encodedOutStream << outVal;
                // printf("%d. bitstream: %u\n", outCnt, (uint8_t)outVal.data[0]);
                // printf("%d. bitstream: %u\n", outCnt+1, (uint8_t)outVal.data[1]);
                bitstream >>= 16;
                bitCount -= 16;
                outCnt += 2;
            }
            // switch state flow
            stateIdx = (stateIdx + 1) & 1; // 0 if 1, 1 if 0
        }
        // encode last two
        bitstream |= ((preStateVal[0] & c_bitMask[tableLog]) << bitCount);
        bitCount += tableLog;
        bitstream |= ((preStateVal[1] & c_bitMask[tableLog]) << bitCount);
        bitCount += tableLog;
        // mark end by adding 1-bit "1"
        bitstream |= (uint32_t)1 << bitCount;
        ++bitCount;
        // max remaining biCount can be 15 + (2 * 6) + 1= 28 bits => 4 bytes
        int8_t remBytes = (int8_t)((bitCount + 7) / 8);
    // write bitstream to output
    write_rem_bytes:
        while (remBytes > 0) {
#pragma HLS PIPELINE II = 1
            // write to output stream
            outVal.data[0] = bitstream.range(7, 0);
            outVal.data[1] = bitstream.range(15, 8);
            outVal.strobe = ((remBytes > 1) ? 2 : 1);
            encodedOutStream << outVal;
            bitstream >>= 16;
            remBytes -= 2;
            // printf("%d. bitstream r: %u\n", outCnt, (uint8_t)outVal.data[0]);
            // if(outVal.strobe > 1) printf("%d. bitstream r: %u\n", outCnt+1, (uint8_t)outVal.data[1]);
            outCnt += outVal.strobe;
        }
        // printf("%d. lit FSE encoded bs size: %u\n", blk_n, outCnt);
        outVal.strobe = 0;
        encodedOutStream << outVal;
    }
    // dump strobe 0 data
    hufWeightStream.read();
    // write end of block
    outVal.strobe = 0;
    encodedOutStream << outVal;
}

template <int MAX_FREQ_DWIDTH>
void fseEncodeSequences(hls::stream<DSVectorStream_dt<Sequence_dt<MAX_FREQ_DWIDTH>, 1> >& inSeqStream,
                        hls::stream<DSVectorStream_dt<Sequence_dt<8>, 1> >& inSeqCodeStream,
                        hls::stream<IntVectorStream_dt<36, 1> >& fseSeqTableStream,
                        hls::stream<IntVectorStream_dt<8, 4> >& encodedOutStream) {
    // fse encoding of reversed sequences stream
    IntVectorStream_dt<8, 4> outVal;
    ap_uint<36> fseStateBitsTableLL[512];
    uint16_t fseNextStateTableLL[512];
    ap_uint<36> fseStateBitsTableML[512];
    uint16_t fseNextStateTableML[512];
    ap_uint<36> fseStateBitsTableOF[256];
    uint16_t fseNextStateTableOF[256];
fse_lit_encode_outer:
    while (true) {
        uint8_t tableLogLL, tableLogML, tableLogOF;
        uint16_t maxSymbolLL, maxSymbolML, maxSymbolOF;
        uint16_t maxFreqLL, maxFreqML, maxFreqOF;
        // read initial value to check OES
        auto inSeq = inSeqStream.read();
        auto inSeqCode = inSeqCodeStream.read();
        if (inSeq.strobe == 0) break;
        // read FSE encoding tables for litlen, matlen, offset
        readFseTable(fseSeqTableStream, fseStateBitsTableLL, fseNextStateTableLL, tableLogLL, maxFreqLL);
        readFseTable(fseSeqTableStream, fseStateBitsTableOF, fseNextStateTableOF, tableLogOF, maxFreqOF);
        readFseTable(fseSeqTableStream, fseStateBitsTableML, fseNextStateTableML, tableLogML, maxFreqML);
        // printf("Sequences FSE Encoding\n");
        // read and encode sequences
        uint16_t seqCnt = 1;
        // printf("ll: %u, ml: %u, of: %u\n",
        //		(uint32_t)inSeq.data[0].litlen, (uint32_t)inSeq.data[0].matlen, (uint32_t)inSeq.data[0].offset);
        uint16_t llPrevStateVal, mlPrevStateVal, ofPrevStateVal;
        bool isInit[3] = {true, true, true};
        ap_uint<64> bitstream = 0;
        uint8_t bitCount = 0;
        int outCnt = 0;
        outVal.strobe = 4;
        // intialize states for matlen -> offset -> litlen
        fseEncodeSymbol((uint8_t)(inSeqCode.data[0].matlen), fseStateBitsTableML, fseNextStateTableML, mlPrevStateVal,
                        bitstream, bitCount, true);
        fseEncodeSymbol((uint8_t)(inSeqCode.data[0].offset), fseStateBitsTableOF, fseNextStateTableOF, ofPrevStateVal,
                        bitstream, bitCount, true);
        fseEncodeSymbol((uint8_t)(inSeqCode.data[0].litlen), fseStateBitsTableLL, fseNextStateTableLL, llPrevStateVal,
                        bitstream, bitCount, true);
        // add bits to bitstream

        uint32_t llBits = c_extraBitsLL[inSeqCode.data[0].litlen];
        uint32_t mlBits = c_extraBitsML[inSeqCode.data[0].matlen];
        bitstream |= ((uint64_t)(inSeq.data[0].litlen & c_bitMask[llBits]) << bitCount);
        bitCount += llBits;
        bitstream |= ((uint64_t)(inSeq.data[0].matlen & c_bitMask[mlBits]) << bitCount);
        bitCount += mlBits;
        // for offset
        int8_t ofBits = inSeqCode.data[0].offset;
        int8_t extraBits = ofBits - ((ofBits < 24) ? ofBits : 24);
        if (extraBits) {
            bitstream |= ((uint64_t)(inSeq.data[0].offset & c_bitMask[extraBits]) << bitCount);
            bitCount += extraBits;
        }
        int8_t remBits = ofBits - extraBits;
        remBits = remBits > 0 ? remBits : 0;
        bitstream |= ((uint64_t)((inSeq.data[0].offset >> extraBits) & c_bitMask[remBits]) << bitCount);
        bitCount += remBits;
        // write bitstream to output
        if (bitCount > 31) {
            // write to output stream
            outVal.data[0] = bitstream.range(7, 0);
            outVal.data[1] = bitstream.range(15, 8);
            outVal.data[2] = bitstream.range(23, 16);
            outVal.data[3] = bitstream.range(31, 24);
            encodedOutStream << outVal;
            // printf("%d. bitstream: %u\n", outCnt, (uint8_t)outVal.data[0]);
            // printf("%d. bitstream: %u\n", outCnt+1, (uint8_t)outVal.data[1]);
            // printf("%d. bitstream: %u\n", outCnt+2, (uint8_t)outVal.data[2]);
            // printf("%d. bitstream: %u\n", outCnt+3, (uint8_t)outVal.data[3]);
            // bitstream.range(31, 0) = bitstream.range(63, 32);
            bitstream >>= 32;
            bitCount -= 32;
            outCnt += 4;
        }

    encode_sequences:
        while (inSeq.strobe > 0) {
#pragma HLS PIPELINE II = 1
            // read the seq and code values
            inSeq = inSeqStream.read();
            inSeqCode = inSeqCodeStream.read();
            if (inSeq.strobe == 0) break;
            // if (seqCnt > 20) printf("ll: %u, ml: %u, of: %u\n",
            //		(uint16_t)inSeq.data[0].litlen, (uint16_t)inSeq.data[0].matlen, (uint16_t)inSeq.data[0].offset);
            uint8_t llCode = inSeqCode.data[0].litlen;
            uint8_t ofCode = inSeqCode.data[0].offset;
            uint8_t mlCode = inSeqCode.data[0].matlen;
            llBits = c_extraBitsLL[llCode];
            mlBits = c_extraBitsML[mlCode];
            int8_t ofBits = ofCode;
            // encode codes in order offset -> matlen -> litlen, isInit[0] is always false, used just for the sake of
            // argument
            fseEncodeSymbol(ofCode, fseStateBitsTableOF, fseNextStateTableOF, ofPrevStateVal, bitstream, bitCount,
                            false);
            fseEncodeSymbol(mlCode, fseStateBitsTableML, fseNextStateTableML, mlPrevStateVal, bitstream, bitCount,
                            false);
            fseEncodeSymbol(llCode, fseStateBitsTableLL, fseNextStateTableLL, llPrevStateVal, bitstream, bitCount,
                            false);

            // encode literal length
            bitstream |= ((uint64_t)(inSeq.data[0].litlen & c_bitMask[llBits]) << bitCount);
            bitCount += llBits;

            // encode match length
            bitstream |= ((uint64_t)(inSeq.data[0].matlen & c_bitMask[mlBits]) << bitCount);
            bitCount += mlBits;

            // encode offset
            int8_t extraBits = ofBits - ((ofBits < 56) ? ofBits : 56);
            // printf("extraBits: %d\n", extraBits);
            if (extraBits) {
                bitstream |= ((uint64_t)(inSeq.data[0].offset & c_bitMask[extraBits]) << bitCount);
                bitCount += extraBits;
            }
            int8_t remBits = ofBits - extraBits;
            bitstream |= ((uint64_t)((inSeq.data[0].offset >> extraBits) & c_bitMask[remBits]) << bitCount);
            bitCount += remBits;

            // push bitstream
            if (bitCount > 31) {
                // write to output stream
                outVal.data[0] = bitstream.range(7, 0);
                outVal.data[1] = bitstream.range(15, 8);
                outVal.data[2] = bitstream.range(23, 16);
                outVal.data[3] = bitstream.range(31, 24);
                // printf("%d. bitstream: %u\n", outCnt, (uint8_t)outVal.data[0]);
                // printf("%d. bitstream: %u\n", outCnt+1, (uint8_t)outVal.data[1]);
                // printf("%d. bitstream: %u\n", outCnt+2, (uint8_t)outVal.data[2]);
                // printf("%d. bitstream: %u\n", outCnt+3, (uint8_t)outVal.data[3]);
                encodedOutStream << outVal;
                // bitstream.range(31, 0) = bitstream.range(63, 32);
                bitstream >>= 32;
                bitCount -= 32;
                outCnt += 4;
            }
            ++seqCnt;
            // if (seqCnt > 24) exit(0);
        }
        // encode last sequence states
        bitstream |= ((uint64_t)(mlPrevStateVal & c_bitMask[tableLogML]) << bitCount);
        bitCount += tableLogML;
        bitstream |= ((uint64_t)(ofPrevStateVal & c_bitMask[tableLogOF]) << bitCount);
        bitCount += tableLogOF;
        bitstream |= ((uint64_t)(llPrevStateVal & c_bitMask[tableLogLL]) << bitCount);
        bitCount += tableLogLL;
        // mark end by adding 1-bit "1"
        bitstream |= (uint64_t)1 << bitCount;
        ++bitCount;
        // max remaining biCount can be 31 + 8 + 8 + 9 + 1 = 57 bits => 8 bytes
        int8_t remBytes = (int8_t)((bitCount + 7) / 8);
    // write bitstream to output
    write_rem_bytes:
        while (remBytes > 0) {
#pragma HLS PIPELINE II = 1
            // write to output stream
            outVal.data[0] = bitstream.range(7, 0);
            outVal.data[1] = bitstream.range(15, 8);
            outVal.data[2] = bitstream.range(23, 16);
            outVal.data[3] = bitstream.range(31, 24);
            outVal.strobe = ((remBytes > 4) ? 4 : remBytes);
            // printf("%d. bitstream r: %u\n", outCnt, (uint8_t)outVal.data[0]);
            // if(outVal.strobe > 1) printf("%d. bitstream r: %u\n", outCnt+1, (uint8_t)outVal.data[1]);
            // if(outVal.strobe > 2) printf("%d. bitstream r: %u\n", outCnt+2, (uint8_t)outVal.data[2]);
            // if(outVal.strobe > 3) printf("%d. bitstream r: %u\n", outCnt+3, (uint8_t)outVal.data[3]);
            encodedOutStream << outVal;
            // bitstream.range(31, 0) = bitstream.range(63, 32);
            bitstream >>= 32;
            bitCount -= 32;
            remBytes -= 4;
            outCnt += outVal.strobe;
        }
        // printf("seq FSE encoded bs size: %u\n", outCnt);
        outVal.strobe = 0;
        encodedOutStream << outVal;
    }
    // dump last strobe 0 data
    fseSeqTableStream.read();
    outVal.strobe = 0;
    encodedOutStream << outVal;
}

} // details
} // compression
} // xf
#endif
