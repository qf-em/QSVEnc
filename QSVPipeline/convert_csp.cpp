﻿// -----------------------------------------------------------------------------------------
// QSVEnc by rigaya
// -----------------------------------------------------------------------------------------
// The MIT License
//
// Copyright (c) 2011-2016 rigaya
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
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// ------------------------------------------------------------------------------------------

#include <cstdint>
#include "qsv_tchar.h"
#include <vector>
#include "mfxstructures.h"
#include "qsv_simd.h"
#include "qsv_version.h"
#include "convert_csp.h"

enum : uint32_t {
    _P_ = 0x1,
    _I_ = 0x2,
    ALL = _P_ | _I_,
};

void convert_yuy2_to_nv12_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yuy2_to_nv12_avx(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yuy2_to_nv12_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

void convert_yuy2_to_nv12_i_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yuy2_to_nv12_i_ssse3(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yuy2_to_nv12_i_avx(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yuy2_to_nv12_i_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

void convert_yv12_to_nv12_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_to_nv12_avx(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_to_nv12_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

void convert_uv_yv12_to_nv12_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_uv_yv12_to_nv12_avx(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_uv_yv12_to_nv12_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

void convert_rgb3_to_rgb4_ssse3(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_rgb3_to_rgb4_avx(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_rgb3_to_rgb4_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

void convert_rgb4_to_rgb4_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_rgb4_to_rgb4_avx(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_rgb4_to_rgb4_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

void convert_yv12_to_p010_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_to_p010_avx(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_to_p010_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

void convert_yv12_16_to_nv12_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_16_to_nv12_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_14_to_nv12_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_14_to_nv12_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_12_to_nv12_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_12_to_nv12_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_10_to_nv12_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_10_to_nv12_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_09_to_nv12_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_09_to_nv12_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

void convert_yv12_16_to_p010_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_16_to_p010_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_14_to_p010_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_14_to_p010_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_12_to_p010_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_12_to_p010_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_10_to_p010_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_10_to_p010_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_09_to_p010_avx2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);
void convert_yv12_09_to_p010_sse2(void **dst, const void **src, int width, int src_y_pitch_byte, int src_uv_pitch_byte, int dst_y_pitch_byte, int height, int dst_height, int *crop);

#if defined(_MSC_VER) || defined(__AVX2__)
#define FUNC_AVX2(from, to, uv_only, funcp, funci, simd) { from, to, uv_only, { funcp, funci }, simd },
#else
#define FUNC_AVX2(from, to, uv_only, funcp, funci, simd)
#endif

#if defined(_MSC_VER) || defined(__AVX__)
#define FUNC_AVX(from, to, uv_only, funcp, funci, simd) { from, to, uv_only, { funcp, funci }, simd },
#else
#define FUNC_AVX(from, to, uv_only, funcp, funci, simd)
#endif
#define FUNC_SSE(from, to, uv_only, funcp, funci, simd) { from, to, uv_only, { funcp, funci }, simd },


static const ConvertCSP funcList[] = {
    FUNC_AVX2(QSV_ENC_CSP_YUY2, QSV_ENC_CSP_NV12, false, convert_yuy2_to_nv12_avx2,     convert_yuy2_to_nv12_i_avx2,   AVX2|AVX)
    FUNC_AVX( QSV_ENC_CSP_YUY2, QSV_ENC_CSP_NV12, false, convert_yuy2_to_nv12_avx,      convert_yuy2_to_nv12_i_avx,    AVX )
    FUNC_SSE( QSV_ENC_CSP_YUY2, QSV_ENC_CSP_NV12, false, convert_yuy2_to_nv12_sse2,     convert_yuy2_to_nv12_i_ssse3,  SSSE3|SSE2 )
    FUNC_SSE( QSV_ENC_CSP_YUY2, QSV_ENC_CSP_NV12, false, convert_yuy2_to_nv12_sse2,     convert_yuy2_to_nv12_i_sse2,   SSE2 )
#if !QSVENC_AUO
    FUNC_AVX2(QSV_ENC_CSP_YV12, QSV_ENC_CSP_NV12, false, convert_yv12_to_nv12_avx2,     convert_yv12_to_nv12_avx2,     AVX2|AVX)
    FUNC_AVX( QSV_ENC_CSP_YV12, QSV_ENC_CSP_NV12, false, convert_yv12_to_nv12_avx,      convert_yv12_to_nv12_avx,      AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12, QSV_ENC_CSP_NV12, false, convert_yv12_to_nv12_sse2,     convert_yv12_to_nv12_sse2,     SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12, QSV_ENC_CSP_NV12, true,  convert_uv_yv12_to_nv12_avx2,  convert_uv_yv12_to_nv12_avx2,  AVX2|AVX )
    FUNC_AVX( QSV_ENC_CSP_YV12, QSV_ENC_CSP_NV12, true,  convert_uv_yv12_to_nv12_avx,   convert_uv_yv12_to_nv12_avx,   AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12, QSV_ENC_CSP_NV12, true,  convert_uv_yv12_to_nv12_sse2,  convert_uv_yv12_to_nv12_sse2,  SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_RGB3, QSV_ENC_CSP_RGB4, false, convert_rgb3_to_rgb4_avx2,     convert_rgb3_to_rgb4_avx2,     AVX2|AVX )
    FUNC_AVX( QSV_ENC_CSP_RGB3, QSV_ENC_CSP_RGB4, false, convert_rgb3_to_rgb4_avx,      convert_rgb3_to_rgb4_avx,      AVX )
    FUNC_SSE( QSV_ENC_CSP_RGB3, QSV_ENC_CSP_RGB4, false, convert_rgb3_to_rgb4_ssse3,    convert_rgb3_to_rgb4_ssse3,    SSSE3|SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_RGB4, QSV_ENC_CSP_RGB4, false, convert_rgb4_to_rgb4_avx2,     convert_rgb4_to_rgb4_avx2,     AVX2|AVX )
    FUNC_AVX( QSV_ENC_CSP_RGB4, QSV_ENC_CSP_RGB4, false, convert_rgb4_to_rgb4_avx,      convert_rgb4_to_rgb4_avx,      AVX )
    FUNC_SSE( QSV_ENC_CSP_RGB4, QSV_ENC_CSP_RGB4, false, convert_rgb4_to_rgb4_sse2,     convert_rgb4_to_rgb4_sse2,     SSE2 )
 
    FUNC_AVX2(QSV_ENC_CSP_YV12,      QSV_ENC_CSP_P010, false,  convert_yv12_to_p010_avx2,    convert_yv12_to_p010_avx2   ,AVX2|AVX )
    FUNC_AVX( QSV_ENC_CSP_YV12,      QSV_ENC_CSP_P010, false,  convert_yv12_to_p010_avx,     convert_yv12_to_p010_avx    ,AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12,      QSV_ENC_CSP_P010, false,  convert_yv12_to_p010_sse2,    convert_yv12_to_p010_sse2   ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_16,   QSV_ENC_CSP_NV12, false, convert_yv12_16_to_nv12_avx2, convert_yv12_16_to_nv12_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_16,   QSV_ENC_CSP_NV12, false, convert_yv12_16_to_nv12_sse2, convert_yv12_16_to_nv12_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_14,   QSV_ENC_CSP_NV12, false, convert_yv12_14_to_nv12_avx2, convert_yv12_14_to_nv12_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_14,   QSV_ENC_CSP_NV12, false, convert_yv12_14_to_nv12_sse2, convert_yv12_14_to_nv12_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_12,   QSV_ENC_CSP_NV12, false, convert_yv12_12_to_nv12_avx2, convert_yv12_12_to_nv12_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_12,   QSV_ENC_CSP_NV12, false, convert_yv12_12_to_nv12_sse2, convert_yv12_12_to_nv12_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_10,   QSV_ENC_CSP_NV12, false, convert_yv12_10_to_nv12_avx2, convert_yv12_10_to_nv12_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_10,   QSV_ENC_CSP_NV12, false, convert_yv12_10_to_nv12_sse2, convert_yv12_10_to_nv12_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_09,   QSV_ENC_CSP_NV12, false, convert_yv12_09_to_nv12_avx2, convert_yv12_09_to_nv12_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_09,   QSV_ENC_CSP_NV12, false, convert_yv12_09_to_nv12_sse2, convert_yv12_09_to_nv12_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_16,   QSV_ENC_CSP_P010, false, convert_yv12_16_to_p010_avx2, convert_yv12_16_to_p010_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_16,   QSV_ENC_CSP_P010, false, convert_yv12_16_to_p010_sse2, convert_yv12_16_to_p010_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_14,   QSV_ENC_CSP_P010, false, convert_yv12_14_to_p010_avx2, convert_yv12_14_to_p010_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_14,   QSV_ENC_CSP_P010, false, convert_yv12_14_to_p010_sse2, convert_yv12_14_to_p010_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_12,   QSV_ENC_CSP_P010, false, convert_yv12_12_to_p010_avx2, convert_yv12_12_to_p010_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_12,   QSV_ENC_CSP_P010, false, convert_yv12_12_to_p010_sse2, convert_yv12_12_to_p010_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_10,   QSV_ENC_CSP_P010, false, convert_yv12_10_to_p010_sse2, convert_yv12_10_to_p010_sse2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_10,   QSV_ENC_CSP_P010, false, convert_yv12_10_to_p010_sse2, convert_yv12_10_to_p010_sse2 ,SSE2 )
    FUNC_AVX2(QSV_ENC_CSP_YV12_09,   QSV_ENC_CSP_P010, false, convert_yv12_09_to_p010_avx2, convert_yv12_09_to_p010_avx2 ,AVX2|AVX )
    FUNC_SSE( QSV_ENC_CSP_YV12_09,   QSV_ENC_CSP_P010, false, convert_yv12_09_to_p010_sse2, convert_yv12_09_to_p010_sse2 ,SSE2 )
#endif
};

const ConvertCSP *get_convert_csp_func(QSV_ENC_CSP csp_from, QSV_ENC_CSP csp_to, bool uv_only) {
    unsigned int availableSIMD = get_availableSIMD();
    const ConvertCSP *convert = nullptr;
    for (int i = 0; i < _countof(funcList); i++) {
        if (csp_from != funcList[i].csp_from)
            continue;
        
        if (csp_to != funcList[i].csp_to)
            continue;
        
        if (uv_only != funcList[i].uv_only)
            continue;
        
        if (funcList[i].simd != (availableSIMD & funcList[i].simd))
            continue;

        convert = &funcList[i];
        break;
    }
    return convert;
}

const TCHAR *get_simd_str(unsigned int simd) {
    static std::vector<std::pair<uint32_t, const TCHAR*>> simd_str_list = {
        { AVX2,  _T("AVX2")   },
        { AVX,   _T("AVX")    },
        { SSE42, _T("SSE4.2") },
        { SSE41, _T("SSE4.2") },
        { SSSE3, _T("SSSE3")  },
        { SSE2,  _T("SSE2")   },
    };
    for (auto simd_str : simd_str_list) {
        if (simd_str.first & simd)
            return simd_str.second;
    }
    return _T("-");
}

QSV_ENC_CSP mfx_fourcc_to_qsv_enc_csp(uint32_t fourcc) {
    switch (fourcc) {
    case MFX_FOURCC_NV12: return QSV_ENC_CSP_NV12;
    case MFX_FOURCC_YV12: return QSV_ENC_CSP_YV12;
    case MFX_FOURCC_YUY2: return QSV_ENC_CSP_YUY2;
    case MFX_FOURCC_P010: return QSV_ENC_CSP_P010;
    case MFX_FOURCC_P210: return QSV_ENC_CSP_P210;
    case MFX_FOURCC_RGB3: return QSV_ENC_CSP_RGB3;
    case MFX_FOURCC_RGB4: return QSV_ENC_CSP_RGB4;
    default: return QSV_ENC_CSP_NA;
    }
}
