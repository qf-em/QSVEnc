﻿//  -----------------------------------------------------------------------------------------
//    QSVEnc by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#ifndef _AUO_SYSTEM_H_
#define _AUO_SYSTEM_H_

#include <Windows.h>
#include "auo.h"
#include "auo_settings.h"
#include "auo_conf.h"

#include "mfxstructures.h"

#if _M_IX86
#define ALIGN_PTR __declspec(align(4))
#else
#define ALIGN_PTR __declspec(align(8))
#endif

typedef struct ALIGN_PTR {
	HANDLE ALIGN_PTR he_aud_start; //InterlockedExchangeを使用するため、__declspec(align(4))が必要
	HANDLE ALIGN_PTR he_vid_start; //InterlockedExchangeを使用するため、__declspec(align(4))が必要
	HANDLE th_aud;
	void  *buffer;
	DWORD  buf_len;
	DWORD  buf_max_size;
	int    start;
	int    get_length;
	BOOL   abort;
} AUD_PARALLEL_ENC;

typedef struct {
	AUD_PARALLEL_ENC aud_parallel;         //音声並列処理の管理
	int video_out_type;                    //出力する動画のフォーマット(拡張子により判断)
	int muxer_to_be_used;                  //使用するmuxerのインデックス
	int drop_count;                        //ドロップ数
	BOOL afs_init;                         //動画入力の準備ができているか
	HANDLE h_p_aviutl;                     //優先度取得用のAviutlのハンドル
	char temp_filename[MAX_PATH_LEN];      //一時ファイル名
	char muxed_vid_filename[MAX_PATH_LEN]; //mux後に退避された動画のみファイル
	int  aud_count;                        //音声ファイル数...音声エンコード段階で設定する
	                                       //auo_mux.cppのenable_aud_muxの制限から31以下
	char aud_temp_dir[MAX_PATH_LEN];       //音声一時ディレクトリ
	FILE_APPENDIX append;                  //ファイル名に追加する文字列のリスト
} PRM_ENC;

typedef struct {
	BOOL init;
	char auo_path[MAX_PATH_LEN];    //QSVEnc.auoのフルパス
	char aviutl_dir[MAX_PATH_LEN];  //Aviutlのディレクトリ(\無し)
	guiEx_settings *exstg;          //ini設定
	mfxVersion sw_ver;              //ソフトウェアのライブラリのバージョン
	mfxVersion hw_ver;              //ハードウェアのライブラリのバージョン
} SYSTEM_DATA;

void init_SYSTEM_DATA(SYSTEM_DATA *_sys_dat);
void delete_SYSTEM_DATA(SYSTEM_DATA *_sys_dat);

#endif //_AUO_SYSTEM_H_