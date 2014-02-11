﻿//  -----------------------------------------------------------------------------------------
//    QSVEnc by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#include <Windows.h>
#include <Process.h>
#include <Math.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib") 
#include <mmintrin.h>  //イントリンシック命令 SSE
#include <emmintrin.h> //イントリンシック命令 SSE2

#include "sample_defs.h"
#include "sample_utils.h"

#include "output.h"
#include "vphelp_client.h"

#pragma warning( push )
#pragma warning( disable: 4127 )
#include "afs_client.h"
#pragma warning( pop )

#include "convert.h"

#include "auo_util.h"
#include "auo_qsv_link.h"
#include "auo_video.h"
#include "auo_encode.h"
#include "auo_audio_parallel.h"
#include "auo_frm.h"
#include "auo_error.h"
#include "convert.h"

AUO_RESULT aud_parallel_task(const OUTPUT_INFO *oip, PRM_ENC *pe);


static int calc_input_frame_size(int width, int height, int color_format) {
	width = (color_format == CF_RGB) ? (width+3) & ~3 : (width+1) & ~1;
	return width * height * COLORFORMATS[color_format].size;
}

BOOL setup_afsvideo(const OUTPUT_INFO *oip, CONF_GUIEX *conf, PRM_ENC *pe, BOOL auto_afs_disable) {
	//すでに初期化してある または 必要ない
	if (pe->afs_init || pe->video_out_type == VIDEO_OUTPUT_DISABLED || !conf->vid.afs)
		return TRUE;

	const int color_format = CF_YUY2;
	const int frame_size = calc_input_frame_size(oip->w, oip->h, color_format);
	//Aviutl(自動フィールドシフト)からの映像入力
	if (afs_vbuf_setup((OUTPUT_INFO *)oip, conf->vid.afs, frame_size, COLORFORMATS[color_format].FOURCC)) {
		pe->afs_init = TRUE;
		return TRUE;
	} else if (conf->vid.afs && auto_afs_disable) {
		afs_vbuf_release(); //一度解放
		warning_auto_afs_disable();
		conf->vid.afs = FALSE;
		//再度使用するmuxerをチェックする
		pe->muxer_to_be_used = check_muxer_to_be_used(conf, pe->video_out_type, (oip->flag & OUTPUT_INFO_FLAG_AUDIO) != 0);
		return TRUE;
	}
	//エラー
	error_afs_setup(conf->vid.afs, auto_afs_disable);
	return FALSE;
}

void close_afsvideo(PRM_ENC *pe) {
	if (!pe->afs_init || pe->video_out_type == VIDEO_OUTPUT_DISABLED)
		return;

	afs_vbuf_release();

	pe->afs_init = FALSE;
}

static const func_convert_frame CONVERT_FUNC[][2] = {
	{ copy_yuy2,                            copy_yuy2                    },
	{ convert_yuy2_to_nv12_sse2_aligned,    convert_yuy2_to_nv12_sse2    },
	{ convert_yuy2_to_nv12_i_sse2_aligned,  convert_yuy2_to_nv12_i_sse2  },
	{ convert_yuy2_to_nv12_sse2_aligned,    convert_yuy2_to_nv12_sse2    },
	{ convert_yuy2_to_nv12_i_ssse3_aligned, convert_yuy2_to_nv12_i_ssse3 },
#if (_MSC_VER >= 1600)
	{ convert_yuy2_to_nv12_avx_aligned,     convert_yuy2_to_nv12_avx     },
	{ convert_yuy2_to_nv12_i_avx_aligned,   convert_yuy2_to_nv12_i_avx   },
#endif
#if (_MSC_VER >= 1700) //32byteでアライメントが取れているとは限らない
	{ convert_yuy2_to_nv12_avx2,            convert_yuy2_to_nv12_avx2    },
	{ convert_yuy2_to_nv12_i_avx2,          convert_yuy2_to_nv12_i_avx2  }
#endif
};
static const func_convert_frame *convert_frame;
//邪道っぽいが静的グローバル変数
static const OUTPUT_INFO *oip = NULL;
static PRM_ENC *pe = NULL;
static int total_out_frames = NULL;
static int *jitter = NULL;

//静的グローバル変数の初期化
DWORD set_auo_yuvreader_g_data(const OUTPUT_INFO *_oip, CONF_GUIEX *conf, PRM_ENC *_pe, int *_jitter) {
	oip = _oip;
	pe = _pe;
	jitter = _jitter;
	BOOL interlaced = (conf->qsv.nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF)) ? TRUE : FALSE;
	if (conf->qsv.vpp.bColorFmtConvertion) {
		convert_frame = CONVERT_FUNC[0];
		//write_log_auo_line_fmt(LOG_INFO, "converting YUY2 -> NV12, by VPP");
	} else {
		BOOL ssse3_available = !!check_ssse3();
		BOOL avx_available = FALSE, avx2_available = FALSE;
#if (_MSC_VER >= 1600)
		avx_available = !!check_avx();
#endif
#if (_MSC_VER >= 1700)
		avx2_available = !!check_avx2();
#endif
		convert_frame = CONVERT_FUNC[1+interlaced+(ssse3_available+avx_available)*2];
		write_log_auo_line_fmt(LOG_INFO, "converting YUY2 -> NV12%s, using %s%s%s",
			((interlaced) ? "i" : "p"),
			((avx2_available) ? "AVX2" : "SSE2"),
			((interlaced && ssse3_available && !avx2_available) ? " SSSE3" : ""),
			((avx_available) ? " AVX" : "")
		); 
	}
	total_out_frames = oip->n;
	switch (conf->qsv.vpp.nDeinterlace) {
		case MFX_DEINTERLACE_IT:
			total_out_frames *= 4 / 5;
			break;
		case MFX_DEINTERLACE_BOB:
			total_out_frames *= 2;
			break;
		default:
			break;
	}
	return AUO_RESULT_SUCCESS;
}

//静的グローバル変数使用終了
void clear_auo_yuvreader_g_data() {
	convert_frame = NULL;
	total_out_frames = 0;
	oip = NULL;
	pe = NULL;
	jitter = NULL;
}

AUO_YUVReader::AUO_YUVReader()
{
	m_ColorFormat = MFX_FOURCC_NV12; //AUO_YUVReaderはNV12専用
}

//unsigned int __stdcall AUO_YUVReader::InputThreadLauncher(void *pParam) {
//	return reinterpret_cast<AUO_YUVReader*>(pParam)->InputThreadFunc();
//}

#pragma warning( push )
#pragma warning( disable: 4100 )
mfxStatus AUO_YUVReader::Init(const TCHAR *strFileName, mfxU32 ColorFormat, int option, CEncodingThread *pEncThread, CEncodeStatusInfo *pEncSatusInfo, sInputCrop *pInputCrop)
{
	MSDK_CHECK_POINTER(oip, MFX_ERR_NULL_PTR);

	Close();

	MSDK_CHECK_POINTER(pEncThread, MFX_ERR_NULL_PTR);
	m_pEncThread = pEncThread;

	MSDK_CHECK_POINTER(pEncSatusInfo, MFX_ERR_NULL_PTR);
	m_pEncSatusInfo = pEncSatusInfo;
	m_bInited = true;

	m_ColorFormat = MFX_FOURCC_YUY2;

	m_inputFrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
	m_inputFrameInfo.FourCC = MFX_FOURCC_NV12;
	m_inputFrameInfo.FrameRateExtN = oip->rate;
	m_inputFrameInfo.FrameRateExtD = oip->scale;
	m_inputFrameInfo.Width = (mfxU16)oip->w;
	m_inputFrameInfo.Height = (mfxU16)oip->h;
	m_inputFrameInfo.CropW = (mfxU16)oip->w;
	m_inputFrameInfo.CropH = (mfxU16)oip->h;
	m_inputFrameInfo.CropX = 0;
	m_inputFrameInfo.CropY = 0;
	*(DWORD *)&m_inputFrameInfo.FrameId = oip->n;

	char mes[256];
	sprintf_s(mes, _countof(mes), "auo: %s->%s, %dx%d, %d/%d fps", ColorFormatToStr(m_ColorFormat), ColorFormatToStr(m_inputFrameInfo.FourCC),
		m_inputFrameInfo.Width, m_inputFrameInfo.Height, m_inputFrameInfo.FrameRateExtN, m_inputFrameInfo.FrameRateExtD);
	m_strInputInfo += mes;

	m_tmLastUpdate = timeGetTime();
    return MFX_ERR_NONE;
}
#pragma warning( pop )

AUO_YUVReader::~AUO_YUVReader()
{
    Close();
}

void AUO_YUVReader::Close()
{
}

mfxStatus AUO_YUVReader::LoadNextFrame(mfxFrameSurface1* pSurface)
{
#ifdef _DEBUG
	MSDK_CHECK_POINTER(pSurface, MFX_ERR_NULL_PTR);
	MSDK_CHECK_POINTER(m_pEncThread, MFX_ERR_NULL_PTR);
#endif
	void *frame;
	int total_frames = oip->n;

    // check if reader is initialized
    //MSDK_CHECK_ERROR(m_bInited, false, MFX_ERR_NOT_INITIALIZED);
    //MSDK_CHECK_POINTER(pSurface, MFX_ERR_NULL_PTR);
	int nFrame = m_pEncSatusInfo->m_nInputFrames + pe->drop_count;

	if (nFrame >= total_frames) {
		oip->func_rest_time_disp(current_frame, total_frames);
		release_audio_parallel_events(pe);
		return MFX_ERR_MORE_DATA;
	}

	if (oip->func_is_abort())
		return MFX_ERR_ABORTED;

    mfxFrameInfo* pInfo = &pSurface->Info;
    mfxFrameData* pData = &pSurface->Data;

	mfxU8 *dst_Y = pData->Y;
	mfxU8 *dst_C = pData->UV;

	int bAligned = (((size_t)dst_Y | (size_t)dst_C) & 0x0F) != 0x00;

	//if (pInfo->CropH > 0 && pInfo->CropW > 0) 
    //{
    //    w = pInfo->CropW;
    //    h = pInfo->CropH;
    //} 
    //else 
    //{
    //    w = pInfo->Width;
    //    h = pInfo->Height;
    //}
	
	if (pe->afs_init) {
		BOOL drop = FALSE;
		for ( ; ; ) {
			if ((frame = afs_get_video((OUTPUT_INFO *)oip, nFrame, &drop, &jitter[nFrame + 1])) == NULL) {
				error_afs_get_frame();
				return MFX_ERR_MORE_DATA;
			}
			if (!drop)
				break;
			jitter[nFrame] = DROP_FRAME_FLAG;
			pe->drop_count++;
			nFrame++;
			if (nFrame >= total_frames) {
				oip->func_rest_time_disp(current_frame, total_frames);
				release_audio_parallel_events(pe);
				return MFX_ERR_MORE_DATA;
			}
		}
	} else {
		if ((frame = oip->func_get_video_ex(m_pEncSatusInfo->m_nInputFrames, COLORFORMATS[CF_YUY2].FOURCC)) == NULL) {
			error_afs_get_frame();
			return MFX_ERR_MORE_DATA;
		}
	}

	convert_frame[bAligned](frame, dst_Y, dst_C, pInfo->CropW, pInfo->CropH, pData->Pitch);

	m_pEncSatusInfo->m_nInputFrames++;
	if (!(m_pEncSatusInfo->m_nInputFrames & 7))
		aud_parallel_task(oip, pe);

	mfxU32 tm = timeGetTime();
	//pSurface->Data.TimeStamp = m_pEncSatusInfo->m_nInputFrames * (mfxU64)m_pEncSatusInfo->m_nOutputFPSScale;
	if (tm - m_tmLastUpdate > UPDATE_INTERVAL) {
		m_tmLastUpdate = tm;
		m_pEncSatusInfo->UpdateDisplay(tm, pe->drop_count);
		oip->func_rest_time_disp(m_pEncSatusInfo->m_nInputFrames + pe->drop_count, total_frames);
		oip->func_update_preview();
	}
    return MFX_ERR_NONE;    
}

AUO_EncodeStatusInfo::AUO_EncodeStatusInfo() { }

AUO_EncodeStatusInfo::~AUO_EncodeStatusInfo() {	 }

void AUO_EncodeStatusInfo::WriteLine(const TCHAR *mes) {
	const char *HEADER = "qsv [info]: ";
	int buf_len = strlen(mes) + 1 + strlen(HEADER);
	char *buf = (char *)calloc(buf_len, sizeof(buf[0]));
	if (buf) {
		memcpy(buf, HEADER, strlen(HEADER));
		memcpy(buf + strlen(HEADER), mes, strlen(mes) + 1);
		write_log_line(LOG_INFO, buf);
		free(buf);
	}
}
void AUO_EncodeStatusInfo::UpdateDisplay(const char *mes, int drop_frames) {
	set_log_title_and_progress(mes, (m_nProcessedFramesNum + drop_frames) / (mfxF64)m_nTotalOutFrames);
}