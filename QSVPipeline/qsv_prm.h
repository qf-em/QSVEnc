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
// --------------------------------------------------------------------------------------------

#ifndef _QSV_PRM_H_
#define _QSV_PRM_H_

#include <climits>
#include <vector>
#include "qsv_version.h"
#include "qsv_util.h"
#include "mfxcommon.h"
#include "mfxvp8.h"
#include "mfxvp9.h"
#include "convert_csp.h"

enum {
    QSV_LOG_TRACE = -3,
    QSV_LOG_DEBUG = -2,
    QSV_LOG_MORE = -1,
    QSV_LOG_INFO = 0,
    QSV_LOG_WARN,
    QSV_LOG_ERROR,
    QSV_LOG_QUIET,
};

enum {
    QSV_RESAMPLER_SWR,
    QSV_RESAMPLER_SOXR,
};

enum {
    MFX_DEINTERLACE_NONE        = 0,
    MFX_DEINTERLACE_NORMAL      = 1,
    MFX_DEINTERLACE_IT          = 2, //inverse telecine, to 24fps
    MFX_DEINTERLACE_BOB         = 3,
    MFX_DEINTERLACE_IT_MANUAL   = 4, //inverse telecine, manual select
    MFX_DEINTERLACE_AUTO_SINGLE = 5,
    MFX_DEINTERLACE_AUTO_DOUBLE = 6,
};

enum {
    MVC_DISABLED          = 0x0,
    MVC_ENABLED           = 0x1,
    MVC_VIEWOUTPUT        = 0x2,    // 2 output bitstreams
};

enum {
    INPUT_FMT_AUTO = 0,
    INPUT_FMT_RAW,
    INPUT_FMT_Y4M,
    INPUT_FMT_AVI,
    INPUT_FMT_AVS,
    INPUT_FMT_VPY,
    INPUT_FMT_VPY_MT,
    INPUT_FMT_AVCODEC_QSV,
    INPUT_FMT_AVCODEC_SW,
    INPUT_FMT_AVCODEC_ANY,
    INPUT_FMT_AUO,
};

static const int QSV_ENC_CSP_TO_MFX_FOURCC[] = {
    0, //QSV_ENC_CSP_NA
    MFX_FOURCC_NV12, //QSV_ENC_CSP_NV12
    MFX_FOURCC_YV12, //QSV_ENC_CSP_YV12
    MFX_FOURCC_YUY2, //QSV_ENC_CSP_YUY2 
    0, //QSV_ENC_CSP_YUV422
    0, //QSV_ENC_CSP_YUV444
    MFX_FOURCC_P010, //QSV_ENC_CSP_YV12_09
    MFX_FOURCC_P010,
    MFX_FOURCC_P010,
    MFX_FOURCC_P010,
    MFX_FOURCC_P010, //QSV_ENC_CSP_YV12_16
    MFX_FOURCC_P010, //QSV_ENC_CSP_P010
    MFX_FOURCC_P210, //QSV_ENC_CSP_P210
    0, //QSV_ENC_CSP_YUV444_09
    0,
    0,
    0,
    0, //QSV_ENC_CSP_YUV444_16
    MFX_FOURCC_RGB3,
    MFX_FOURCC_RGB4,
    0 //QSV_ENC_CSP_YC48
};

enum MemType {
    SYSTEM_MEMORY = 0x00,
    VA_MEMORY     = 0x01,
    D3D9_MEMORY   = 0x01,
    D3D11_MEMORY  = 0x02,
    HW_MEMORY     = D3D9_MEMORY | D3D11_MEMORY,
};

static MemType operator|(MemType a, MemType b) {
    return (MemType)((uint32_t)a | (uint32_t)b);
}

static MemType operator|=(MemType& a, MemType b) {
    a = a | b;
    return a;
}

static MemType operator&(MemType a, MemType b) {
    return (MemType)((uint32_t)a & (uint32_t)b);
}

static MemType operator&=(MemType& a, MemType b) {
    a = (MemType)((uint32_t)a & (uint32_t)b);
    return a;
}

static const int8_t QSV_OUTPUT_THREAD_AUTO = -1;
static const int8_t QSV_AUDIO_THREAD_AUTO = -1;
static const int8_t QSV_INPUT_THREAD_AUTO = -1;

typedef struct {
    mfxU16 left, up, right, bottom;
} sInputCrop;

typedef struct {
    int start, fin;
} sTrim;

typedef struct {
    std::vector<sTrim> list;
    int offset;
} sTrimParam;

typedef  std::vector<std::pair<tstring, tstring>> muxOptList;

static const int TRIM_MAX = INT_MAX;
static const int TRIM_OVERREAD_FRAMES = 128;

static bool inline frame_inside_range(int frame, const std::vector<sTrim>& trimList) {
    if (trimList.size() == 0)
        return true;
    if (frame < 0)
        return false;
    for (auto trim : trimList) {
        if (trim.start <= frame && frame <= trim.fin) {
            return true;
        }
    }
    return false;
}

static bool inline rearrange_trim_list(int frame, int offset, std::vector<sTrim>& trimList) {
    if (trimList.size() == 0)
        return true;
    if (frame < 0)
        return false;
    for (uint32_t i = 0; i < trimList.size(); i++) {
        if (trimList[i].start >= frame) {
            trimList[i].start = clamp(trimList[i].start + offset, 0, TRIM_MAX);
        }
        if (trimList[i].fin && trimList[i].fin >= frame) {
            trimList[i].fin = (int)clamp((int64_t)trimList[i].fin + offset, 0, (int64_t)TRIM_MAX);
        }
    }
    return false;
}

enum {
    FPS_CONVERT_NONE = 0,
    FPS_CONVERT_MUL2,
    FPS_CONVERT_MUL2_5,
};

enum QSVAVSync : uint32_t {
    QSV_AVSYNC_THROUGH   = 0x00,
    QSV_AVSYNC_INIT      = 0x01,
    QSV_AVSYNC_CHECK_PTS = 0x02,
    QSV_AVSYNC_VFR       = 0x02,
    QSV_AVSYNC_FORCE_CFR = 0x04 | QSV_AVSYNC_CHECK_PTS,
};

static const int CHECK_PTS_MAX_INSERT_FRAMES = 8;

enum {
    QSVENC_MUX_NONE     = 0x00,
    QSVENC_MUX_VIDEO    = 0x01,
    QSVENC_MUX_AUDIO    = 0x02,
    QSVENC_MUX_SUBTITLE = 0x04,
};

enum {
    QSV_VPP_SUB_SIMPLE = 0,
    QSV_VPP_SUB_COMPLEX,
};

static const uint32_t MAX_SPLIT_CHANNELS = 32;
static const uint64_t QSV_CHANNEL_AUTO = UINT64_MAX;

template <uint32_t size>
static bool bSplitChannelsEnabled(uint64_t (&pnStreamChannels)[size]) {
    bool bEnabled = false;
    for (uint32_t i = 0; i < size; i++) {
        bEnabled |= pnStreamChannels[i] != 0;
    }
    return bEnabled;
}

template <uint32_t size>
static void setSplitChannelAuto(uint64_t (&pnStreamChannels)[size]) {
    for (uint32_t i = 0; i < size; i++) {
        pnStreamChannels[i] = ((uint64_t)1) << i;
    }
}

template <uint32_t size>
static bool isSplitChannelAuto(uint64_t (&pnStreamChannels)[size]) {
    bool isAuto = true;
    for (uint32_t i = 0; isAuto && i < size; i++) {
        isAuto &= (pnStreamChannels[i] == (((uint64_t)1) << i));
    }
    return isAuto;
}

typedef struct {
    bool bEnable;             //use vpp

    bool bUseResize;          //use vpp resizer

    bool __unsed2;
    bool __unsed;
    mfxU16 nRotate;      
    bool bUseProAmp;          //not supported
    bool bUseDenoise;         //use vpp denoise
    mfxU16 nDenoise;          // 0 - 100 Denoise Strength
    bool bUseDetailEnhance;   //use vpp detail enhancer
    bool __unsed4;
    mfxU16 nDetailEnhance;    // 0 - 100 
    mfxU16 nDeinterlace;      //set deinterlace mode

    mfxU16 nImageStabilizer;  //MFX_IMAGESTAB_MODE_UPSCALE, MFX_IMAGESTAB_MODE_BOXED
    mfxU16 nFPSConversion;    //FPS_CONVERT_xxxx

    mfxU16 nTelecinePattern;

    bool bHalfTurn;
    bool __unsed3;

    struct {
        TCHAR     *pFilePath; //ロゴファイル名へのポインタ
        TCHAR     *pSelect; //選択するロゴ
        mfxI16Pair nPosOffset;
        uint8_t    nDepth;
        uint8_t    bAdd;
        mfxI16     nYOffset;
        mfxI16     nCbOffset;
        mfxI16     nCrOffset;
    } delogo;

    struct {
        int    nTrack;    //動画ファイルから字幕を抽出する場合の字幕トラック (0で無効)
        TCHAR *pFilePath; //字幕を別ファイルから読み込む場合のファイルの場所
        TCHAR *pCharEnc;  //字幕の文字コード
        int    nShaping;  //字幕を焼きこむときのモード
    } subburn;

    mfxU16 nMirrorType;  //MFX_MIRRORING_xxx
    mfxU16 nScalingQuality; //MFX_SCALING_MODE_xxx
    mfxU8 Reserved[84];
} sVppParams;

typedef struct sAudioSelect {
    int    nAudioSelect;      //選択した音声トラックのリスト 1,2,...(1から連番で指定)
    TCHAR *pAVAudioEncodeCodec; //音声エンコードのコーデック
    int    nAVAudioEncodeBitrate; //音声エンコードに選択した音声トラックのビットレート
    int    nAudioSamplingRate;    //サンプリング周波数
    TCHAR *pAudioExtractFilename; //抽出する音声のファイル名のリスト
    TCHAR *pAudioExtractFormat; //抽出する音声ファイルのフォーマット
    TCHAR *pAudioFilter; //音声フィルタ
    uint64_t pnStreamChannelSelect[MAX_SPLIT_CHANNELS]; //入力音声の使用するチャンネル
    uint64_t pnStreamChannelOut[MAX_SPLIT_CHANNELS];    //出力音声のチャンネル
} sAudioSelect;

struct sInputParams
{
    mfxU16 nInputFmt;     // 0 - raw, 1 - y4m, 2 - avi/avs
    mfxU16 nEncMode;      // RateControl
    mfxU16 nTargetUsage;  // Quality
    mfxU32 CodecId;       // H.264 only for this
    mfxU16 CodecProfile; 
    mfxU16 CodecLevel;
    mfxU16 nIdrInterval;  // Idr frame interval to I frame, not supported
    mfxU16 nGOPLength;    // (Max) GOP Length
    bool   bopenGOP;      // if false, GOP_CLOSED is set
    bool   bforceGOPSettings; // if true, GOP_STRICT is set
    mfxI16 nBframes;      // set sequential Bframes num, -1 is auto.
    mfxU16 nRef;          // set ref frames num.
    mfxU16 nQPI;          // QP for I frames
    mfxU16 nQPP;          // QP for P frames
    mfxU16 nQPB;          // QP for B frames
    mfxU16 nAVBRAccuarcy;    // param for AVBR algorithm, for API v1.3
    mfxU16 nAVBRConvergence; // param for AVBR algorithm, for API v1.3

    mfxU16 nSlices;       // number of slices, 0 is auto

    mfxU16 VideoFormat;   //for API v1.3
    mfxU16 ColorMatrix;   //for API v1.3
    mfxU16 ColorPrim;     //for API v1.3
    mfxU16 Transfer;      //for API v1.3
    bool   bFullrange;    //for API v1.3

    mfxU32 ColorFormat;   //YV12 or NV12
    mfxU16 nPicStruct;    //Progressive or interlaced, and other flags
    mfxU16 nWidth;        //width of input
    mfxU16 nHeight;       //height of input
    mfxU32 nFPSRate;      //fps rate of input
    mfxU32 nFPSScale;     //fps scale of input
    mfxU16 __nBitRate;
    mfxU16 __nMaxBitrate;
    mfxI16 nLogLevel;     //ログレベル

    mfxU16 nDstWidth;     //output width 
    mfxU16 nDstHeight;    //input width

    mfxU8 memType;       //use d3d surface
    bool bUseHWLib;       //use QSV (hw encoding)

    mfxU16 nInputBufSize; //input buf size

    bool   bIsMVC; // true if Multi-View-Codec is in use
    mfxU32 numViews; // number of views for Multi-View-Codec
    

    mfxI32     nPAR[2]; //PAR比
    bool       bCAVLC;  //CAVLC
    mfxU16     nInterPred;
    mfxU16     nIntraPred;
    bool       bRDO;
    mfxU16     nMVPrecision;
    mfxI16Pair MVSearchWindow;

    mfxU16     MVC_flags;
    mfxU8      nBluray;

    mfxU16     nVQPStrength;
    mfxU16     nVQPSensitivity;

    sInputCrop sInCrop;

    mfxU16     nQuality; // quality parameter for JPEG encoder

    mfxU8      bMBBRC;
    mfxU8      bExtBRC;

    mfxU16     nLookaheadDepth;
    mfxU16     nTrellis;

    TCHAR     *pStrLogFile; //ログファイル名へのポインタ
    mfxU16     nAsyncDepth;
    mfxI16     nOutputBufSizeMB;

    mfxU16     bBPyramid;
    mfxU8      bAdaptiveI;
    mfxU8      bAdaptiveB;
    mfxU16     nLookaheadDS;

    mfxU16     nICQQuality;
    mfxU8      bBenchmark;

    mfxU8      bDisableTimerPeriodTuning;
    
    mfxU16     nQVBRQuality;

    mfxU8      bIntraRefresh;
    mfxU8      bNoDeblock;
    mfxU8      nQPMin[3];
    mfxU8      nQPMax[3];

    mfxU16     nWinBRCSize;
    
    mfxU8      nMVCostScaling;
    mfxU8      bDirectBiasAdjust;
    mfxU8      bGlobalMotionAdjust;
    mfxU8      bUseFixedFunc;
    mfxU32     nBitRate;
    mfxU32     nMaxBitrate;

    mfxU16     nTrimCount;
    sTrim     *pTrimList;
    mfxU16     inputBitDepthLuma;
    mfxU16     inputBitDepthChroma;
    mfxU8      nAVMux; //QSVENC_MUX_xxx
    mfxU16     nAVDemuxAnalyzeSec;

    TCHAR     *pAVMuxOutputFormat;
    
    mfxU8      nAudioSelectCount; //pAudioSelectの数
    sAudioSelect **ppAudioSelectList;

    mfxU16     nSessionThreads;
    mfxU16     nSessionThreadPriority;

    mfxU8      bCopyChapter;
    mfxU8      nAudioResampler;
    mfxU8      nVP8Sharpness;
    mfxU8      nAudioSourceCount;
    TCHAR      **ppAudioSourceList;

    mfxU16     nWeightP;
    mfxU16     nWeightB;
    mfxU16     nFadeDetect;
    mfxU16     nSubtitleSelectCount;
    int       *pSubtitleSelect;
    int64_t    nPerfMonitorSelect;
    int64_t    nPerfMonitorSelectMatplot;
    int        nPerfMonitorInterval;
    TCHAR     *pPythonPath;
    mfxU32     nBenchQuality; //ベンチマークの対象
    int8_t     nOutputThread;
    int8_t     nAudioThread;

    muxOptList *pMuxOpt;
    TCHAR     *pChapterFile;
    uint32_t   nAudioIgnoreDecodeError;
    QSVAVSync  nAVSyncMode;     //avsyncの方法 (QSV_AVSYNC_xxx)
    uint16_t   nProcSpeedLimit; //プリデコードする場合の処理速度制限 (0で制限なし)
    int8_t     nInputThread;
    int8_t     bAudioIgnoreNoTrackError;
    float      fSeekSec; //指定された秒数分先頭を飛ばす
    TCHAR     *pFramePosListLog;
    uint32_t   nFallback;
    int        nVideoStreamId;
    int8_t     nVideoTrack;
    int8_t     bOutputAud;
    int8_t     bOutputPicStruct;
    int8_t     bChapterNoTrim;
    int16_t    pQPOffset[8];
    TCHAR     *pMuxVidTsLogFile;
    TCHAR     *pAVInputFormat;
    TCHAR     *pLogCopyFrameData;
    int8_t     Reserved[1030];

    TCHAR strSrcFile[MAX_FILENAME_LEN];
    TCHAR strDstFile[MAX_FILENAME_LEN];

    mfxU16 nRotationAngle; //not supported

    sVppParams vpp;
};

const int MFX_COLOR_VALUE_AUTO = 0x0000ffff; //max of 16bit-integer (unsigned)

const CX_DESC list_empty[] = { 
    { NULL, 0 }
};
const CX_DESC list_codec[] = {
    { _T("h264"),     MFX_CODEC_AVC   },
    { _T("hevc"),     MFX_CODEC_HEVC  },
    { _T("mpeg2"),    MFX_CODEC_MPEG2 },
    { _T("vc-1"),     MFX_CODEC_VC1   },
    { _T("vp8"),      MFX_CODEC_VP8   },
    { _T("raw"),      MFX_CODEC_RAW   },
    { NULL, 0 }
};

const CX_DESC list_rc_mode[] = {
    { _T("CBR"),     MFX_RATECONTROL_CBR    },
    { _T("VBR"),     MFX_RATECONTROL_VBR    },
    { _T("CQP"),     MFX_RATECONTROL_CQP    },
    { _T("VQP"),     MFX_RATECONTROL_VQP    },
    { _T("AVBR"),    MFX_RATECONTROL_AVBR   },
    { _T("LA"),      MFX_RATECONTROL_LA     },
    { _T("LA-EXT"),  MFX_RATECONTROL_LA_EXT },
    { _T("LA-HRD"),  MFX_RATECONTROL_LA_HRD },
    { _T("LA-ICQ"),  MFX_RATECONTROL_LA_ICQ },
    { _T("ICQ"),     MFX_RATECONTROL_ICQ    },
    { _T("QVBR"),    MFX_RATECONTROL_QVBR   },
    { _T("VCM"),     MFX_RATECONTROL_VCM    },
    { NULL, 0 }
};

const CX_DESC list_avc_profile[] = {
    { _T("auto"),     0                        },
    { _T("Baseline"), MFX_PROFILE_AVC_BASELINE },
    { _T("Main"),     MFX_PROFILE_AVC_MAIN     },
    { _T("High"),     MFX_PROFILE_AVC_HIGH     },
    { NULL, 0 }
};

const CX_DESC list_hevc_profile[] = {
    { _T("auto"),     0                       },
    { _T("main"),     MFX_PROFILE_HEVC_MAIN   },
    { _T("main10"),   MFX_PROFILE_HEVC_MAIN10 },
    //{ _T("mainsp"),   MFX_PROFILE_HEVC_MAINSP },
    { NULL, 0 }
};

const CX_DESC list_mpeg2_profile[] = {
    { _T("auto"),     0                        },
    { _T("Simple"),   MFX_PROFILE_MPEG2_SIMPLE },
    { _T("Main"),     MFX_PROFILE_MPEG2_MAIN   },
    { _T("High"),     MFX_PROFILE_MPEG2_HIGH   },
    { NULL, 0 }
};

const CX_DESC list_vc1_profile[] = {
    { _T("auto"),     0                        },
    { _T("Simple"),   MFX_PROFILE_VC1_SIMPLE   },
    { _T("Main"),     MFX_PROFILE_VC1_MAIN     },
    { _T("Advanced"), MFX_PROFILE_VC1_ADVANCED },
    { NULL, 0 }
};

const CX_DESC list_vp8_profile[] = {
    { _T("auto"),     0 },
    { _T("0"),        MFX_PROFILE_VP8_0 },
    { _T("1"),        MFX_PROFILE_VP8_1 },
    { _T("2"),        MFX_PROFILE_VP8_2 },
    { _T("3"),        MFX_PROFILE_VP8_3 },
    { NULL, 0 }
};

const CX_DESC list_vp9_profile[] = {
    { _T("auto"),     0 },
    { _T("0"),        MFX_PROFILE_VP9_0 },
    { _T("1"),        MFX_PROFILE_VP9_1 },
    { _T("2"),        MFX_PROFILE_VP9_2 },
    { _T("3"),        MFX_PROFILE_VP9_3 },
    { NULL, 0 }
};

const CX_DESC list_interlaced[] = {
    { _T("progressive"),     MFX_PICSTRUCT_PROGRESSIVE },
    { _T("interlaced(tff)"), MFX_PICSTRUCT_FIELD_TFF   },
    { _T("interlaced(bff)"), MFX_PICSTRUCT_FIELD_BFF   },
    { NULL, 0 }
};

const CX_DESC list_deinterlace[] = {
    { _T("none"),      MFX_DEINTERLACE_NONE        },
    { _T("normal"),    MFX_DEINTERLACE_NORMAL      },
    { _T("it"),        MFX_DEINTERLACE_IT          },
    { _T("bob"),       MFX_DEINTERLACE_BOB         },
#if ENABLE_ADVANCED_DEINTERLACE
    { _T("it-manual"), MFX_DEINTERLACE_IT_MANUAL   },
    { _T("auto"),      MFX_DEINTERLACE_AUTO_SINGLE },
    { _T("auto-bob"),  MFX_DEINTERLACE_AUTO_DOUBLE },
#endif
    { NULL, 0 }
};

const CX_DESC list_telecine_patterns[] = {
    { _T("32"),     MFX_TELECINE_PATTERN_32 },
    { _T("2332"),   MFX_TELECINE_PATTERN_2332 },
    { _T("repeat"), MFX_TELECINE_PATTERN_FRAME_REPEAT },
    { _T("41"),     MFX_TELECINE_PATTERN_41 },
    { NULL, 0 }
};

const CX_DESC list_avc_level[] = { 
    { _T("auto"), 0                 },
    { _T("1"),    MFX_LEVEL_AVC_1   },
    { _T("1b"),   MFX_LEVEL_AVC_1b  },
    { _T("1.1"),  MFX_LEVEL_AVC_11  },
    { _T("1.2"),  MFX_LEVEL_AVC_12  },
    { _T("1.3"),  MFX_LEVEL_AVC_13  },
    { _T("2"),    MFX_LEVEL_AVC_2   },
    { _T("2.1"),  MFX_LEVEL_AVC_21  },
    { _T("2.2"),  MFX_LEVEL_AVC_22  },
    { _T("3"),    MFX_LEVEL_AVC_3   },
    { _T("3.1"),  MFX_LEVEL_AVC_31  },
    { _T("3.2"),  MFX_LEVEL_AVC_32  },
    { _T("4"),    MFX_LEVEL_AVC_4   },
    { _T("4.1"),  MFX_LEVEL_AVC_41  },
    { _T("4.2"),  MFX_LEVEL_AVC_42  },
    { _T("5"),    MFX_LEVEL_AVC_5   },
    { _T("5.1"),  MFX_LEVEL_AVC_51  },
    { _T("5.2"),  MFX_LEVEL_AVC_52  },
    { NULL, 0 }
};
const CX_DESC list_hevc_level[] = { 
    { _T("auto"), 0                 },
    { _T("1"),    MFX_LEVEL_HEVC_1   },
    { _T("2"),    MFX_LEVEL_HEVC_2   },
    { _T("2.1"),  MFX_LEVEL_HEVC_21  },
    { _T("3"),    MFX_LEVEL_HEVC_3   },
    { _T("3.1"),  MFX_LEVEL_HEVC_31  },
    { _T("4"),    MFX_LEVEL_HEVC_4   },
    { _T("4.1"),  MFX_LEVEL_HEVC_41  },
    { _T("5"),    MFX_LEVEL_HEVC_5   },
    { _T("5.1"),  MFX_LEVEL_HEVC_51  },
    { _T("5.2"),  MFX_LEVEL_HEVC_52  },
    { _T("6"),    MFX_LEVEL_HEVC_6   },
    { _T("6.1"),  MFX_LEVEL_HEVC_61  },
    { _T("6.2"),  MFX_LEVEL_HEVC_62  },
    { NULL, 0 }
};
const CX_DESC list_mpeg2_level[] = { 
    { _T("auto"),     0                        },
    { _T("low"),      MFX_LEVEL_MPEG2_LOW      },
    { _T("main"),     MFX_LEVEL_MPEG2_MAIN     },
    { _T("high"),     MFX_LEVEL_MPEG2_HIGH     },
    { _T("high1440"), MFX_LEVEL_MPEG2_HIGH1440 },
    { NULL, 0 }
};
const CX_DESC list_vc1_level[] = { 
    { _T("auto"),     0                    },
    { _T("low"),      MFX_LEVEL_VC1_LOW    },
    { _T("median"),   MFX_LEVEL_VC1_MEDIAN },
    { _T("high"),     MFX_LEVEL_VC1_HIGH   },
    { NULL, 0 }
};
const CX_DESC list_vc1_level_adv[] = { 
    { _T("auto"),  0               },
    { _T("0"),     MFX_LEVEL_VC1_0 },
    { _T("1"),     MFX_LEVEL_VC1_1 },
    { _T("2"),     MFX_LEVEL_VC1_2 },
    { _T("3"),     MFX_LEVEL_VC1_3 },
    { _T("4"),     MFX_LEVEL_VC1_4 },
    { NULL, 0 }
};
const CX_DESC list_vp8_level[] = { 
    { _T("auto"),     0                    },
    { NULL, 0 }
};
const CX_DESC list_vp9_level[] = { 
    { _T("auto"),     0                    },
    { NULL, 0 }
};
const CX_DESC list_avc_trellis[] = {
    { _T("Auto"),           MFX_TRELLIS_UNKNOWN },
    { _T("off"),            MFX_TRELLIS_OFF },
    { _T("for I frames"),   MFX_TRELLIS_I   },
    { _T("for I,P frames"), MFX_TRELLIS_I | MFX_TRELLIS_P },
    { _T("for All frames"), MFX_TRELLIS_I | MFX_TRELLIS_P | MFX_TRELLIS_B },
    { NULL, 0 }
};
const CX_DESC list_avc_trellis_for_options[] = { 
    { _T("auto"), MFX_TRELLIS_UNKNOWN },
    { _T("off"),  MFX_TRELLIS_OFF },
    { _T("i"),    MFX_TRELLIS_I   },
    { _T("ip"),   MFX_TRELLIS_I | MFX_TRELLIS_P },
    { _T("all"),  MFX_TRELLIS_I | MFX_TRELLIS_P | MFX_TRELLIS_B },
    { _T("ipb"),  MFX_TRELLIS_I | MFX_TRELLIS_P | MFX_TRELLIS_B },
    { _T("ib"),   MFX_TRELLIS_I | MFX_TRELLIS_B },
    { _T("p"),    MFX_TRELLIS_P },
    { _T("pb"),   MFX_TRELLIS_P | MFX_TRELLIS_B },
    { _T("b"),    MFX_TRELLIS_B },
    { NULL, 0 }
};

const CX_DESC list_lookahead_ds[] = {
    { _T("auto"),   MFX_LOOKAHEAD_DS_UNKNOWN },
    { _T("slow"),   MFX_LOOKAHEAD_DS_OFF     },
    { _T("medium"), MFX_LOOKAHEAD_DS_2x      },
    { _T("fast"),   MFX_LOOKAHEAD_DS_4x      },
    { NULL, 0 }
};

const CX_DESC list_mv_cost_scaling[] = {
    { _T("default"), -1 },
    { _T("0"),        0 },
    { _T("1"),        1 },
    { _T("2"),        2 },
    { _T("3"),        3 },
    { _T("4"),        4 },
    { NULL, 0 }
};

const CX_DESC list_log_level[] = {
    { _T("trace"), QSV_LOG_TRACE },
    { _T("debug"), QSV_LOG_DEBUG },
    { _T("more"),  QSV_LOG_MORE  },
    { _T("info"),  QSV_LOG_INFO  },
    { _T("warn"),  QSV_LOG_WARN  },
    { _T("error"), QSV_LOG_ERROR },
    { NULL, 0 }
};

const CX_DESC list_avsync[] = {
    { _T("through"),  QSV_AVSYNC_THROUGH   },
    { _T("forcecfr"), QSV_AVSYNC_FORCE_CFR },
    { NULL, 0 }
};

const CX_DESC list_priority[] = {
    { _T("low"),    MFX_PRIORITY_LOW    },
    { _T("normal"), MFX_PRIORITY_NORMAL },
    { _T("high"),   MFX_PRIORITY_HIGH   },
};

const CX_DESC list_resampler[] = {
    { _T("swr"),  QSV_RESAMPLER_SWR  },
    { _T("soxr"), QSV_RESAMPLER_SOXR },
    { NULL, 0 }
};

enum {
    QSV_AUD_ENC_NONE = -1,
    QSV_AUD_ENC_COPY = 0,
    QSV_AUD_ENC_AAC,
    QSV_AUD_ENC_MP3,
    QSV_AUD_ENC_MP2,
    QSV_AUD_ENC_VORBIS,
    QSV_AUD_ENC_WAV,
    QSV_AUD_ENC_OPUS,
    QSV_AUD_ENC_AC3,
};

const CX_DESC list_avqsv_aud_encoder[] = {
    { _T("none"),       QSV_AUD_ENC_NONE },
    { _T("copy"),       QSV_AUD_ENC_COPY },
    { _T("aac"),        QSV_AUD_ENC_AAC },
    { _T("libmp3lame"), QSV_AUD_ENC_MP3 },
    { _T("libtwolame"), QSV_AUD_ENC_MP2 },
    { _T("libvorbis"),  QSV_AUD_ENC_VORBIS },
    { _T("pcm_s16le"),  QSV_AUD_ENC_WAV },
    { _T("libopus"),    QSV_AUD_ENC_OPUS },
    { _T("ac3"),        QSV_AUD_ENC_AC3 },
    { NULL, 0 }
};

static inline const CX_DESC *get_level_list(int CodecID) {
    switch (CodecID) {
        case MFX_CODEC_AVC:     return list_avc_level;
        case MFX_CODEC_MPEG2:   return list_mpeg2_level;
        case MFX_CODEC_VC1:     return list_vc1_level;
        case MFX_CODEC_HEVC:    return list_hevc_level;
        case MFX_CODEC_VP8:     return list_vp8_level;
        case MFX_CODEC_VP9:     return list_vp9_level;
        case MFX_CODEC_RAW:     return list_empty;
        case MFX_CODEC_CAPTURE: return list_empty;
        default:                return list_empty;
    }
}

static inline const CX_DESC *get_profile_list(int CodecID) {
    switch (CodecID) {
        case MFX_CODEC_AVC:     return list_avc_profile;
        case MFX_CODEC_MPEG2:   return list_mpeg2_profile;
        case MFX_CODEC_VC1:     return list_vc1_profile;
        case MFX_CODEC_HEVC:    return list_hevc_profile;
        case MFX_CODEC_VP8:     return list_vp8_profile;
        case MFX_CODEC_VP9:     return list_vp9_profile;
        case MFX_CODEC_RAW:     return list_empty;
        case MFX_CODEC_CAPTURE: return list_empty;
        default:                return list_empty;
    }
}

const int HD_HEIGHT_THRESHOLD = 720;
const int HD_INDEX = 2;
const int SD_INDEX = 3;
const CX_DESC list_colorprim[] = {
    { _T("undef"),     2  },
    { _T("auto"),      MFX_COLOR_VALUE_AUTO },
    { _T("bt709"),     1  },
    { _T("smpte170m"), 6  },
    { _T("bt470m"),    4  },
    { _T("bt470bg"),   5  },
    { _T("smpte240m"), 7  },
    { _T("film"),      8  },
    { NULL, 0 }
};
const CX_DESC list_transfer[] = {
    { _T("undef"),     2  },
    { _T("auto"),      MFX_COLOR_VALUE_AUTO },
    { _T("bt709"),     1  },
    { _T("smpte170m"), 6  },
    { _T("bt470m"),    4  },
    { _T("bt470bg"),   5  },
    { _T("smpte240m"), 7  },
    { _T("linear"),    8  },
    { _T("log100"),    9  },
    { _T("log316"),    10 },
    { NULL, 0 }
};
const CX_DESC list_colormatrix[] = {
    { _T("undef"),     2  },
    { _T("auto"),      MFX_COLOR_VALUE_AUTO },
    { _T("bt709"),     1  },
    { _T("smpte170m"), 6  },
    { _T("bt470bg"),   5  },
    { _T("smpte240m"), 7  },
    { _T("YCgCo"),     8  },
    { _T("fcc"),       4  },
    { _T("GBR"),       0  },
    { NULL, 0 }
};
const CX_DESC list_videoformat[] = {
    { _T("undef"),     5  },
    { _T("ntsc"),      2  },
    { _T("component"), 0  },
    { _T("pal"),       1  },
    { _T("secam"),     3  },
    { _T("mac"),       4  },
    { NULL, 0 }
};

//表示用
const CX_DESC list_quality[] = {
    { _T(" 1 - best quality"), MFX_TARGETUSAGE_BEST_QUALITY },
    { _T(" 2 - higher"),       2                            },
    { _T(" 3 - high quality"), 3                            },
    { _T(" 4 - balanced"),     MFX_TARGETUSAGE_BALANCED     },
    { _T(" 5 - fast"),         5                            },
    { _T(" 6 - faster"),       6                            },
    { _T(" 7 - fastest"),      MFX_TARGETUSAGE_BEST_SPEED   },
    { NULL, 0 }
};

//オプション用
const CX_DESC list_quality_for_option[] = {
    { _T("best"),     MFX_TARGETUSAGE_BEST_QUALITY },
    { _T("higher"),   2                            },
    { _T("high"),     3                            },
    { _T("balanced"), MFX_TARGETUSAGE_BALANCED     },
    { _T("fast"),     5                            },
    { _T("faster"),   6                            },
    { _T("fastest"),  MFX_TARGETUSAGE_BEST_SPEED   },
    { NULL, 0 }
};

const CX_DESC list_mv_presicion[] = {
    { _T("Auto"),     MFX_MVPRECISION_UNKNOWN    },
    { _T("full-pel"), MFX_MVPRECISION_INTEGER    },
    { _T("half-pel"), MFX_MVPRECISION_HALFPEL    },
    { _T("Q-pel"),    MFX_MVPRECISION_QUARTERPEL },
    { NULL, 0 }
};

const CX_DESC list_pred_block_size[] = {
    { _T("Auto"),          MFX_BLOCKSIZE_UNKNOWN    },
    { _T("16x16"),         MFX_BLOCKSIZE_MIN_16X16  },
    { _T("8x8"),           MFX_BLOCKSIZE_MIN_8X8    },
    { _T("4x4"),           MFX_BLOCKSIZE_MIN_4X4    },
    { NULL, 0 }
};

const CX_DESC list_vpp_image_stabilizer[] = {
    { _T("none"),    0 },
    { _T("upscale"), MFX_IMAGESTAB_MODE_UPSCALE },
    { _T("box"),     MFX_IMAGESTAB_MODE_BOXING  },
    { NULL, 0 }
};

const CX_DESC list_vpp_fps_conversion[] = {
    { _T("off"),  0 },
    { _T("x2"),   FPS_CONVERT_MUL2 },
    { _T("x2.5"), FPS_CONVERT_MUL2_5  },
    { NULL, 0 }
};

const CX_DESC list_vpp_sub_shaping[] = {
    { _T("simple"),  QSV_VPP_SUB_SIMPLE  },
    { _T("complex"), QSV_VPP_SUB_COMPLEX },
    { NULL, 0 }
};

const CX_DESC list_vpp_rotate_angle[] = {
    { _T("0"),     MFX_ANGLE_0    },
    { _T("90"),    MFX_ANGLE_90   },
    { _T("180"),   MFX_ANGLE_180  },
    { _T("270"),   MFX_ANGLE_270  },
    { NULL, 0 }
};

const CX_DESC list_vpp_mirroring[] = {
    { _T("n"), MFX_MIRRORING_DISABLED   },
    { _T("h"), MFX_MIRRORING_HORIZONTAL },
    { _T("v"), MFX_MIRRORING_VERTICAL   },
    { NULL, 0 }
};

const CX_DESC list_vpp_scaling_quality[] = {
    { _T("auto"),   MFX_SCALING_MODE_DEFAULT  },
    { _T("simple"), MFX_SCALING_MODE_LOWPOWER },
    { _T("fine"),   MFX_SCALING_MODE_QUALITY  },
    { NULL, 0 }
};

static int get_cx_index(const CX_DESC * list, int v) {
    for (int i = 0; list[i].desc; i++)
        if (list[i].value == v)
            return i;
    return 0;
}
static int get_cx_index(const CX_DESC * list, const TCHAR *chr) {
    for (int i = 0; list[i].desc; i++)
        if (0 == _tcsicmp(list[i].desc, chr))
            return i;
    return 0;
}

static int PARSE_ERROR_FLAG = INT_MIN;
static int get_value_from_chr(const CX_DESC *list, const TCHAR *chr) {
    for (int i = 0; list[i].desc; i++)
        if (0 == _tcsicmp(list[i].desc, chr))
            return list[i].value;
    return PARSE_ERROR_FLAG;
}
static const TCHAR *get_chr_from_value(const CX_DESC *list, int v) {
    for (int i = 0; list[i].desc; i++)
        if (list[i].value == v)
            return list[i].desc;
    return _T("unknown");
}

//define defaults
const int QSV_DEFAULT_REF = 0;
const int QSV_DEFAULT_GOP_LEN = 0;
const int QSV_DEFAULT_ICQ = 23;
const int QSV_DEFAULT_QVBR = 23;
const int QSV_DEFAULT_QPI = 24;
const int QSV_DEFAULT_QPP = 26;
const int QSV_DEFAULT_QPB = 27;
const int QSV_BFRAMES_AUTO = -1;
const int QSV_DEFAULT_H264_BFRAMES = 3;
const int QSV_DEFAULT_HEVC_BFRAMES = 3;
const int QSV_DEFAULT_QUALITY = MFX_TARGETUSAGE_BALANCED;
const int QSV_DEFAULT_INPUT_BUF_SW = 1;
const int QSV_DEFAULT_INPUT_BUF_HW = 3;
const int QSV_INPUT_BUF_MIN = 1;
const int QSV_INPUT_BUF_MAX = 16;
const int QSV_DEFAULT_CONVERGENCE = 90;
const int QSV_DEFAULT_ACCURACY = 500;
const int QSV_DEFAULT_FORCE_GOP_LEN = 1;
const int QSV_DEFAULT_OUTPUT_BUF_MB = 8;
const int QSV_OUTPUT_BUF_MB_MAX = 128;
const uint32_t QSV_DEFAULT_BENCH = (1 << 1) | (1 << 4) | (1 << 7);

const mfxU16 QSV_DEFAULT_VQP_STRENGTH = 10;
const mfxU16 QSV_DEFAULT_VQP_SENSITIVITY = 50;
const mfxU16 QSV_DEFAULT_SC_SENSITIVITY = 80;

const mfxU16 QSV_DEFAULT_ASYNC_DEPTH = 4;
const mfxU16 QSV_ASYNC_DEPTH_MAX = 64;
const mfxU16 QSV_SESSION_THREAD_MAX = 64;

const int QSV_LOOKAHEAD_DEPTH_MIN = 10;
const int QSV_LOOKAHEAD_DEPTH_MAX = 100;

const uint32_t QSV_DEFAULT_AUDIO_IGNORE_DECODE_ERROR = 10;

const mfxI16 QSV_DEFAULT_VPP_DELOGO_DEPTH = 128;

const int QSV_DEFAULT_PERF_MONITOR_INTERVAL = 500;

const int QSV_VPP_DENOISE_MIN = 0;
const int QSV_VPP_DENOISE_MAX = 100;
const int QSV_VPP_DETAIL_ENHANCE_MIN = 0;
const int QSV_VPP_DETAIL_ENHANCE_MAX = 100;

void init_qsvp_prm(sInputParams *prm);

#endif //_QSV_PRM_H_
