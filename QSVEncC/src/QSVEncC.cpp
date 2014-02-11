﻿//
//               INTEL CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or
//  nondisclosure agreement with Intel Corporation and may not be copied
//  or disclosed except in accordance with the terms of that agreement.
//        Copyright (c) 2005-2010 Intel Corporation. All Rights Reserved.
//

//  -----------------------------------------------------------------------------------------
//    QSVEncC
//      modified from sample_encode.cpp by rigaya 
//  -----------------------------------------------------------------------------------------

#include <io.h>
#include <fcntl.h>
#include <Math.h>
#include <signal.h>
#include "shlwapi.h"
#pragma comment(lib, "shlwapi.lib") 

#include "pipeline_encode.h"
#include "qsv_prm.h"
#include "qsv_version.h"

static void PrintVersion() {
	static const TCHAR *const ENABLED_INFO[] = { _T("disabled"), _T("enabled") };
#ifdef _M_IX86
	_ftprintf(stdout, _T("QSVEncC (x86) %s by rigaya, build %s %s\n"), VER_STR_FILEVERSION_TCHAR, _T(__DATE__), _T(__TIME__));
#else
	_ftprintf(stdout, _T("QSVEncC (x64) %s by rigaya, build %s %s\n"), VER_STR_FILEVERSION_TCHAR, _T(__DATE__), _T(__TIME__));
#endif
	_ftprintf(stdout, _T("based on Intel(R) Media SDK Encoding Sample %s\n"), MSDK_SAMPLE_VERSION);
	_ftprintf(stdout, _T("  avi reader: %s\n"), ENABLED_INFO[!!ENABLE_AVI_READER]);
	_ftprintf(stdout, _T("  avs reader: %s\n"), ENABLED_INFO[!!ENABLE_AVISYNTH_READER]);
	_ftprintf(stdout, _T("  vpy reader: %s\n"), ENABLED_INFO[!!ENABLE_VAPOURSYNTH_READER]);
	_ftprintf(stdout, _T("\n"));
}

//適当に改行しながら表示する
static void PrintListOptions(FILE *fp, TCHAR *option_name, const CX_DESC *list, int default_index) {
	const TCHAR *indent_space = _T("                                  ");
	const int indent_len = (int)_tcslen(indent_space);
	const int max_len = 77;
	int print_len = _ftprintf(fp, _T("   %s "), option_name);
	while (print_len < indent_len)
		 print_len += _ftprintf(stdout, _T(" "));
	for (int i = 0; list[i].desc; i++) {
		if (print_len + _tcslen(list[i].desc) + _tcslen(_T(", ")) >= max_len) {
			_ftprintf(fp, _T("\n%s"), indent_space);
			print_len = indent_len;
		} else {
			if (i)
				print_len += _ftprintf(fp, _T(", "));
		}
		print_len += _ftprintf(fp, _T("%s"), list[i].desc);
	}
	_ftprintf(fp, _T("\n%s default: %s\n"), indent_space, list[default_index].desc);
}

static void PrintHelp(TCHAR *strAppName, TCHAR *strErrorMessage, TCHAR *strOptionName)
{
	if (strErrorMessage)
	{
		if (strOptionName)
			_ftprintf(stderr, _T("Error: %s for %s\n\n"), strErrorMessage, strOptionName);
		else
			_ftprintf(stderr, _T("Error: %s\n\n"), strErrorMessage);
	}
	else
	{
		PrintVersion();

		_ftprintf(stdout, _T("Usage: %s [Options] -i <filename> -o <filename>\n"), PathFindFileName(strAppName));
		_ftprintf(stdout, _T("\n")
			_T("input can be %s%s%sraw YUV or YUV4MPEG2(y4m) format.\n")
			_T("when raw(default), fps, input-res are also necessary.\n")
			_T("\n")
			_T("output format will be raw H.264/AVC ES.\n")
			_T("when output filename is set to \"-\", H.264/AVC ES output is thrown to stdout.\n")
			_T("\n")
			_T("Example:\n")
			_T("  QSVEncC -i \"<avsfilename>\" -o \"<outfilename>\"\n")
			_T("  avs2pipemod -y4mp \"<avsfile>\" | QSVEncC --y4m -i - -o \"<outfilename>\"\n")
			_T("\n")
			_T("Options: \n")
			_T("-h,-? --help                      show help\n")
			_T("-v,--version                      show version info\n")
			_T("\n")
			_T("-i,--input-file <filename>        set input file name\n")
			_T("-o,--output-file <filename>       set ouput file name\n")
			_T("\n")
			_T(" Input formats (will be estimated from extension if not set.)\n")
			_T("   --raw                          set input as raw format\n")
			_T("   --y4m                          set input as y4m format\n")
			_T("   --avi                          set input as avi format\n")
			_T("   --avs                          set input as avs format\n")
			_T("   --vpy                          set input as vpy format\n")
			_T("   --vpy-mt                       set input as vpy format in multi-thread\n")
			_T("\n")
			_T("   --nv12                         set raw input as NV12 color format,\n")
			_T("                                  if not specified YV12 is expected\n")
			_T("   --tff                          set as interlaced, top field first\n")
			_T("   --bff                          set as interlaced, bottom field first\n")
			_T("-f,--fps <int>/<int> or <float>   video frame rate (frames per second)\n")
			_T("\n")
			_T("   --input-res <int>x<int>        input resolution\n")
			_T("   --output-res <int>x<int>       output resolution\n")
			_T("                                  if different from input, uses vpp resizing\n")
			_T("                                  if not set, output resolution will be same\n")
			_T("                                  as input (no resize will be done).\n")
			_T("   --crop <int>,<int>,<int>,<int> set crop pixels of left, up, right, bottom.\n")
			_T("\n")
			_T("   --slices <int>                 number of slices, default 0 (auto)\n")
			_T("\n")
			_T("   --sw                           use software encoding, instead of QSV (hw)\n")
			_T("   --hw-check                     check if QuickSyncVideo is available\n")
			_T("   --lib-check                    check lib API version installed\n"),
			(ENABLE_AVI_READER)         ? _T("avi, ") : _T(""),
			(ENABLE_AVISYNTH_READER)    ? _T("avs, ") : _T(""),
			(ENABLE_VAPOURSYNTH_READER) ? _T("vpy, ") : _T(""));
#ifdef D3D_SURFACES_SUPPORT
		_ftprintf(stdout, _T("")
			_T("   --disable-d3d                  disable using d3d surfaces\n"));
#if MFX_D3D11_SUPPORT
		_ftprintf(stdout, _T("")
			_T("   --d3d                          use d3d9/d3d11 surfaces\n")
			_T("   --d3d9                         use d3d9 surfaces\n")
			_T("   --d3d11                        use d3d11 surfaces\n"));
#else
		_ftprintf(stdout, _T("")
			_T("   --d3d                          use d3d9 surfaces\n"));
#endif //MFX_D3D11_SUPPORT
#endif //D3D_SURFACES_SUPPORT
		_ftprintf(stdout,_T("\n")
			_T(" EncMode default: --cqp\n")
			_T("   --cqp <int> or                 encode in Constant QP, default %d:%d:%d\n")
			_T("         <int>:<int>:<int>        set qp value for i:p:b frame\n")
			_T("   --vqp <int> or                 encode in Variable QP, default %d:%d:%d\n")
			_T("         <int>:<int>:<int>        set qp value for i:p:b frame\n")
			_T("   --la <int>                     encoded bitrate in Lookahead mode (kbps)\n")
			_T("   --icq <int>                    encode in Intelligent Const. Qualtiy mode\n")
			_T("                                    default value: %d\n")
			_T("   --la-icq <int>                 encode in ICQ mode with Lookahead\n")
			_T("                                    default value: %d\n")
			_T("   --cbr <int>                    encoded bitrate in CBR mode (kbps)\n")
			_T("   --vbr <int>                    encoded bitrate in VBR mode (kbps)\n")
			_T("   --avbr <int>                   encoded bitrate in AVBR mode (kbps)\n")
			_T("                                   avbr mode is only supported with API v1.3\n")
			_T("   --avbr-unitsize <int>          avbr calculation period in x100 frames\n")
			_T("                                   default %d (= unit size %d00 frames)\n")
			_T("   --vcm <int>                    encoded bitrate in VCM mode (kbps)\n")
			//_T("   --avbr-range <float>           avbr accuracy range from bitrate set\n)"
			//_T("                                   in percentage, defalut %.1f(%%)\n)"
			_T("\n")
			_T("   --la-depth <int>               set Lookahead Depth, %d-%d\n")
			_T("   --maxbitrate <int>             set max bitrate(kbps)\n")
			_T("-u,--quality <string>             encode quality\n")
			_T("                                    - best, higher, high, balanced(default)\n")
			_T("                                      fast, faster, fastest\n")
			_T("\n")
			_T("   --ref <int>                    reference frames for sw encoding,\n")
			_T("                                    default %d (auto)\n")
			_T("   --bframes <int>                number of sequential b frames,\n")
			_T("                                    default %d (auto)\n")
			_T("\n")
			_T("   --gop-len <int>                (max) gop length, default %d (auto)\n")
			_T("                                    when auto, fps x 10 will be set.\n")
			_T("   --strict-gop                   force gop structure\n")
			_T("   --scenechange                  enable scene change detection\n")
			_T("   --no-scenechange               disable scene change detection\n")
			_T("\n")
			_T("   --level <string>               set codec level, default auto\n")
			_T("   --profile <string>             set codec profile, default auto\n")
			_T("   --sar <int>:<int>              set Sample Aspect Ratio.\n")
			_T("   --bluray                       for H.264 bluray encoding.\n")
			_T("\n")
			_T("   --vpp-denoise <int>            use vpp denoise, set strength\n")
			_T("   --vpp-detail-enhance <int>     use vpp detail enahancer, set strength\n")
			_T("   --vpp-deinterlace <string>     set vpp deinterlace mode\n")
			_T("                                  enabled only when set --tff or --bff\n")
			_T("                                   - none    disable deinterlace\n")
			_T("                                   - normal  normal deinterlace\n")
			_T("                                   - it      inverse telecine\n")
			_T("                                   - bob     double framerate\n"),
			QSV_DEFAULT_QPI, QSV_DEFAULT_QPP, QSV_DEFAULT_QPB,
			QSV_DEFAULT_QPI, QSV_DEFAULT_QPP, QSV_DEFAULT_QPB,
			QSV_DEFAULT_ICQ, QSV_DEFAULT_ICQ,
			QSV_DEFAULT_CONVERGENCE, QSV_DEFAULT_CONVERGENCE,
			QSV_LOOKAHEAD_DEPTH_MIN, QSV_LOOKAHEAD_DEPTH_MAX,
			QSV_DEFAULT_REF,
			QSV_DEFAULT_BFRAMES,
			QSV_DEFAULT_GOP_LEN
			);
		_ftprintf(stdout, _T("\n")
			_T("   --input-buf <int>              buffer size for input (%d-%d)\n")
			_T("                                   default   hw: %d,  sw: %d\n"),
			QSV_INPUT_BUF_MIN, QSV_INPUT_BUF_MAX,
			QSV_DEFAULT_INPUT_BUF_HW, QSV_DEFAULT_INPUT_BUF_SW
			);
		_ftprintf(stdout,
			_T("   --log <string>                 output log to file.\n"));
		_ftprintf(stdout, _T("\n")
			_T(" settings below are only supported with API v1.3\n")
			_T("   --fullrange                    set stream as fullrange yuv.\n")
			);
		PrintListOptions(stdout, _T("--videoformat <string>"), list_videoformat, 0);
		PrintListOptions(stdout, _T("--colormatrix <string>"), list_colormatrix, 0);
		PrintListOptions(stdout, _T("--colorprim <string>"), list_colorprim, 0);
		PrintListOptions(stdout, _T("--transfer <string>"), list_transfer, 0);
		_ftprintf(stdout, _T("\n")
			_T(" settings below are only supported with API v1.6\n")
			_T("   --mbbrc                        enables per macro block rate control.\n")
			_T("   --extbrc                       enables extended rate control.\n")
			);
		_ftprintf(stdout, _T("\n")
			_T(" settings below are only supported with API v1.7\n")
			_T("   --trellis <string>             set trellis mode used in encoding.\n")
			_T("                                   - auto(default), none, i, ip, all.\n")
			);
		_ftprintf(stdout, _T("\n")
			_T(" settings below are only supported with API v1.8\n")
			_T("   --i-adapt                      enables adaptive I frame insert.\n")
			_T("   --b-adapt                      enables adaptive B frame insert.\n")
			_T("   --b-pyramid                    enables B-frame pyramid reference.\n")
			_T("   --lookahead-ds <string>        set lookahead quality.\n")
			_T("                                   - auto(default), fast, normal, slow\n")
			);
		_ftprintf(stdout,_T("\n"
			_T(" Settings below are available only for software ecoding.\n")
			_T("   --cavlc                        use cavlc instead of cabac.\n")
			_T("   --rdo                          use rate distortion optmization.\n")
			_T("   --inter-pred <int>             set minimum block size used for\n")
			_T("   --intra-pred <int>             inter/intra prediction.\n")
			_T("                                    0: auto(default)   1: 16x16\n")
			_T("                                    2: 8x8             3: 4x4\n")
			_T("   --mv-search <int>              set window size for mv search.\n")
			_T("                                    default: 0 (auto)\n")
			_T("   --mv-precision <int>           set precision of mv search\n")
			_T("                                    0: auto(default)   1: full-pell\n")
			_T("                                    2: half-pell       3: quater-pell\n")
			));
	}
}

static void PrintAPISupportError(TCHAR *option_name, mfxVersion required_ver, mfxVersion current_ver, BOOL hardware_mode) {
	_ftprintf(stderr, _T("Error: %s requires API v%d.%d, current %s API v%d.%d"),
		option_name, 
		required_ver.Major, required_ver.Minor, 
		hardware_mode ? _T("hw") : _T("sw"), 
		current_ver.Major, current_ver.Minor);
}

mfxStatus ParseInputString(TCHAR* strInput[], mfxU8 nArgNum, sInputParams* pParams)
{
	TCHAR* strArgument = _T("");

	if (1 == nArgNum)
	{
		PrintHelp(strInput[0], NULL, NULL);
		PrintHelp(strInput[0], _T("options needed."), NULL);
		return MFX_PRINT_OPTION_ERR;
	}


	MSDK_CHECK_POINTER(pParams, MFX_ERR_NULL_PTR);
	MSDK_ZERO_MEMORY(*pParams);
	mfxU16 tmp_input_buf  = 0;

	pParams->CodecId       = MFX_CODEC_AVC;
	pParams->nTargetUsage  = QSV_DEFAULT_QUALITY;
	pParams->nEncMode      = MFX_RATECONTROL_CQP;
	pParams->ColorFormat   = MFX_FOURCC_YV12;
	pParams->nPicStruct    = MFX_PICSTRUCT_PROGRESSIVE;
	pParams->nQPI          = QSV_DEFAULT_QPI;
	pParams->nQPP          = QSV_DEFAULT_QPP;
	pParams->nQPB          = QSV_DEFAULT_QPB;
	pParams->nRef          = QSV_DEFAULT_REF;
	pParams->bUseHWLib     = true;
	pParams->memType       = HW_MEMORY;
	pParams->nBframes      = QSV_DEFAULT_BFRAMES;
	pParams->nGOPLength    = QSV_DEFAULT_GOP_LEN;
	pParams->ColorPrim     = (mfxU16)list_colorprim[0].value;
	pParams->ColorMatrix   = (mfxU16)list_colormatrix[0].value;
	pParams->Transfer      = (mfxU16)list_transfer[0].value;
	pParams->VideoFormat   = (mfxU16)list_videoformat[0].value;
	pParams->nInputBufSize = QSV_DEFAULT_INPUT_BUF_HW;

	// parse command line parameters
	for (mfxU8 i = 1; i < nArgNum; i++)
	{
		MSDK_CHECK_POINTER(strInput[i], MFX_ERR_NULL_PTR);

		TCHAR *option_name = NULL;

		if (strInput[i][0] == _T('-')) {
			switch (strInput[i][1]) {
				case _T('-'):
					option_name = &strInput[i][2];
					break;
				case _T('c'):
					option_name = _T("codec");
					break;
				case _T('u'):
					option_name = _T("quality");
					break;
				case _T('f'):
					option_name = _T("fps");
					break;
				case _T('i'):
					option_name = _T("input-file");
					break;
				case _T('o'):
					option_name = _T("output-file");
					_tcscpy_s(pParams->strDstFile, strArgument);
					break;
				case _T('v'):
					option_name = _T("version");
					break;
				case _T('h'):
				case _T('?'):
					option_name = _T("help");
					break;
				default:
					PrintHelp(strInput[0], _T("Unknown options"), NULL);
					return MFX_PRINT_OPTION_ERR;
			}
		}

		MSDK_CHECK_POINTER(option_name, MFX_ERR_NULL_PTR);

		// process multi-character options
		if (0 == _tcscmp(option_name, _T("help")))
		{
			PrintHelp(strInput[0], NULL, NULL);
			return MFX_PRINT_OPTION_DONE;
		}
		else if (0 == _tcscmp(option_name, _T("version")))
		{
			PrintVersion();
			return MFX_PRINT_OPTION_DONE;
		}
		else if (0 == _tcscmp(option_name, _T("output-res")))
		{
			i++;
			if (2 == _stscanf_s(strInput[i], _T("%hdx%hd"), &pParams->nDstWidth, &pParams->nDstHeight))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd,%hd"), &pParams->nDstWidth, &pParams->nDstHeight))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd/%hd"), &pParams->nDstWidth, &pParams->nDstHeight))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd:%hd"), &pParams->nDstWidth, &pParams->nDstHeight))
				;
			else {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("input-res")))
		{
			i++;
			if (2 == _stscanf_s(strInput[i], _T("%hdx%hd"), &pParams->nWidth, &pParams->nHeight))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd,%hd"), &pParams->nWidth, &pParams->nHeight))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd/%hd"), &pParams->nWidth, &pParams->nHeight))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd:%hd"), &pParams->nWidth, &pParams->nHeight))
				;
			else {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("crop")))
		{
			i++;
			if (4 == _stscanf_s(strInput[i], _T("%hd,%hd,%hd,%hd"), &pParams->sInCrop.left, &pParams->sInCrop.up, &pParams->sInCrop.right, &pParams->sInCrop.bottom))
				;
			else if (4 == _stscanf_s(strInput[i], _T("%hd:%hd:%hd:%hd"), &pParams->sInCrop.left, &pParams->sInCrop.up, &pParams->sInCrop.right, &pParams->sInCrop.bottom))
				;
			else {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("codec")))
		{
			i++;
			for (int i = 0; list_codec[i].desc; i++)
				if (_tcsicmp(list_codec[i].desc, strInput[i]) == 0) {
					pParams->CodecId = list_codec[i].value;
					continue;
				}
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
		}
		else if (0 == _tcscmp(option_name, _T("raw")))
		{
			pParams->nInputFmt = INPUT_FMT_RAW;
		}
		else if (0 == _tcscmp(option_name, _T("y4m")))
		{
			pParams->nInputFmt = INPUT_FMT_Y4M;
		}
		else if (0 == _tcscmp(option_name, _T("avi")))
		{
			pParams->nInputFmt = INPUT_FMT_AVI;
		}
		else if (0 == _tcscmp(option_name, _T("avs")))
		{
			pParams->nInputFmt = INPUT_FMT_AVS;
		}
		else if (0 == _tcscmp(option_name, _T("vpy")))
		{
			pParams->nInputFmt = INPUT_FMT_VPY;
		}
		else if (0 == _tcscmp(option_name, _T("vpy-mt")))
		{
			pParams->nInputFmt = INPUT_FMT_VPY_MT;
		}
		else if (0 == _tcscmp(option_name, _T("input-file")))
		{
			i++;
			_tcscpy_s(pParams->strSrcFile, strInput[i]);
		}
		else if (0 == _tcscmp(option_name, _T("output-file")))
		{
			i++;
			_tcscpy_s(pParams->strDstFile, strInput[i]);
		}
		else if (0 == _tcscmp(option_name, _T("quality")))
		{
			i++;
			int value = MFX_TARGETUSAGE_BALANCED;
			if (1 == _stscanf_s(strInput[i], _T("%d"), &value)) {
				pParams->nTargetUsage = (mfxU16)clamp(value, MFX_TARGETUSAGE_BEST_QUALITY, MFX_TARGETUSAGE_BEST_SPEED);
			} else if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_quality_for_option, strInput[i]))) {
				pParams->nTargetUsage = (mfxU16)value;
			} else {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("level")))
		{
			i++;
			double d;
			int value;
			if (_tcscmp(strInput[i], _T("1b")) == 0) {
				pParams->CodecLevel = MFX_LEVEL_AVC_1b;
				continue;
			}
			if (1 == _stscanf_s(strInput[i], _T("%lf"), &d)) {
				if (get_cx_index(list_avc_level, (int)(d * 10.0 + 0.5)) >= 0) {
					pParams->CodecLevel = (mfxU16)(d * 10.0 + 0.5);
					continue;
				}
			}
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_mpeg2_level, strInput[i]))) {
				pParams->CodecLevel = (mfxU16)value;
				continue;
			}				
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_vc1_level, strInput[i]))) {
				pParams->CodecLevel = (mfxU16)value;
				continue;
			}				
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_vc1_level_adv, strInput[i]))) {
				pParams->CodecLevel = (mfxU16)value;
				continue;
			}
			PrintHelp(strInput[0], _T("Unknown value"), option_name);
			return MFX_PRINT_OPTION_ERR;
		}
		else if (0 == _tcscmp(option_name, _T("profile")))
		{
			i++;
			int value;
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_avc_profile, strInput[i]))) {
				pParams->CodecProfile = (mfxU16)value;
				continue;
			}
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_mpeg2_profile, strInput[i]))) {
				pParams->CodecProfile = (mfxU16)value;
				continue;
			}
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_vc1_profile, strInput[i]))) {
				pParams->CodecProfile = (mfxU16)value;
				continue;
			}
			PrintHelp(strInput[0], _T("Unknown value"), option_name);
			return MFX_PRINT_OPTION_ERR;
		}
		else if (0 == _tcscmp(option_name, _T("sar")))
		{
			i++;
			if (2 == _stscanf_s(strInput[i], _T("%dx%d"), &pParams->nPAR[0], &pParams->nPAR[1]))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%d,%d"), &pParams->nPAR[0], &pParams->nPAR[1]))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%d/%d"), &pParams->nPAR[0], &pParams->nPAR[1]))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%d:%d"), &pParams->nPAR[0], &pParams->nPAR[1]))
				;
			else {
				MSDK_ZERO_MEMORY(pParams->nPAR);
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("sw")))
		{
			pParams->bUseHWLib = false;
		}
		else if (0 == _tcscmp(option_name, _T("slices")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nSlices)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("gop-len")))
		{
			i++;
			if (0 == _tcsnccmp(strInput[i], _T("auto"), _tcslen(_T("auto")))) {
				pParams->nGOPLength = 0;
			} else if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nGOPLength)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		//else if (0 == _tcscmp(option_name, _T("open-gop")))
		//{
		//    pParams->bopenGOP = true;
		//}
		else if (0 == _tcscmp(option_name, _T("strict-gop")))
		{
			pParams->bforceGOPSettings = true;
		}
		else if (0 == _tcscmp(option_name, _T("no-scenechange")))
		{
			pParams->bforceGOPSettings = true;
		}
		else if (0 == _tcscmp(option_name, _T("scenechange")))
		{
			pParams->bforceGOPSettings = false;
		}
		else if (0 == _tcscmp(option_name, _T("i-adapt")))
		{
			pParams->bAdaptiveI = true;
		}
		else if (0 == _tcscmp(option_name, _T("b-adapt")))
		{
			pParams->bAdaptiveB = true;
		}
		else if (0 == _tcscmp(option_name, _T("b-pyramid")))
		{
			pParams->bBPyramid = true;
		}
		else if (0 == _tcscmp(option_name, _T("lookahead-ds")))
		{
			i++;
			int value = MFX_LOOKAHEAD_DS_UNKNOWN;
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_lookahead_ds, strInput[i]))) {
				pParams->nTrellis = (mfxU16)value;
			} else {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("scenechange")))
		{
			pParams->bforceGOPSettings = false;
		}
		else if (0 == _tcscmp(option_name, _T("trellis")))
		{
			i++;
			int value = MFX_TRELLIS_UNKNOWN;
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_avc_trellis_for_options, strInput[i]))) {
				pParams->nTrellis = (mfxU16)value;
			} else {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("bluray")))
		{
			pParams->nBluray = 1;
		}
		else if (0 == _tcscmp(option_name, _T("force-bluray")))
		{
			pParams->nBluray = 2;
		}
		else if (0 == _tcscmp(option_name, _T("nv12")))
		{
			pParams->ColorFormat = MFX_FOURCC_NV12;
		}
		else if (0 == _tcscmp(option_name, _T("tff")))
		{
			pParams->nPicStruct = MFX_PICSTRUCT_FIELD_TFF;
		}
		else if (0 == _tcscmp(option_name, _T("bff")))
		{
			pParams->nPicStruct = MFX_PICSTRUCT_FIELD_BFF;
		}
		else if (0 == _tcscmp(option_name, _T("la")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nBitRate)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nEncMode = MFX_RATECONTROL_LA;
		}
		else if (0 == _tcscmp(option_name, _T("icq")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nICQQuality)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nEncMode = MFX_RATECONTROL_ICQ;
		}
		else if (0 == _tcscmp(option_name, _T("la-icq")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nICQQuality)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nEncMode = MFX_RATECONTROL_LA_ICQ;
		}
		else if (0 == _tcscmp(option_name, _T("vcm")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nBitRate)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nEncMode = MFX_RATECONTROL_VCM;
		}
		else if (0 == _tcscmp(option_name, _T("vbr")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nBitRate)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nEncMode = MFX_RATECONTROL_VBR;
		}
		else if (0 == _tcscmp(option_name, _T("cbr")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nBitRate)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nEncMode = MFX_RATECONTROL_CBR;
		}
		else if (0 == _tcscmp(option_name, _T("avbr")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nBitRate)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nEncMode = MFX_RATECONTROL_AVBR;
		}
		else if (0 == _tcscmp(option_name, _T("maxbitrate")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nMaxBitrate)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("la-depth")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nLookaheadDepth)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("cqp")) || 0 == _tcscmp(option_name, _T("vqp")))
		{
			i++;
			if (3 == _stscanf_s(strInput[i], _T("%hd:%hd:%hd"), &pParams->nQPI, &pParams->nQPP, &pParams->nQPB))
				;
			else if (3 == _stscanf_s(strInput[i], _T("%hd,%hd,%hd"), &pParams->nQPI, &pParams->nQPP, &pParams->nQPB))
				;
			else if (3 == _stscanf_s(strInput[i], _T("%hd/%hd/%hd"), &pParams->nQPI, &pParams->nQPP, &pParams->nQPB))
				;
			else if (1 == _stscanf_s(strInput[i], _T("%hd"), &pParams->nQPI)) {
				pParams->nQPP = pParams->nQPI;
				pParams->nQPB = pParams->nQPI;
			} else {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nEncMode = (mfxU16)((0 == _tcscmp(option_name, _T("vqp"))) ? MFX_RATECONTROL_VQP : MFX_RATECONTROL_CQP);
		}
		else if (0 == _tcscmp(option_name, _T("avbr-unitsize")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nAVBRConvergence)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		//else if (0 == _tcscmp(option_name, _T("avbr-range")))
		//{
		//	double accuracy;
		//	if (1 != _stscanf_s(strArgument, _T("%f"), &accuracy)) {
		//		PrintHelp(strInput[0], _T("Unknown value"), option_name);
		//		return MFX_PRINT_OPTION_ERR;
		//	}
		//	pParams->nAVBRAccuarcy = (mfxU16)(accuracy * 10 + 0.5);
		//}
		else if (0 == _tcscmp(option_name, _T("ref")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nRef)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("bframes")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->nBframes)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("cavlc")))
		{
			pParams->bCAVLC = true;
		}
		else if (0 == _tcscmp(option_name, _T("rdo")))
		{
			pParams->bRDO = true;
		}
		else if (0 == _tcscmp(option_name, _T("extbrc")))
		{
			pParams->bExtBRC = true;
		}
		else if (0 == _tcscmp(option_name, _T("mbbrc")))
		{
			pParams->bMBBRC = true;
		}
		else if (0 == _tcscmp(option_name, _T("fullrange")))
		{
			pParams->bFullrange = true;
		}
		else if (0 == _tcscmp(option_name, _T("inter-pred")))
		{
			i++;
			mfxI32 v;
			if (1 != _stscanf_s(strInput[i], _T("%d"), &v) && 0 <= v && v < _countof(list_pred_block_size) - 1) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nInterPred = (mfxU16)list_pred_block_size[v].value;
		}
		else if (0 == _tcscmp(option_name, _T("intra-pred")))
		{
			i++;
			mfxI32 v;
			if (1 != _stscanf_s(strInput[i], _T("%d"), &v) && 0 <= v && v < _countof(list_pred_block_size) - 1) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nIntraPred = (mfxU16)list_pred_block_size[v].value;
		}
		else if (0 == _tcscmp(option_name, _T("mv-precision")))
		{
			i++;
			mfxI32 v;
			if (1 != _stscanf_s(strInput[i], _T("%d"), &v) && 0 <= v && v < _countof(list_mv_presicion) - 1) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->nMVPrecision = (mfxU16)list_mv_presicion[v].value;
		}
		else if (0 == _tcscmp(option_name, _T("mv-search")))
		{
			i++;
			mfxI32 v;
			if (1 != _stscanf_s(strInput[i], _T("%d"), &v)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->MVSearchWindow.x = (mfxU16)clamp(v, 0, 128);
			pParams->MVSearchWindow.y = (mfxU16)clamp(v, 0, 128);
		}
		else if (0 == _tcscmp(option_name, _T("fps")))
		{
			i++;
			if (2 == _stscanf_s(strInput[i], _T("%d/%d"), &pParams->nFPSRate, &pParams->nFPSScale))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%d:%d"), &pParams->nFPSRate, &pParams->nFPSScale))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%d,%d"), &pParams->nFPSRate, &pParams->nFPSScale))
				;
			else {
				double d;
				if (1 == _stscanf_s(strInput[i], _T("%lf"), &d)) {
					int rate = (int)(d * 1001.0 + 0.5);
					if (rate % 1000 == 0) {
						pParams->nFPSRate = rate;
						pParams->nFPSScale = 1001;
					} else {
						pParams->nFPSScale = 100000;
						pParams->nFPSRate = (int)(d * pParams->nFPSScale + 0.5);
						int gcd = GCD(pParams->nFPSRate, pParams->nFPSScale);
						pParams->nFPSScale /= gcd;
						pParams->nFPSRate  /= gcd;
					}
				} else  {
					PrintHelp(strInput[0], _T("Unknown value"), option_name);
					return MFX_PRINT_OPTION_ERR;
				}
			}
		}
#ifdef D3D_SURFACES_SUPPORT
		else if (0 == _tcscmp(option_name, _T("disable-d3d")))
		{
			pParams->memType = SYSTEM_MEMORY;
		}
		else if (0 == _tcscmp(option_name, _T("d3d9")))
		{
			pParams->memType = D3D9_MEMORY;
		}
#if MFX_D3D11_SUPPORT
		else if (0 == _tcscmp(option_name, _T("d3d11")))
		{
			pParams->memType = D3D11_MEMORY;
		}
		else if (0 == _tcscmp(option_name, _T("d3d")))
		{
			pParams->memType = HW_MEMORY;
		}
#else
		else if (0 == _tcscmp(option_name, _T("d3d")))
		{
			pParams->memType = D3D9_MEMORY;
		}
#endif //MFX_D3D11_SUPPORT
#endif //D3D_SURFACES_SUPPORT
		else if (0 == _tcscmp(option_name, _T("vpp-denoise")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->vpp.nDenoise)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->vpp.bEnable = true;
			pParams->vpp.bUseDenoise = true;
		}
		else if (0 == _tcscmp(option_name, _T("vpp-no-denoise")))
		{
			i++;
			pParams->vpp.bUseDenoise = false;
			pParams->vpp.nDenoise = 0;
		}
		else if (0 == _tcscmp(option_name, _T("vpp-detail-enhance")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &pParams->vpp.nDetailEnhance)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
			pParams->vpp.bEnable = true;
			pParams->vpp.bUseDetailEnhance = true;
		}
		else if (0 == _tcscmp(option_name, _T("vpp-no-detail-enhance")))
		{
			i++;
			pParams->vpp.bUseDetailEnhance = false;
			pParams->vpp.nDetailEnhance = 0;
		}
		else if (0 == _tcscmp(option_name, _T("vpp-deinterlace")))
		{
			i++;
			if (0 == _tcscmp(strInput[i], _T("none")))
				pParams->vpp.nDeinterlace = MFX_DEINTERLACE_NONE;
			else if (0 == _tcscmp(strInput[i], _T("normal"))) {
				pParams->vpp.bEnable = true;
				pParams->vpp.nDeinterlace = MFX_DEINTERLACE_NORMAL;
			} else if (0 == _tcscmp(strInput[i], _T("it"))) {
				pParams->vpp.bEnable = true;
				pParams->vpp.nDeinterlace = MFX_DEINTERLACE_IT;
			} else if (0 == _tcscmp(strInput[i], _T("bob"))) {
				pParams->vpp.bEnable = true;
				pParams->vpp.nDeinterlace = MFX_DEINTERLACE_BOB;
			} else  {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("input-buf")))
		{
			i++;
			if (1 != _stscanf_s(strInput[i], _T("%hd"), &tmp_input_buf)) {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("log")))
		{
			i++;
			int filename_len = (int)_tcslen(strInput[i]);
			pParams->pStrLogFile = (TCHAR *)calloc(filename_len + 1, sizeof(pParams->pStrLogFile[0]));
			memcpy(pParams->pStrLogFile, strInput[i], sizeof(pParams->pStrLogFile[0]) * filename_len);
		}
		else if (0 == _tcscmp(option_name, _T("hw-check")))
		{
			mfxVersion ver = { 0, 1 };
			if (check_lib_version(get_mfx_libhw_version(), ver) != 0) {
				_ftprintf(stdout, _T("Success: QuickSyncVideo (hw encoding) available"));
				return MFX_PRINT_OPTION_DONE;
			} else {
				_ftprintf(stdout, _T("Error: QuickSyncVideo (hw encoding) unavailable"));
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else if (0 == _tcscmp(option_name, _T("lib-check")))
		{
			mfxVersion test = { 0, 1 };
			mfxVersion hwlib = get_mfx_libhw_version();
			mfxVersion swlib = get_mfx_libsw_version();
			PrintVersion();
#ifdef _M_IX86
			const TCHAR *dll_platform = _T("32");
#else
			const TCHAR *dll_platform = _T("64");
#endif
			if (check_lib_version(hwlib, test))
				_ftprintf(stdout, _T("libmfxhw%s.dll : v%d.%d\n"), dll_platform, hwlib.Major, hwlib.Minor);
			else
				_ftprintf(stdout, _T("libmfxhw%s.dll : ----\n"), dll_platform);
			if (check_lib_version(swlib, test))
				_ftprintf(stdout, _T("libmfxsw%s.dll : v%d.%d\n"), dll_platform, swlib.Major, swlib.Minor);
			else
				_ftprintf(stdout, _T("libmfxsw%s.dll : ----\n"), dll_platform);
			return MFX_PRINT_OPTION_DONE;
		}
		else if (0 == _tcscmp(option_name, _T("colormatrix")))
		{
			i++;
			int value;
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_colormatrix, strInput[i])))
				pParams->ColorMatrix = (mfxU16)value;
		}
		else if (0 == _tcscmp(option_name, _T("colorprim")))
		{
			i++;
			int value;
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_colorprim, strInput[i])))
				pParams->ColorPrim = (mfxU16)value;
		}
		else if (0 == _tcscmp(option_name, _T("transfer")))
		{
			i++;
			int value;
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_transfer, strInput[i])))
				pParams->Transfer = (mfxU16)value;
		}
		else if (0 == _tcscmp(option_name, _T("videoformat")))
		{
			i++;
			int value;
			if (PARSE_ERROR_FLAG != (value = get_value_from_chr(list_videoformat, strInput[i])))
				pParams->ColorMatrix = (mfxU16)value;
		}
		else if (0 == _tcscmp(option_name, _T("fullrange")))
		{
			pParams->bFullrange = true;
		}
		else if (0 == _tcscmp(option_name, _T("sar")))
		{
			i++;
			if (2 == _stscanf_s(strInput[i], _T("%hdx%hd"), &pParams->nPAR[0], &pParams->nPAR[1]))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd,%hd"), &pParams->nPAR[0], &pParams->nPAR[1]))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd/%hd"), &pParams->nPAR[0], &pParams->nPAR[1]))
				;
			else if (2 == _stscanf_s(strInput[i], _T("%hd:%hd"), &pParams->nPAR[0], &pParams->nPAR[1]))
				;
			else {
				PrintHelp(strInput[0], _T("Unknown value"), option_name);
				return MFX_PRINT_OPTION_ERR;
			}
		}
		else
		{
			PrintHelp(strInput[0], _T("Unknown options"), NULL);
			return MFX_PRINT_OPTION_ERR;
		}
	}

	// check if all mandatory parameters were set
	if (0 == _tcslen(pParams->strSrcFile)) {
		PrintHelp(strInput[0], _T("Source file name not found"), NULL);
		return MFX_PRINT_OPTION_ERR;
	}

	if (0 == _tcslen(pParams->strDstFile)) {
		PrintHelp(strInput[0], _T("Destination file name not found"), NULL);
		return MFX_PRINT_OPTION_ERR;
	}

	if (MFX_CODEC_MPEG2 != pParams->CodecId && 
		MFX_CODEC_AVC   != pParams->CodecId && 
		MFX_CODEC_VC1   != pParams->CodecId) {
		PrintHelp(strInput[0], _T("Unknown codec"), NULL);
		return MFX_PRINT_OPTION_ERR;
	}

	// set default values for optional parameters that were not set or were set incorrectly
	pParams->nTargetUsage = clamp(pParams->nTargetUsage, MFX_TARGETUSAGE_BEST_QUALITY, MFX_TARGETUSAGE_BEST_SPEED);

	// calculate default bitrate based on the resolution (a parameter for encoder, so Dst resolution is used)
	if (pParams->nBitRate == 0) {
		pParams->nBitRate = CalculateDefaultBitrate(pParams->CodecId, pParams->nTargetUsage, pParams->nDstWidth,
			pParams->nDstHeight, pParams->nFPSRate / (double)pParams->nFPSScale);
	}

	// if nv12 option isn't specified, input YUV file is expected to be in YUV420 color format
	if (!pParams->ColorFormat) {
		pParams->ColorFormat = MFX_FOURCC_YV12;
	}

	//if picstruct not set, progressive frame is expected
	if (!pParams->nPicStruct) {
		pParams->nPicStruct = MFX_PICSTRUCT_PROGRESSIVE;
	}

	mfxVersion mfxlib_hw = get_mfx_libhw_version();
	mfxVersion mfxlib_sw = get_mfx_libsw_version();
	//check if dll exists
	if (pParams->bUseHWLib && (check_lib_version(mfxlib_hw, MFX_LIB_VERSION_1_1) == 0)) {
		PrintHelp(strInput[0], _T("QuickSyncVideo (hw encoding) unavailable"), NULL);
		return MFX_PRINT_OPTION_ERR;
	}

	if (!pParams->bUseHWLib && (check_lib_version(mfxlib_sw, MFX_LIB_VERSION_1_1) == 0)) {
#ifdef _M_IX86
		PrintHelp(strInput[0], _T("software encoding unavailable. Please Check for libmfxsw32.dll."), NULL);
#else
		PrintHelp(strInput[0], _T("software encoding unavailable. Please Check for libmfxsw64.dll."), NULL);
#endif
		return MFX_PRINT_OPTION_ERR;
	}
	//check for API 1.3 options
	mfxVersion mfxlib_current = (pParams->bUseHWLib) ? mfxlib_hw : mfxlib_sw;
	if (!check_lib_version(mfxlib_current, MFX_LIB_VERSION_1_3)) {
#define PRINT_API_1_3_SUPPORT_ERROR(option_name) { PrintAPISupportError(option_name, mfxlib_current, MFX_LIB_VERSION_1_3, pParams->bUseHWLib); return MFX_PRINT_OPTION_ERR; }
		if (pParams->bFullrange)                               PRINT_API_1_3_SUPPORT_ERROR(_T("--fullrange"));
		if (pParams->Transfer    != list_transfer[0].value)    PRINT_API_1_3_SUPPORT_ERROR(_T("--transfer"));
		if (pParams->VideoFormat != list_videoformat[0].value) PRINT_API_1_3_SUPPORT_ERROR(_T("--videoformat"));
		if (pParams->ColorMatrix != list_colormatrix[0].value) PRINT_API_1_3_SUPPORT_ERROR(_T("--colormatrix"));
		if (pParams->ColorPrim   != list_colorprim[0].value)   PRINT_API_1_3_SUPPORT_ERROR(_T("--colorprim"));
		if (pParams->nEncMode    == MFX_RATECONTROL_AVBR)      PRINT_API_1_3_SUPPORT_ERROR(_T("--avbr"));
#undef PRINT_API_1_3_SUPPORT_ERROR
	}

	//don't use d3d memory with software encoding
	if (!pParams->bUseHWLib) {
		pParams->memType = SYSTEM_MEMORY;
	}

	//set input buffer size
	if (tmp_input_buf == 0)
		tmp_input_buf = (pParams->bUseHWLib) ? QSV_DEFAULT_INPUT_BUF_HW : QSV_DEFAULT_INPUT_BUF_SW;
	pParams->nInputBufSize = clamp(tmp_input_buf, QSV_INPUT_BUF_MIN, QSV_INPUT_BUF_MAX);

	if (pParams->nRotationAngle != 0 && pParams->nRotationAngle != 180) {
		PrintHelp(strInput[0], _T("Angles other than 180 degrees are not supported."), NULL);
		return MFX_PRINT_OPTION_ERR; // other than 180 are not supported 
	}

	// not all options are supported if rotate plugin is enabled
	if (pParams->nRotationAngle == 180) 
	{
		if (MFX_FOURCC_NV12 != pParams->ColorFormat)
		{
			PrintHelp(strInput[0], _T("Rotation plugin requires NV12 input. Please specify -nv12 option."), NULL);
			return MFX_PRINT_OPTION_ERR;
		}
		pParams->nPicStruct = MFX_PICSTRUCT_PROGRESSIVE;
		pParams->nDstWidth = pParams->nWidth;
		pParams->nDstHeight = pParams->nHeight;
		pParams->memType = SYSTEM_MEMORY;
	}

	return MFX_ERR_NONE;
}

//Ctrl + C ハンドラ
static bool g_signal_abort = false;
#pragma warning(push)
#pragma warning(disable:4100)
static void sigcatch(int sig) {
	g_signal_abort = true;
}
#pragma warning(pop)
static int set_signal_handler() {
	int ret = 0;
	if (SIG_ERR == signal(SIGINT, sigcatch)) {
		_ftprintf(stderr, _T("failed to set signal handler.\n"));
	}
	return ret;
}


int _tmain(int argc, TCHAR *argv[])
{
	sInputParams        Params;   // input parameters from command line
	std::auto_ptr<CEncodingPipeline>  pPipeline; 

	mfxStatus sts = MFX_ERR_NONE; // return value check

	sts = ParseInputString(argv, (mfxU8)argc, &Params);
	if (sts >= MFX_PRINT_OPTION_DONE)
		return sts - MFX_PRINT_OPTION_DONE;
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, 1);

	//set stdin to binary mode when using pipe input
	if (_tcscmp(Params.strSrcFile, _T("-")) == NULL) {
		if (_setmode( _fileno( stdin ), _O_BINARY ) == 1) {
			PrintHelp(argv[0], _T("failed to switch stdin to binary mode."), NULL);
			return 1;
		}
	}

	//set stdout to binary mode when using pipe output
	if (_tcscmp(Params.strDstFile, _T("-")) == NULL) {
		if (_setmode( _fileno( stdout ), _O_BINARY ) == 1) {
			PrintHelp(argv[0], _T("failed to switch stdout to binary mode."), NULL);
			return 1;
		}
	}

	//pPipeline.reset((Params.nRotationAngle) ? new CUserPipeline : new CEncodingPipeline);
	pPipeline.reset(new CEncodingPipeline);
	MSDK_CHECK_POINTER(pPipeline.get(), MFX_ERR_MEMORY_ALLOC);

	sts = pPipeline->Init(&Params);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, 1);

	if (Params.pStrLogFile) {
		free(Params.pStrLogFile);
		Params.pStrLogFile = NULL;
	}

	pPipeline->SetAbortFlagPointer(&g_signal_abort);
	set_signal_handler();

	pPipeline->CheckCurrentVideoParam();
	_ftprintf(stderr, _T("Processing started\n"));

	for (;;)
	{
		sts = pPipeline->Run();

		if (MFX_ERR_DEVICE_LOST == sts || MFX_ERR_DEVICE_FAILED == sts)
		{
			_ftprintf(stderr, _T("\nERROR: Hardware device was lost or returned an unexpected error. Recovering...\n"));
			sts = pPipeline->ResetDevice();
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, 1);

			sts = pPipeline->ResetMFXComponents(&Params);
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, 1);
			continue;
		}
		else
		{
			MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, 1);
			break;
		}
	}

	pPipeline->Close();  
	_ftprintf(stderr, _T("\nProcessing finished\n"));

	return 0;
}