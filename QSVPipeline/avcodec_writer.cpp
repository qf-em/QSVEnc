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

#include <fcntl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include "qsv_osdep.h"
#include "qsv_util.h"
#include "qsv_control.h"
#include "avcodec_writer.h"
#include "avcodec_qsv_log.h"

#define USE_AVCODECPAR 1

#if ENABLE_AVCODEC_QSV_READER
#if USE_CUSTOM_IO
static int funcReadPacket(void *opaque, uint8_t *buf, int buf_size) {
    CAvcodecWriter *writer = reinterpret_cast<CAvcodecWriter *>(opaque);
    return writer->readPacket(buf, buf_size);
}
static int funcWritePacket(void *opaque, uint8_t *buf, int buf_size) {
    CAvcodecWriter *writer = reinterpret_cast<CAvcodecWriter *>(opaque);
    return writer->writePacket(buf, buf_size);
}
static int64_t funcSeek(void *opaque, int64_t offset, int whence) {
    CAvcodecWriter *writer = reinterpret_cast<CAvcodecWriter *>(opaque);
    return writer->seek(offset, whence);
}
#endif //USE_CUSTOM_IO

CAvcodecWriter::CAvcodecWriter() {
    QSV_MEMSET_ZERO(m_Mux.format);
    QSV_MEMSET_ZERO(m_Mux.video);
    m_strWriterName = _T("avout");
}

CAvcodecWriter::~CAvcodecWriter() {
    Close();
}

void CAvcodecWriter::CloseSubtitle(AVMuxSub *pMuxSub) {
    //close decoder
    if (pMuxSub->pOutCodecDecodeCtx) {
        avcodec_close(pMuxSub->pOutCodecDecodeCtx);
        av_free(pMuxSub->pOutCodecDecodeCtx);
        AddMessage(QSV_LOG_DEBUG, _T("Closed pOutCodecDecodeCtx.\n"));
    }

    //close encoder
    if (pMuxSub->pOutCodecEncodeCtx) {
        avcodec_close(pMuxSub->pOutCodecEncodeCtx);
        av_free(pMuxSub->pOutCodecEncodeCtx);
        AddMessage(QSV_LOG_DEBUG, _T("Closed pOutCodecEncodeCtx.\n"));
    }
    if (pMuxSub->pBuf) {
        av_free(pMuxSub->pBuf);
    }

    memset(pMuxSub, 0, sizeof(pMuxSub[0]));
    AddMessage(QSV_LOG_DEBUG, _T("Closed subtitle.\n"));
}

void CAvcodecWriter::CloseAudio(AVMuxAudio *pMuxAudio) {
    //close resampler
    if (pMuxAudio->pSwrContext) {
        swr_free(&pMuxAudio->pSwrContext);
        AddMessage(QSV_LOG_DEBUG, _T("Closed pSwrContext.\n"));
    }
    if (pMuxAudio->pSwrBuffer) {
        if (pMuxAudio->pSwrBuffer[0]) {
            av_free(pMuxAudio->pSwrBuffer[0]);
        }
        av_free(pMuxAudio->pSwrBuffer);
    }

    //close decoder
    if (pMuxAudio->pOutCodecDecodeCtx
        && pMuxAudio->nInSubStream == 0) { //サブストリームのものは単なるコピーなので開放不要
        avcodec_close(pMuxAudio->pOutCodecDecodeCtx);
        av_free(pMuxAudio->pOutCodecDecodeCtx);
        AddMessage(QSV_LOG_DEBUG, _T("Closed pOutCodecDecodeCtx.\n"));
    }

    //close encoder
    if (pMuxAudio->pOutCodecEncodeCtx) {
        avcodec_close(pMuxAudio->pOutCodecEncodeCtx);
        av_free(pMuxAudio->pOutCodecEncodeCtx);
        AddMessage(QSV_LOG_DEBUG, _T("Closed pOutCodecEncodeCtx.\n"));
    }

    //close filter
    if (pMuxAudio->pFilterGraph) {
        avfilter_graph_free(&pMuxAudio->pFilterGraph);
    }

    //free packet
    if (pMuxAudio->OutPacket.data) {
        av_packet_unref(&pMuxAudio->OutPacket);
    }
    if (pMuxAudio->pAACBsfc) {
        av_bsf_free(&pMuxAudio->pAACBsfc);
    }
    if (pMuxAudio->pCodecCtxIn) {
        avcodec_free_context(&pMuxAudio->pCodecCtxIn);
        AddMessage(QSV_LOG_DEBUG, _T("Closed AVCodecConetxt.\n"));
    }
    memset(pMuxAudio, 0, sizeof(pMuxAudio[0]));
    AddMessage(QSV_LOG_DEBUG, _T("Closed audio.\n"));
}

void CAvcodecWriter::CloseVideo(AVMuxVideo *pMuxVideo) {
    if (m_Mux.video.fpTsLogFile) {
        fclose(m_Mux.video.fpTsLogFile);
    }
    memset(pMuxVideo, 0, sizeof(pMuxVideo[0]));
    AddMessage(QSV_LOG_DEBUG, _T("Closed video.\n"));
}

void CAvcodecWriter::CloseFormat(AVMuxFormat *pMuxFormat) {
    if (pMuxFormat->pFormatCtx) {
        if (!pMuxFormat->bStreamError) {
            av_write_trailer(pMuxFormat->pFormatCtx);
        }
#if USE_CUSTOM_IO
        if (!pMuxFormat->fpOutput) {
#endif
            avio_close(pMuxFormat->pFormatCtx->pb);
            AddMessage(QSV_LOG_DEBUG, _T("Closed AVIO Context.\n"));
#if USE_CUSTOM_IO
        }
#endif
        avformat_free_context(pMuxFormat->pFormatCtx);
        AddMessage(QSV_LOG_DEBUG, _T("Closed avformat context.\n"));
    }
#if USE_CUSTOM_IO
    if (pMuxFormat->fpOutput) {
        fflush(pMuxFormat->fpOutput);
        fclose(pMuxFormat->fpOutput);
        AddMessage(QSV_LOG_DEBUG, _T("Closed File Pointer.\n"));
    }

    if (pMuxFormat->pAVOutBuffer) {
        av_free(pMuxFormat->pAVOutBuffer);
    }

    if (pMuxFormat->pOutputBuffer) {
        free(pMuxFormat->pOutputBuffer);
    }
#endif //USE_CUSTOM_IO
    memset(pMuxFormat, 0, sizeof(pMuxFormat[0]));
    AddMessage(QSV_LOG_DEBUG, _T("Closed format.\n"));
}

void CAvcodecWriter::CloseQueues() {
#if ENABLE_AVCODEC_OUT_THREAD
    m_Mux.thread.bThAudEncodeAbort = true;
    m_Mux.thread.bThAudProcessAbort = true;
    m_Mux.thread.bAbortOutput = true;
    m_Mux.thread.qVideobitstream.close();
    m_Mux.thread.qVideobitstreamFreeI.close([](mfxBitstream *bitstream) { mfxBitstreamClear(bitstream); });
    m_Mux.thread.qVideobitstreamFreePB.close([](mfxBitstream *bitstream) { mfxBitstreamClear(bitstream); });
    m_Mux.thread.qAudioPacketOut.close();
    m_Mux.thread.qAudioFrameEncode.close();
    m_Mux.thread.qAudioPacketProcess.close();
    AddMessage(QSV_LOG_DEBUG, _T("closed queues...\n"));
#endif
}

void CAvcodecWriter::CloseThread() {
#if ENABLE_AVCODEC_OUT_THREAD
    m_Mux.thread.bThAudEncodeAbort = true;
    if (m_Mux.thread.thAudEncode.joinable()) {
        //下記同様に、m_Mux.thread.heEventThOutputClosingがセットされるまで、
        //SetEvent(m_Mux.thread.heEventThOutputPktAdded)を実行し続ける必要がある。
        while (WAIT_TIMEOUT == WaitForSingleObject(m_Mux.thread.heEventClosingAudEncode, 100)) {
            SetEvent(m_Mux.thread.heEventPktAddedAudEncode);
        }
        m_Mux.thread.thAudEncode.join();
        CloseEvent(m_Mux.thread.heEventPktAddedAudEncode);
        CloseEvent(m_Mux.thread.heEventClosingAudEncode);
        AddMessage(QSV_LOG_DEBUG, _T("closed audio encode thread...\n"));
    }
    m_Mux.thread.bThAudProcessAbort = true;
    if (m_Mux.thread.thAudProcess.joinable()) {
        //下記同様に、m_Mux.thread.heEventThOutputClosingがセットされるまで、
        //SetEvent(m_Mux.thread.heEventThOutputPktAdded)を実行し続ける必要がある。
        while (WAIT_TIMEOUT == WaitForSingleObject(m_Mux.thread.heEventClosingAudProcess, 100)) {
            SetEvent(m_Mux.thread.heEventPktAddedAudProcess);
        }
        m_Mux.thread.thAudProcess.join();
        CloseEvent(m_Mux.thread.heEventPktAddedAudProcess);
        CloseEvent(m_Mux.thread.heEventClosingAudProcess);
        AddMessage(QSV_LOG_DEBUG, _T("closed audio process thread...\n"));
    }
    m_Mux.thread.bAbortOutput = true;
    if (m_Mux.thread.thOutput.joinable()) {
        //ここに来た時に、まだメインスレッドがループ中の可能性がある
        //その場合、SetEvent(m_Mux.thread.heEventPktAddedOutput)を一度やるだけだと、
        //そのあとにResetEvent(m_Mux.thread.heEventPktAddedOutput)が発生してしまい、
        //ここでスレッドが停止してしまう。
        //これを回避するため、m_Mux.thread.heEventClosingOutputがセットされるまで、
        //SetEvent(m_Mux.thread.heEventPktAddedOutput)を実行し続ける必要がある。
        while (WAIT_TIMEOUT == WaitForSingleObject(m_Mux.thread.heEventClosingOutput, 100)) {
            SetEvent(m_Mux.thread.heEventPktAddedOutput);
        }
        m_Mux.thread.thOutput.join();
        CloseEvent(m_Mux.thread.heEventPktAddedOutput);
        CloseEvent(m_Mux.thread.heEventClosingOutput);
        AddMessage(QSV_LOG_DEBUG, _T("closed output thread...\n"));
    }
    CloseQueues();
    m_Mux.thread.bAbortOutput = false;
    m_Mux.thread.bThAudProcessAbort = false;
    m_Mux.thread.bThAudEncodeAbort = false;
#endif
}

void CAvcodecWriter::Close() {
    AddMessage(QSV_LOG_DEBUG, _T("Closing...\n"));
    CloseThread();
    CloseFormat(&m_Mux.format);
    for (int i = 0; i < (int)m_Mux.audio.size(); i++) {
        CloseAudio(&m_Mux.audio[i]);
    }
    m_Mux.audio.clear();
    for (int i = 0; i < (int)m_Mux.sub.size(); i++) {
        CloseSubtitle(&m_Mux.sub[i]);
    }
    m_Mux.sub.clear();
    CloseVideo(&m_Mux.video);
    m_strOutputInfo.clear();
    m_pEncSatusInfo.reset();
    AddMessage(QSV_LOG_DEBUG, _T("Closed.\n"));
}

AVCodecID CAvcodecWriter::getAVCodecId(mfxU32 QSVFourcc) {
    for (int i = 0; i < _countof(QSV_DECODE_LIST); i++)
        if (QSV_DECODE_LIST[i].qsv_fourcc == QSVFourcc)
            return (AVCodecID)QSV_DECODE_LIST[i].codec_id;
    return AV_CODEC_ID_NONE;
}
bool CAvcodecWriter::codecIDIsPCM(AVCodecID targetCodec) {
    static const auto pcmCodecs = make_array<AVCodecID>(
        AV_CODEC_ID_FIRST_AUDIO,
        AV_CODEC_ID_PCM_S16LE,
        AV_CODEC_ID_PCM_S16BE,
        AV_CODEC_ID_PCM_U16LE,
        AV_CODEC_ID_PCM_U16BE,
        AV_CODEC_ID_PCM_S8,
        AV_CODEC_ID_PCM_U8,
        AV_CODEC_ID_PCM_MULAW,
        AV_CODEC_ID_PCM_ALAW,
        AV_CODEC_ID_PCM_S32LE,
        AV_CODEC_ID_PCM_S32BE,
        AV_CODEC_ID_PCM_U32LE,
        AV_CODEC_ID_PCM_U32BE,
        AV_CODEC_ID_PCM_S24LE,
        AV_CODEC_ID_PCM_S24BE,
        AV_CODEC_ID_PCM_U24LE,
        AV_CODEC_ID_PCM_U24BE,
        AV_CODEC_ID_PCM_S24DAUD,
        AV_CODEC_ID_PCM_ZORK,
        AV_CODEC_ID_PCM_S16LE_PLANAR,
        AV_CODEC_ID_PCM_DVD,
        AV_CODEC_ID_PCM_F32BE,
        AV_CODEC_ID_PCM_F32LE,
        AV_CODEC_ID_PCM_F64BE,
        AV_CODEC_ID_PCM_F64LE,
        AV_CODEC_ID_PCM_BLURAY,
        AV_CODEC_ID_PCM_LXF,
        AV_CODEC_ID_S302M,
        AV_CODEC_ID_PCM_S8_PLANAR,
        AV_CODEC_ID_PCM_S24LE_PLANAR,
        AV_CODEC_ID_PCM_S32LE_PLANAR,
        AV_CODEC_ID_PCM_S16BE_PLANAR
    );
    return (pcmCodecs.end() != std::find(pcmCodecs.begin(), pcmCodecs.end(), targetCodec));
}

AVCodecID CAvcodecWriter::PCMRequiresConversion(const AVCodecContext *audioCtx) {
    static const std::pair<AVCodecID, AVCodecID> pcmConvertCodecs[] = {
        { AV_CODEC_ID_FIRST_AUDIO,      AV_CODEC_ID_FIRST_AUDIO },
        { AV_CODEC_ID_PCM_DVD,          AV_CODEC_ID_FIRST_AUDIO },
        { AV_CODEC_ID_PCM_BLURAY,       AV_CODEC_ID_FIRST_AUDIO },
        { AV_CODEC_ID_PCM_S8_PLANAR,    AV_CODEC_ID_PCM_S8      },
        { AV_CODEC_ID_PCM_S16LE_PLANAR, AV_CODEC_ID_PCM_S16LE   },
        { AV_CODEC_ID_PCM_S16BE_PLANAR, AV_CODEC_ID_PCM_S16LE   },
        { AV_CODEC_ID_PCM_S16BE,        AV_CODEC_ID_PCM_S16LE   },
        { AV_CODEC_ID_PCM_S24LE_PLANAR, AV_CODEC_ID_PCM_S24LE   },
        { AV_CODEC_ID_PCM_S24BE,        AV_CODEC_ID_PCM_S24LE   },
        { AV_CODEC_ID_PCM_S32LE_PLANAR, AV_CODEC_ID_PCM_S32LE   },
        { AV_CODEC_ID_PCM_S32BE,        AV_CODEC_ID_PCM_S32LE   },
        { AV_CODEC_ID_PCM_F32BE,        AV_CODEC_ID_PCM_S32LE   },
        { AV_CODEC_ID_PCM_F64BE,        AV_CODEC_ID_PCM_S32LE   },
    };
    AVCodecID prmCodec = AV_CODEC_ID_NONE;
    for (int i = 0; i < _countof(pcmConvertCodecs); i++) {
        if (pcmConvertCodecs[i].first == audioCtx->codec_id) {
            if (pcmConvertCodecs[i].second != AV_CODEC_ID_FIRST_AUDIO) {
                return pcmConvertCodecs[i].second;
            }
            switch (audioCtx->bits_per_raw_sample) {
            case 32: prmCodec = AV_CODEC_ID_PCM_S32LE; break;
            case 24: prmCodec = AV_CODEC_ID_PCM_S24LE; break;
            case 8:  prmCodec = AV_CODEC_ID_PCM_S16LE; break;
            case 16:
            default: prmCodec = AV_CODEC_ID_PCM_S16LE; break;
            }
        }
    }
    if (prmCodec != AV_CODEC_ID_NONE) {
        AddMessage(QSV_LOG_DEBUG, _T("PCM requires conversion...\n"));
    }
    return prmCodec;
}

void CAvcodecWriter::SetExtraData(AVCodecContext *codecCtx, const uint8_t *data, uint32_t size) {
    if (data == nullptr || size == 0)
        return;
    if (codecCtx->extradata)
        av_free(codecCtx->extradata);
    codecCtx->extradata_size = size;
    codecCtx->extradata      = (uint8_t *)av_malloc(codecCtx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(codecCtx->extradata, data, size);
};

void CAvcodecWriter::SetExtraData(AVCodecParameters *pCodecParam, const uint8_t *data, uint32_t size) {
    if (data == nullptr || size == 0)
        return;
    if (pCodecParam->extradata)
        av_free(pCodecParam->extradata);
    pCodecParam->extradata_size = size;
    pCodecParam->extradata      = (uint8_t *)av_malloc(pCodecParam->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(pCodecParam->extradata, data, size);
};

//音声のchannel_layoutを自動選択する
uint64_t CAvcodecWriter::AutoSelectChannelLayout(const uint64_t *pChannelLayout, const AVCodecContext *pSrcAudioCtx) {
    int srcChannels = av_get_channel_layout_nb_channels(pSrcAudioCtx->channel_layout);
    if (srcChannels == 0) {
        srcChannels = pSrcAudioCtx->channels;
    }
    if (pChannelLayout == nullptr) {
        switch (srcChannels) {
        case 1:  return AV_CH_LAYOUT_MONO;
        case 2:  return AV_CH_LAYOUT_STEREO;
        case 3:  return AV_CH_LAYOUT_2_1;
        case 4:  return AV_CH_LAYOUT_QUAD;
        case 5:  return AV_CH_LAYOUT_5POINT0;
        case 6:  return AV_CH_LAYOUT_5POINT1;
        case 7:  return AV_CH_LAYOUT_6POINT1;
        case 8:  return AV_CH_LAYOUT_7POINT1;
        default: return AV_CH_LAYOUT_NATIVE;
        }
    }

    for (int i = 0; pChannelLayout[i]; i++) {
        if (srcChannels == av_get_channel_layout_nb_channels(pChannelLayout[i])) {
            return pChannelLayout[i];
        }
    }
    return pChannelLayout[0];
}

int CAvcodecWriter::AutoSelectSamplingRate(const int *pSamplingRateList, int nSrcSamplingRate) {
    if (pSamplingRateList == nullptr) {
        return nSrcSamplingRate;
    }
    //一致するものがあれば、それを返す
    int i = 0;
    for (; pSamplingRateList[i]; i++) {
        if (nSrcSamplingRate == pSamplingRateList[i]) {
            return nSrcSamplingRate;
        }
    }
    //相対誤差が最も小さいものを選択する
    vector<double> diffrate(i);
    for (i = 0; pSamplingRateList[i]; i++) {
        diffrate[i] = std::abs(1 - pSamplingRateList[i] / (double)nSrcSamplingRate);
    }
    return pSamplingRateList[std::distance(diffrate.begin(), std::min_element(diffrate.begin(), diffrate.end()))];
}

AVSampleFormat CAvcodecWriter::AutoSelectSampleFmt(const AVSampleFormat *pSamplefmtList, const AVCodecContext *pSrcAudioCtx) {
    AVSampleFormat srcFormat = pSrcAudioCtx->sample_fmt;
    if (pSamplefmtList == nullptr) {
        return srcFormat;
    }
    if (srcFormat == AV_SAMPLE_FMT_NONE) {
        return pSamplefmtList[0];
    }
    for (int i = 0; pSamplefmtList[i] >= 0; i++) {
        if (srcFormat == pSamplefmtList[i]) {
            return pSamplefmtList[i];
        }
    }
    static const auto sampleFmtLevel = make_array<std::pair<AVSampleFormat, int>>(
        std::make_pair(AV_SAMPLE_FMT_DBLP, 8),
        std::make_pair(AV_SAMPLE_FMT_DBL,  8),
        std::make_pair(AV_SAMPLE_FMT_FLTP, 6),
        std::make_pair(AV_SAMPLE_FMT_FLT,  6),
        std::make_pair(AV_SAMPLE_FMT_S32P, 4),
        std::make_pair(AV_SAMPLE_FMT_S32,  4),
        std::make_pair(AV_SAMPLE_FMT_S16P, 2),
        std::make_pair(AV_SAMPLE_FMT_S16,  2),
        std::make_pair(AV_SAMPLE_FMT_U8P,  1),
        std::make_pair(AV_SAMPLE_FMT_U8,   1)
    );
    int srcFormatLevel = std::find_if(sampleFmtLevel.begin(), sampleFmtLevel.end(),
        [srcFormat](const std::pair<AVSampleFormat, int>& targetFormat) { return targetFormat.first == srcFormat;})->second;
    auto foundFormat = std::find_if(sampleFmtLevel.begin(), sampleFmtLevel.end(),
        [srcFormatLevel](const std::pair<AVSampleFormat, int>& targetFormat) { return targetFormat.second == srcFormatLevel; });
    for (; foundFormat != sampleFmtLevel.end(); foundFormat++) {
        for (int i = 0; pSamplefmtList[i] >= 0; i++) {
            if (foundFormat->first == pSamplefmtList[i]) {
                return pSamplefmtList[i];
            }
        }
    }
    return pSamplefmtList[0];
}

mfxStatus CAvcodecWriter::InitVideo(const AvcodecWriterPrm *prm) {
    m_Mux.format.pFormatCtx->video_codec_id = getAVCodecId(prm->pVideoInfo->CodecId);
    if (m_Mux.format.pFormatCtx->video_codec_id == AV_CODEC_ID_NONE) {
        AddMessage(QSV_LOG_ERROR, _T("failed to find codec id for video.\n"));
        return MFX_ERR_NULL_PTR;
    }
    m_Mux.format.pFormatCtx->oformat->video_codec = m_Mux.format.pFormatCtx->video_codec_id;
    if (NULL == (m_Mux.video.pCodec = avcodec_find_decoder(m_Mux.format.pFormatCtx->video_codec_id))) {
        AddMessage(QSV_LOG_ERROR, _T("failed to codec for video.\n"));
        return MFX_ERR_NULL_PTR;
    }
    if (NULL == (m_Mux.video.pStream = avformat_new_stream(m_Mux.format.pFormatCtx, m_Mux.video.pCodec))) {
        AddMessage(QSV_LOG_ERROR, _T("failed to create new stream for video.\n"));
        return MFX_ERR_NULL_PTR;
    }
    m_Mux.video.nFPS = av_make_q(prm->pVideoInfo->FrameInfo.FrameRateExtN, prm->pVideoInfo->FrameInfo.FrameRateExtD);
    AddMessage(QSV_LOG_DEBUG, _T("output video stream fps: %d/%d\n"), prm->pVideoInfo->FrameInfo.FrameRateExtN, prm->pVideoInfo->FrameInfo.FrameRateExtD);

    m_Mux.video.pCodecCtx = m_Mux.video.pStream->codec;
#if USE_AVCODECPAR
    m_Mux.video.pStream->codecpar->codec_type              = AVMEDIA_TYPE_VIDEO;
    m_Mux.video.pStream->codecpar->codec_id                = m_Mux.format.pFormatCtx->video_codec_id;
    m_Mux.video.pStream->codecpar->width                   = prm->pVideoInfo->FrameInfo.CropW;
    m_Mux.video.pStream->codecpar->height                  = prm->pVideoInfo->FrameInfo.CropH;
    m_Mux.video.pStream->codecpar->format                  = AV_PIX_FMT_YUV420P;
    m_Mux.video.pStream->codecpar->level                   = prm->pVideoInfo->CodecLevel;
    m_Mux.video.pStream->codecpar->profile                 = prm->pVideoInfo->CodecProfile;
    m_Mux.video.pStream->codecpar->sample_aspect_ratio.num = prm->pVideoInfo->FrameInfo.AspectRatioW;
    m_Mux.video.pStream->codecpar->sample_aspect_ratio.den = prm->pVideoInfo->FrameInfo.AspectRatioH;
    m_Mux.video.pStream->codecpar->chroma_location         = AVCHROMA_LOC_LEFT;
    m_Mux.video.pStream->codecpar->field_order             = qsv_field_order(&prm->pVideoInfo->FrameInfo);
    m_Mux.video.pStream->codecpar->video_delay             = ((prm->pVideoInfo->GopRefDist - 1) > 0) + (((prm->pVideoInfo->GopRefDist - 1) > 0) & ((prm->pVideoInfo->GopRefDist - 1) > 2));
#else
    m_Mux.video.pCodecCtx->codec_type              = AVMEDIA_TYPE_VIDEO;
    m_Mux.video.pCodecCtx->codec_id                = m_Mux.format.pFormatCtx->video_codec_id;
    m_Mux.video.pCodecCtx->width                   = prm->pVideoInfo->FrameInfo.CropW;
    m_Mux.video.pCodecCtx->height                  = prm->pVideoInfo->FrameInfo.CropH;
    m_Mux.video.pCodecCtx->time_base               = av_inv_q(m_Mux.video.nFPS);
    m_Mux.video.pCodecCtx->pix_fmt                 = AV_PIX_FMT_YUV420P;
    m_Mux.video.pCodecCtx->compression_level       = FF_COMPRESSION_DEFAULT;
    m_Mux.video.pCodecCtx->level                   = prm->pVideoInfo->CodecLevel;
    m_Mux.video.pCodecCtx->profile                 = prm->pVideoInfo->CodecProfile;
    m_Mux.video.pCodecCtx->refs                    = prm->pVideoInfo->NumRefFrame;
    m_Mux.video.pCodecCtx->gop_size                = prm->pVideoInfo->GopPicSize;
    m_Mux.video.pCodecCtx->max_b_frames            = prm->pVideoInfo->GopRefDist - 1;
    m_Mux.video.pCodecCtx->chroma_sample_location  = AVCHROMA_LOC_LEFT;
    m_Mux.video.pCodecCtx->slice_count             = prm->pVideoInfo->NumSlice;
    m_Mux.video.pCodecCtx->sample_aspect_ratio.num = prm->pVideoInfo->FrameInfo.AspectRatioW;
    m_Mux.video.pCodecCtx->sample_aspect_ratio.den = prm->pVideoInfo->FrameInfo.AspectRatioH;
#endif //#if USE_AVCODECPAR
    m_Mux.video.pStream->sample_aspect_ratio.num   = prm->pVideoInfo->FrameInfo.AspectRatioW; //mkvではこちらの指定も必要
    m_Mux.video.pStream->sample_aspect_ratio.den   = prm->pVideoInfo->FrameInfo.AspectRatioH;
    if (prm->pVideoSignalInfo->ColourDescriptionPresent) {
#if USE_AVCODECPAR
        m_Mux.video.pStream->codecpar->color_space         = (AVColorSpace)prm->pVideoSignalInfo->MatrixCoefficients;
        m_Mux.video.pStream->codecpar->color_primaries     = (AVColorPrimaries)prm->pVideoSignalInfo->ColourPrimaries;
        m_Mux.video.pStream->codecpar->color_range         = (AVColorRange)(prm->pVideoSignalInfo->VideoFullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG);
        m_Mux.video.pStream->codecpar->color_trc           = (AVColorTransferCharacteristic)prm->pVideoSignalInfo->TransferCharacteristics;
#else
        m_Mux.video.pCodecCtx->colorspace          = (AVColorSpace)prm->pVideoSignalInfo->MatrixCoefficients;
        m_Mux.video.pCodecCtx->color_primaries     = (AVColorPrimaries)prm->pVideoSignalInfo->ColourPrimaries;
        m_Mux.video.pCodecCtx->color_range         = (AVColorRange)(prm->pVideoSignalInfo->VideoFullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG);
        m_Mux.video.pCodecCtx->color_trc           = (AVColorTransferCharacteristic)prm->pVideoSignalInfo->TransferCharacteristics;
#endif //#if USE_AVCODECPAR
    }
    if (0 > avcodec_open2(m_Mux.video.pCodecCtx, m_Mux.video.pCodec, NULL)) {
        AddMessage(QSV_LOG_ERROR, _T("failed to open codec for video.\n"));
        return MFX_ERR_NULL_PTR;
    }
    AddMessage(QSV_LOG_DEBUG, _T("opened video avcodec\n"));

    m_Mux.video.pStream->time_base = av_inv_q(m_Mux.video.nFPS);
    if (m_Mux.format.bIsMatroska) {
        m_Mux.video.pStream->time_base = av_make_q(1, 1000);
    }
    if (prm->pVideoInfo->FrameInfo.PicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF)) {
        m_Mux.video.pStream->time_base.den *= 2;
    }
#if !USE_AVCODECPAR
    m_Mux.video.pStream->codec->pkt_timebase = m_Mux.video.pStream->time_base;
    m_Mux.video.pStream->codec->time_base    = m_Mux.video.pStream->time_base;
    m_Mux.video.pStream->codec->framerate    = m_Mux.video.nFPS;
#endif
    m_Mux.video.pStream->start_time          = 0;

    m_Mux.video.bDtsUnavailable   = prm->bVideoDtsUnavailable;
    m_Mux.video.nInputFirstKeyPts = prm->nVideoInputFirstKeyPts;
    m_Mux.video.pInputCodecCtx    = prm->pVideoInputCodecCtx;

    if (prm->pMuxVidTsLogFile) {
        if (_tfopen_s(&m_Mux.video.fpTsLogFile, prm->pMuxVidTsLogFile, _T("a"))) {
            AddMessage(QSV_LOG_WARN, _T("failed to open mux timestamp log file: \"%s\""), prm->pMuxVidTsLogFile);
            m_Mux.video.fpTsLogFile = NULL;
        } else {
            AddMessage(QSV_LOG_DEBUG, _T("Opened mux timestamp log file: \"%s\""), prm->pMuxVidTsLogFile);
            tstring strFileHeadSep;
            for (int i = 0; i < 78; i++) {
                strFileHeadSep += _T("-");
            }
            _ftprintf(m_Mux.video.fpTsLogFile, strFileHeadSep.c_str());
            _ftprintf(m_Mux.video.fpTsLogFile, _T("%s\n"), m_Mux.format.pFilename);
            _ftprintf(m_Mux.video.fpTsLogFile, strFileHeadSep.c_str());
        }
    }

    AddMessage(QSV_LOG_DEBUG, _T("output video stream timebase: %d/%d\n"), m_Mux.video.pStream->time_base.num, m_Mux.video.pStream->time_base.den);
    AddMessage(QSV_LOG_DEBUG, _T("bDtsUnavailable: %s\n"), (m_Mux.video.bDtsUnavailable) ? _T("on") : _T("off"));
    return MFX_ERR_NONE;
}

//音声フィルタの初期化
mfxStatus CAvcodecWriter::InitAudioFilter(AVMuxAudio *pMuxAudio, int channels, uint64_t channel_layout, int sample_rate, AVSampleFormat sample_fmt) {
    //必要ならfilterを初期化
    if (pMuxAudio->pFilter
        && (pMuxAudio->nFilterInChannels      != channels
         || pMuxAudio->nFilterInChannelLayout != channel_layout
         || pMuxAudio->nFilterInSampleRate    != sample_rate
         || pMuxAudio->FilterInSampleFmt      != sample_fmt)) {
        if (pMuxAudio->pFilterGraph) {
            AVPktMuxData pktData = { 0 };
            pktData.pMuxAudio = pMuxAudio;
            pktData.type = MUX_DATA_TYPE_FRAME;
            pktData.got_result = TRUE;
            pktData.pFrame = nullptr;
            //filterをflush
            AudioFilterFrame(&pktData);

            if (pktData.pFrame) {
                //取得したフレームを書き出し
                WriteNextPacketToAudioSubtracks(&pktData);
            }

            //filterをclose
            avfilter_graph_free(&pMuxAudio->pFilterGraph);
        }
        pMuxAudio->nFilterInChannels      = channels;
        pMuxAudio->nFilterInChannelLayout = channel_layout;
        pMuxAudio->nFilterInSampleRate    = sample_rate;
        pMuxAudio->FilterInSampleFmt      = sample_fmt;

        int ret = 0;
        pMuxAudio->pFilterGraph = avfilter_graph_alloc();
        av_opt_set_int(pMuxAudio->pFilterGraph, "threads", 1, 0);

        AVFilterInOut *inputs = nullptr;
        AVFilterInOut *outputs = nullptr;
        if (0 > (ret = avfilter_graph_parse2(pMuxAudio->pFilterGraph, tchar_to_string(pMuxAudio->pFilter).c_str(), &inputs, &outputs))) {
            AddMessage(QSV_LOG_ERROR, _T("Failed to parse filter description: %s: \"%s\"\n"), qsv_av_err2str(ret).c_str(), pMuxAudio->pFilter);
            return MFX_ERR_UNSUPPORTED;
        }
        AddMessage(QSV_LOG_DEBUG, _T("Parsed filter: %s\n"), pMuxAudio->pFilter);

        const int nOutputCount = !!outputs + (outputs && outputs->next);
        const int nInputCount = !!inputs  + (inputs  && inputs->next);
        if (nOutputCount != 1 || nInputCount != 1) {
            const TCHAR *pFilterCountStr[] = { _T("0"), _T("1"), _T(">1") };
            AddMessage(QSV_LOG_ERROR, _T("filtergraph has %s input(s) and %s output(s).\n"), pFilterCountStr[nInputCount], pFilterCountStr[nOutputCount]);
            AddMessage(QSV_LOG_ERROR, _T("only 1 in -> 1 out filtering is supported.\n"));
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return MFX_ERR_UNSUPPORTED;
        }

        const auto args = strsprintf("time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%I64x",
            pMuxAudio->pOutCodecDecodeCtx->pkt_timebase.num, pMuxAudio->pOutCodecDecodeCtx->pkt_timebase.den,
            sample_rate, av_get_sample_fmt_name(sample_fmt), channel_layout);
        const AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
        const auto inName = strsprintf("in_track_%d.%d", pMuxAudio->nInTrackId, pMuxAudio->nInSubStream);
        if (0 > (ret = avfilter_graph_create_filter(&pMuxAudio->pFilterBufferSrcCtx, abuffersrc, inName.c_str(), args.c_str(), nullptr, pMuxAudio->pFilterGraph))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to create abuffer: %s.\n"), qsv_av_err2str(ret).c_str());
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return MFX_ERR_UNSUPPORTED;
        }
        if (0 > (ret = avfilter_link(pMuxAudio->pFilterBufferSrcCtx, 0, inputs->filter_ctx, inputs->pad_idx))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to link abuffer: %s.\n"), qsv_av_err2str(ret).c_str());
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return MFX_ERR_UNKNOWN;
        }
        avfilter_inout_free(&inputs);
        AddMessage(QSV_LOG_DEBUG, _T("filter linked with src buffer.\n"));

        const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
        const auto outName = strsprintf("out_track_%d.%d", pMuxAudio->nInTrackId, pMuxAudio->nInSubStream);
        if (0 > (ret = avfilter_graph_create_filter(&pMuxAudio->pFilterBufferSinkCtx, abuffersink, outName.c_str(), nullptr, nullptr, pMuxAudio->pFilterGraph))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to create abuffersink: %s.\n"), qsv_av_err2str(ret).c_str());
            avfilter_inout_free(&outputs);
            return MFX_ERR_UNSUPPORTED;
        }
        if (0 > (ret = av_opt_set_int(pMuxAudio->pFilterBufferSinkCtx, "all_channel_counts", 1, AV_OPT_SEARCH_CHILDREN))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to set channel counts to abuffersink: %s.\n"), qsv_av_err2str(ret).c_str());
            avfilter_inout_free(&outputs);
            return MFX_ERR_UNSUPPORTED;
        }
        if (0 > (ret = avfilter_link(outputs->filter_ctx, outputs->pad_idx, pMuxAudio->pFilterBufferSinkCtx, 0))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to link abuffersink: %s.\n"), qsv_av_err2str(ret).c_str());
            avfilter_inout_free(&outputs);
            return MFX_ERR_UNKNOWN;
        }
        AddMessage(QSV_LOG_DEBUG, _T("filter linked with sink buffer.\n"));

        avfilter_inout_free(&outputs);
        if (0 > (ret = avfilter_graph_config(pMuxAudio->pFilterGraph, nullptr))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to configure filter graph: %s.\n"), qsv_av_err2str(ret).c_str());
            return MFX_ERR_UNKNOWN;
        }
        AddMessage(QSV_LOG_DEBUG, _T("filter config done, filter ready.\n"));
    }
    return MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::InitAudioResampler(AVMuxAudio *pMuxAudio, int channels, uint64_t channel_layout, int sample_rate, AVSampleFormat sample_fmt) {
    if (   pMuxAudio->nResamplerInChannels      != channels
        || pMuxAudio->nResamplerInSampleRate    != sample_rate
        || pMuxAudio->ResamplerInSampleFmt      != sample_fmt) {
        if (pMuxAudio->pSwrContext != nullptr) {
            AVPktMuxData pktData = { 0 };
            pktData.pMuxAudio = pMuxAudio;
            pktData.type = MUX_DATA_TYPE_FRAME;
            pktData.got_result = TRUE;
            pktData.pFrame = nullptr;
            //resamplerをflush
            WriteNextPacketAudioFrame(&pktData);
            swr_free(&pMuxAudio->pSwrContext);
            AddMessage(QSV_LOG_DEBUG, _T("Cleared resampler for track %d.%d\n"), pMuxAudio->nInTrackId, pMuxAudio->nInSubStream);
        }
        pMuxAudio->nResamplerInChannels      = channels;
        pMuxAudio->nResamplerInChannelLayout = channel_layout;
        pMuxAudio->nResamplerInSampleRate    = sample_rate;
        pMuxAudio->ResamplerInSampleFmt      = sample_fmt;
        pMuxAudio->pSwrContext = swr_alloc();
        av_opt_set_int       (pMuxAudio->pSwrContext, "in_channel_count",   channels,       0);
        av_opt_set_int       (pMuxAudio->pSwrContext, "in_channel_layout",  channel_layout, 0);
        av_opt_set_int       (pMuxAudio->pSwrContext, "in_sample_rate",     sample_rate,    0);
        av_opt_set_sample_fmt(pMuxAudio->pSwrContext, "in_sample_fmt",      sample_fmt,     0);
        av_opt_set_int       (pMuxAudio->pSwrContext, "out_channel_count",  pMuxAudio->pOutCodecEncodeCtx->channels,       0);
        av_opt_set_int       (pMuxAudio->pSwrContext, "out_channel_layout", pMuxAudio->pOutCodecEncodeCtx->channel_layout, 0);
        av_opt_set_int       (pMuxAudio->pSwrContext, "out_sample_rate",    pMuxAudio->pOutCodecEncodeCtx->sample_rate,    0);
        av_opt_set_sample_fmt(pMuxAudio->pSwrContext, "out_sample_fmt",     pMuxAudio->pOutCodecEncodeCtx->sample_fmt,     0);
        if (pMuxAudio->nAudioResampler == QSV_RESAMPLER_SOXR) {
            av_opt_set       (pMuxAudio->pSwrContext, "resampler",          "soxr",                                        0);
        }

        AddMessage(QSV_LOG_DEBUG, _T("Creating audio resampler [%s] for track %d.%d: %s, %dch [%s], %.1fkHz -> %s, %dch [%s], %.1fkHz\n"),
            get_chr_from_value(list_resampler, pMuxAudio->nAudioResampler),
            pMuxAudio->nInTrackId, pMuxAudio->nInSubStream,
            char_to_tstring(av_get_sample_fmt_name(pMuxAudio->pOutCodecDecodeCtx->sample_fmt)).c_str(),
            pMuxAudio->pOutCodecDecodeCtx->channels,
            getChannelLayoutString(pMuxAudio->pOutCodecDecodeCtx->channels, pMuxAudio->pOutCodecDecodeCtx->channel_layout).c_str(),
            pMuxAudio->pOutCodecDecodeCtx->sample_rate * 0.001,
            char_to_tstring(av_get_sample_fmt_name(pMuxAudio->pOutCodecEncodeCtx->sample_fmt)).c_str(),
            pMuxAudio->pOutCodecEncodeCtx->channels,
            getChannelLayoutString(pMuxAudio->pOutCodecEncodeCtx->channels, pMuxAudio->pOutCodecEncodeCtx->channel_layout).c_str(),
            pMuxAudio->pOutCodecEncodeCtx->sample_rate * 0.001);

        if (bSplitChannelsEnabled(pMuxAudio->pnStreamChannelSelect)
            && pMuxAudio->pnStreamChannelSelect[pMuxAudio->nInSubStream] != channel_layout
            && av_get_channel_layout_nb_channels(pMuxAudio->pnStreamChannelSelect[pMuxAudio->nInSubStream]) < channels) {
            //初期化
            for (int inChannel = 0; inChannel < _countof(pMuxAudio->channelMapping); inChannel++) {
                pMuxAudio->channelMapping[inChannel] = -1;
            }
            //オプションによって指定されている、入力音声から抽出するべき音声レイアウト
            const auto select_channel_layout = pMuxAudio->pnStreamChannelSelect[pMuxAudio->nInSubStream];
            const int select_channel_count = av_get_channel_layout_nb_channels(select_channel_layout);
            for (int inChannel = 0; inChannel < channels; inChannel++) {
                //オプションによって指定されているチャンネルレイアウトから、抽出する音声のチャンネルを順に取得する
                //実際には、「オプションによって指定されているチャンネルレイアウト」が入力音声に存在しない場合がある
                auto select_channel = av_channel_layout_extract_channel(select_channel_layout, std::min(inChannel, select_channel_count-1));
                //対象のチャンネルのインデックスを取得する
                auto select_channel_index = av_get_channel_layout_channel_index(pMuxAudio->pOutCodecDecodeCtx->channel_layout, select_channel);
                if (select_channel_index < 0) {
                    //対応するチャンネルがもともとの入力音声ストリームにはない場合
                    const auto nChannels = (std::min)(inChannel, av_get_channel_layout_nb_channels(pMuxAudio->pOutCodecDecodeCtx->channel_layout)-1);
                    //入力音声のストリームから、抽出する音声のチャンネルを順に取得する
                    select_channel = av_channel_layout_extract_channel(pMuxAudio->pOutCodecDecodeCtx->channel_layout, nChannels);
                    //対象のチャンネルのインデックスを取得する
                    select_channel_index = av_get_channel_layout_channel_index(pMuxAudio->pOutCodecDecodeCtx->channel_layout, select_channel);
                }
                pMuxAudio->channelMapping[inChannel] = select_channel_index;
            }
            if (QSV_LOG_DEBUG >= m_pPrintMes->getLogLevel()) {
                tstring channel_layout_str = strsprintf(_T("channel layout for track %d.%d:\n["), pMuxAudio->nInTrackId, pMuxAudio->nInSubStream);
                for (int inChannel = 0; inChannel < channels; inChannel++) {
                    channel_layout_str += strsprintf(_T("%4d"), pMuxAudio->channelMapping[inChannel]);
                }
                channel_layout_str += _T("]\n");
                AddMessage(QSV_LOG_DEBUG, channel_layout_str.c_str());
            }
            int ret = swr_set_channel_mapping(pMuxAudio->pSwrContext, pMuxAudio->channelMapping);
            if (ret < 0) {
                AddMessage(QSV_LOG_ERROR, _T("Failed to set channel mapping to the resampling context: %s\n"), qsv_av_err2str(ret).c_str());
                return MFX_ERR_UNKNOWN;
            }
        }

        int ret = swr_init(pMuxAudio->pSwrContext);
        if (ret < 0) {
            AddMessage(QSV_LOG_ERROR, _T("Failed to initialize the resampling context: %s\n"), qsv_av_err2str(ret).c_str());
            return MFX_ERR_UNKNOWN;
        }
        if (pMuxAudio->pSwrBuffer == nullptr) {
            pMuxAudio->nSwrBufferSize = 16384;
            if (0 >(ret = av_samples_alloc_array_and_samples(&pMuxAudio->pSwrBuffer, &pMuxAudio->nSwrBufferLinesize,
                pMuxAudio->pOutCodecEncodeCtx->channels, pMuxAudio->nSwrBufferSize, pMuxAudio->pOutCodecEncodeCtx->sample_fmt, 0))) {
                AddMessage(QSV_LOG_ERROR, _T("Failed to allocate buffer for resampling: %s\n"), qsv_av_err2str(ret).c_str());
                return MFX_ERR_UNKNOWN;
            }
        }
    }
    return MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::InitAudio(AVMuxAudio *pMuxAudio, AVOutputStreamPrm *pInputAudio, uint32_t nAudioIgnoreDecodeError) {
    pMuxAudio->pCodecCtxIn = avcodec_alloc_context3(NULL);
    avcodec_copy_context(pMuxAudio->pCodecCtxIn, pInputAudio->src.pCodecCtx);
    AddMessage(QSV_LOG_DEBUG, _T("start initializing audio ouput...\n"));
    AddMessage(QSV_LOG_DEBUG, _T("output stream index %d, trackId %d.%d, delay %d, \n"), pInputAudio->src.nIndex, pInputAudio->src.nTrackId, pInputAudio->src.nSubStreamId, pMuxAudio->nDelaySamplesOfAudio);
    AddMessage(QSV_LOG_DEBUG, _T("samplerate %d, stream pkt_timebase %d/%d\n"), pMuxAudio->pCodecCtxIn->sample_rate, pMuxAudio->pCodecCtxIn->pkt_timebase.num, pMuxAudio->pCodecCtxIn->pkt_timebase.den);

    if (NULL == (pMuxAudio->pStream = avformat_new_stream(m_Mux.format.pFormatCtx, NULL))) {
        AddMessage(QSV_LOG_ERROR, _T("failed to create new stream for audio.\n"));
        return MFX_ERR_NULL_PTR;
    }
    pMuxAudio->pDecodedFrameCache = nullptr;
    pMuxAudio->nIgnoreDecodeError = nAudioIgnoreDecodeError;
    pMuxAudio->nInTrackId = pInputAudio->src.nTrackId;
    pMuxAudio->nInSubStream = pInputAudio->src.nSubStreamId;
    pMuxAudio->nStreamIndexIn = pInputAudio->src.nIndex;
    pMuxAudio->nLastPtsIn = AV_NOPTS_VALUE;
    pMuxAudio->pFilter = pInputAudio->pFilter;
    memcpy(pMuxAudio->pnStreamChannelSelect, pInputAudio->src.pnStreamChannelSelect, sizeof(pInputAudio->src.pnStreamChannelSelect));
    memcpy(pMuxAudio->pnStreamChannelOut,    pInputAudio->src.pnStreamChannelOut,    sizeof(pInputAudio->src.pnStreamChannelOut));

    //音声がwavの場合、フォーマット変換が必要な場合がある
    AVCodecID codecId = AV_CODEC_ID_NONE;
    if (!avcodecIsCopy(pInputAudio->pEncodeCodec) || AV_CODEC_ID_NONE != (codecId = PCMRequiresConversion(pMuxAudio->pCodecCtxIn))) {
        //デコーダの作成は親ストリームのみ
        if (pMuxAudio->nInSubStream == 0) {
            //setup decoder
            if (NULL == (pMuxAudio->pOutCodecDecode = avcodec_find_decoder(pMuxAudio->pCodecCtxIn->codec_id))) {
                AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to find decoder"), pInputAudio->src.pCodecCtx->codec_id));
                AddMessage(QSV_LOG_ERROR, _T("Please use --check-decoders to check available decoder.\n"));
                return MFX_ERR_NULL_PTR;
            }
            if (NULL == (pMuxAudio->pOutCodecDecodeCtx = avcodec_alloc_context3(pMuxAudio->pOutCodecDecode))) {
                AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to get decode codec context"), pInputAudio->src.pCodecCtx->codec_id));
                return MFX_ERR_NULL_PTR;
            }
            //設定されていない必須情報があれば設定する
#define COPY_IF_ZERO(dst, src) { if ((dst)==0) (dst)=(src); }
            COPY_IF_ZERO(pMuxAudio->pOutCodecDecodeCtx->sample_rate,         pInputAudio->src.pCodecCtx->sample_rate);
            COPY_IF_ZERO(pMuxAudio->pOutCodecDecodeCtx->channels,            pInputAudio->src.pCodecCtx->channels);
            COPY_IF_ZERO(pMuxAudio->pOutCodecDecodeCtx->channel_layout,      pInputAudio->src.pCodecCtx->channel_layout);
            COPY_IF_ZERO(pMuxAudio->pOutCodecDecodeCtx->bits_per_raw_sample, pInputAudio->src.pCodecCtx->bits_per_raw_sample);
#undef COPY_IF_ZERO
            pMuxAudio->pOutCodecDecodeCtx->pkt_timebase = pInputAudio->src.pCodecCtx->pkt_timebase;
            SetExtraData(pMuxAudio->pOutCodecDecodeCtx, pInputAudio->src.pCodecCtx->extradata, pInputAudio->src.pCodecCtx->extradata_size);
            if (nullptr != strstr(pMuxAudio->pOutCodecDecode->name, "wma")) {
                pMuxAudio->pOutCodecDecodeCtx->block_align = pInputAudio->src.pCodecCtx->block_align;
            }
            int ret;
            if (0 > (ret = avcodec_open2(pMuxAudio->pOutCodecDecodeCtx, pMuxAudio->pOutCodecDecode, NULL))) {
                AddMessage(QSV_LOG_ERROR, _T("failed to open decoder for %s: %s\n"),
                    char_to_tstring(avcodec_get_name(pInputAudio->src.pCodecCtx->codec_id)).c_str(), qsv_av_err2str(ret).c_str());
                return MFX_ERR_NULL_PTR;
            }
            AddMessage(QSV_LOG_DEBUG, _T("Audio Decoder opened\n"));
            AddMessage(QSV_LOG_DEBUG, _T("Audio Decode Info: %s, %dch[0x%02x], %.1fkHz, %s, %d/%d\n"), char_to_tstring(avcodec_get_name(pMuxAudio->pCodecCtxIn->codec_id)).c_str(),
                pMuxAudio->pOutCodecDecodeCtx->channels, (uint32_t)pMuxAudio->pOutCodecDecodeCtx->channel_layout, pMuxAudio->pOutCodecDecodeCtx->sample_rate / 1000.0,
                char_to_tstring(av_get_sample_fmt_name(pMuxAudio->pOutCodecDecodeCtx->sample_fmt)).c_str(),
                pMuxAudio->pOutCodecDecodeCtx->pkt_timebase.num, pMuxAudio->pOutCodecDecodeCtx->pkt_timebase.den);
        }
        av_new_packet(&pMuxAudio->OutPacket, 512 * 1024);
        pMuxAudio->OutPacket.size = 0;

        if (codecId != AV_CODEC_ID_NONE) {
            //PCM encoder
            if (NULL == (pMuxAudio->pOutCodecEncode = avcodec_find_encoder(codecId))) {
                AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to find encoder"), codecId));
                return MFX_ERR_NULL_PTR;
            }
            pInputAudio->pEncodeCodec = AVQSV_CODEC_COPY;
        } else {
            if (avcodecIsAuto(pInputAudio->pEncodeCodec)) {
                //エンコーダを探す (自動)
                if (NULL == (pMuxAudio->pOutCodecEncode = avcodec_find_encoder(m_Mux.format.pOutputFmt->audio_codec))) {
                    AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to find encoder"), m_Mux.format.pOutputFmt->audio_codec));
                    AddMessage(QSV_LOG_ERROR, _T("Please use --check-encoders to find available encoder.\n"));
                    return MFX_ERR_NULL_PTR;
                }
                AddMessage(QSV_LOG_DEBUG, _T("found encoder for codec %s for audio track %d\n"), char_to_tstring(pMuxAudio->pOutCodecEncode->name).c_str(), pInputAudio->src.nTrackId);
            } else {
                //エンコーダを探す (指定のもの)
                if (NULL == (pMuxAudio->pOutCodecEncode = avcodec_find_encoder_by_name(tchar_to_string(pInputAudio->pEncodeCodec).c_str()))) {
                    AddMessage(QSV_LOG_ERROR, _T("failed to find encoder for codec %s\n"), pInputAudio->pEncodeCodec);
                    AddMessage(QSV_LOG_ERROR, _T("Please use --check-encoders to find available encoder.\n"));
                    return MFX_ERR_NULL_PTR;
                }
                AddMessage(QSV_LOG_DEBUG, _T("found encoder for codec %s selected for audio track %d\n"), char_to_tstring(pMuxAudio->pOutCodecEncode->name).c_str(), pInputAudio->src.nTrackId);
            }
            pInputAudio->pEncodeCodec = _T("codec_something");
        }
        if (NULL == (pMuxAudio->pOutCodecEncodeCtx = avcodec_alloc_context3(pMuxAudio->pOutCodecEncode))) {
            AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to get encode codec context"), codecId));
            return MFX_ERR_NULL_PTR;
        }

        //チャンネル選択の自動設定を反映
        for (int i = 0; i < _countof(pMuxAudio->pnStreamChannelSelect); i++) {
            if (pMuxAudio->pnStreamChannelSelect[i] == QSV_CHANNEL_AUTO) {
                pMuxAudio->pnStreamChannelSelect[i] = (pMuxAudio->pOutCodecDecodeCtx->channel_layout)
                    ? pMuxAudio->pOutCodecDecodeCtx->channel_layout
                    : av_get_default_channel_layout(pMuxAudio->pOutCodecDecodeCtx->channels);
            }
        }

        auto enc_channel_layout = AutoSelectChannelLayout(pMuxAudio->pOutCodecEncode->channel_layouts, pMuxAudio->pOutCodecDecodeCtx);
        //もしチャンネルの分離・変更があれば、それを反映してエンコーダの入力とする
        if (bSplitChannelsEnabled(pMuxAudio->pnStreamChannelOut)) {
            enc_channel_layout = pMuxAudio->pnStreamChannelOut[pMuxAudio->nInSubStream];
            if (enc_channel_layout == QSV_CHANNEL_AUTO) {
                auto channels = av_get_channel_layout_nb_channels(pMuxAudio->pnStreamChannelSelect[pMuxAudio->nInSubStream]);
                enc_channel_layout = av_get_default_channel_layout(channels);
            }
        }
        int enc_sample_rate = (pInputAudio->nSamplingRate) ? pInputAudio->nSamplingRate : pMuxAudio->pOutCodecDecodeCtx->sample_rate;
        //select samplefmt
        pMuxAudio->pOutCodecEncodeCtx->sample_fmt          = AutoSelectSampleFmt(pMuxAudio->pOutCodecEncode->sample_fmts, pMuxAudio->pOutCodecDecodeCtx);
        pMuxAudio->pOutCodecEncodeCtx->sample_rate         = AutoSelectSamplingRate(pMuxAudio->pOutCodecEncode->supported_samplerates, enc_sample_rate);
        pMuxAudio->pOutCodecEncodeCtx->channel_layout      = enc_channel_layout;
        pMuxAudio->pOutCodecEncodeCtx->channels            = av_get_channel_layout_nb_channels(enc_channel_layout);
        pMuxAudio->pOutCodecEncodeCtx->bits_per_raw_sample = pMuxAudio->pOutCodecDecodeCtx->bits_per_raw_sample;
        pMuxAudio->pOutCodecEncodeCtx->pkt_timebase        = av_make_q(1, pMuxAudio->pOutCodecDecodeCtx->sample_rate);
        if (!avcodecIsCopy(pInputAudio->pEncodeCodec)) {
            pMuxAudio->pOutCodecEncodeCtx->bit_rate        = ((pInputAudio->nBitrate) ? pInputAudio->nBitrate : AVQSV_DEFAULT_AUDIO_BITRATE) * 1000;
        }
        AddMessage(QSV_LOG_DEBUG, _T("Audio Encoder Param: %s, %dch[0x%02x], %.1fkHz, %s, %d/%d\n"), char_to_tstring(pMuxAudio->pOutCodecEncode->name).c_str(),
            pMuxAudio->pOutCodecEncodeCtx->channels, (uint32_t)pMuxAudio->pOutCodecEncodeCtx->channel_layout, pMuxAudio->pOutCodecEncodeCtx->sample_rate / 1000.0,
            char_to_tstring(av_get_sample_fmt_name(pMuxAudio->pOutCodecEncodeCtx->sample_fmt)).c_str(),
            pMuxAudio->pOutCodecEncodeCtx->pkt_timebase.num, pMuxAudio->pOutCodecEncodeCtx->pkt_timebase.den);
        if (pMuxAudio->pOutCodecEncode->capabilities & CODEC_CAP_EXPERIMENTAL) { 
            //誰がなんと言おうと使うと言ったら使うのだ
            av_opt_set(pMuxAudio->pOutCodecEncodeCtx, "strict", "experimental", 0);
        }
        if (0 > avcodec_open2(pMuxAudio->pOutCodecEncodeCtx, pMuxAudio->pOutCodecEncode, NULL)) {
            AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to open encoder"), codecId));
            return MFX_ERR_NULL_PTR;
        }
        pMuxAudio->nResamplerInChannels      = pMuxAudio->pOutCodecEncodeCtx->channels;
        pMuxAudio->nResamplerInChannelLayout = pMuxAudio->pOutCodecEncodeCtx->channel_layout;
        pMuxAudio->nResamplerInSampleRate    = pMuxAudio->pOutCodecEncodeCtx->sample_rate;
        pMuxAudio->ResamplerInSampleFmt      = pMuxAudio->pOutCodecEncodeCtx->sample_fmt;
        //Resamplerに入力される音声のフォーマット
        auto nResamplerInChannels      = pMuxAudio->pOutCodecDecodeCtx->channels;
        auto nResamplerInChannelLayout = pMuxAudio->pOutCodecDecodeCtx->channel_layout;
        auto nResamplerInSampleRate    = pMuxAudio->pOutCodecDecodeCtx->sample_rate;
        auto ResamplerInSampleFmt      = pMuxAudio->pOutCodecDecodeCtx->sample_fmt;
        if (pMuxAudio->pFilter) {
            //フィルタの作成は親ストリームのみ
            if (pMuxAudio->nInSubStream == 0) {
                auto sts = InitAudioFilter(pMuxAudio,
                    pMuxAudio->pOutCodecDecodeCtx->channels,
                    pMuxAudio->pOutCodecDecodeCtx->channel_layout,
                    pMuxAudio->pOutCodecDecodeCtx->sample_rate,
                    pMuxAudio->pOutCodecDecodeCtx->sample_fmt);
                if (sts != MFX_ERR_NONE) return sts;
            }
            //フィルターがあれば、resamplerへの入力は変わるかもしれない
            nResamplerInChannels      = pMuxAudio->pFilterBufferSinkCtx->inputs[0]->channels;
            nResamplerInChannelLayout = pMuxAudio->pFilterBufferSinkCtx->inputs[0]->channel_layout;
            nResamplerInSampleRate    = pMuxAudio->pFilterBufferSinkCtx->inputs[0]->sample_rate;
            ResamplerInSampleFmt      = (AVSampleFormat)pMuxAudio->pFilterBufferSinkCtx->inputs[0]->format;
        }
        if ((!codecIDIsPCM(codecId) //PCM系のコーデックに出力するなら、sample_fmtのresampleは不要
            && pMuxAudio->pOutCodecEncodeCtx->sample_fmt  != ResamplerInSampleFmt)
            || pMuxAudio->pOutCodecEncodeCtx->sample_rate != nResamplerInSampleRate
            || pMuxAudio->pOutCodecEncodeCtx->channels    != nResamplerInChannels
            || bSplitChannelsEnabled(pMuxAudio->pnStreamChannelSelect)
            || bSplitChannelsEnabled(pMuxAudio->pnStreamChannelOut)) {
            auto sts = InitAudioResampler(pMuxAudio, nResamplerInChannels, nResamplerInChannelLayout, nResamplerInSampleRate, ResamplerInSampleFmt);
            if (sts != MFX_ERR_NONE) return sts;
        }
    } else if (pMuxAudio->pCodecCtxIn->codec_id == AV_CODEC_ID_AAC && pMuxAudio->pCodecCtxIn->extradata == NULL && m_Mux.video.pStream) {
        AddMessage(QSV_LOG_DEBUG, _T("start initialize aac_adtstoasc filter...\n"));
        auto filter = av_bsf_get_by_name("aac_adtstoasc");
        if (filter == nullptr) {
            AddMessage(QSV_LOG_ERROR, _T("failed to find aac_adtstoasc.\n"));
            return MFX_ERR_NULL_PTR;
        }
        int ret = 0;
        if (0 > (ret = av_bsf_alloc(filter, &pMuxAudio->pAACBsfc))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to allocate memory for aac_adtstoasc: %s.\n"), qsv_av_err2str(ret).c_str());
            return MFX_ERR_NULL_PTR;
        }
        if (0 > (ret = avcodec_parameters_from_context(pMuxAudio->pAACBsfc->par_in, pMuxAudio->pCodecCtxIn))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to set parameter for aac_adtstoasc: %s.\n"), qsv_av_err2str(ret).c_str());
            return MFX_ERR_NULL_PTR;
        }
        pMuxAudio->pAACBsfc->time_base_in = pMuxAudio->pCodecCtxIn->time_base;
        if (0 > (ret = av_bsf_init(pMuxAudio->pAACBsfc))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to init aac_adtstoasc: %s.\n"), qsv_av_err2str(ret).c_str());
            return MFX_ERR_NULL_PTR;
        }
        if (pInputAudio->src.pktSample.data) {
            //mkvではavformat_write_headerまでにAVCodecContextにextradataをセットしておく必要がある
            for (AVPacket *inpkt = av_packet_clone(&pInputAudio->src.pktSample); 0 == av_bsf_send_packet(pMuxAudio->pAACBsfc, inpkt); inpkt = nullptr) {
                AVPacket outpkt = { 0 };
                av_init_packet(&outpkt);
                ret = av_bsf_receive_packet(pMuxAudio->pAACBsfc, &outpkt);
                if (ret == 0) {
                    if (pMuxAudio->pAACBsfc->par_out->extradata) {
                        SetExtraData(pMuxAudio->pCodecCtxIn, pMuxAudio->pAACBsfc->par_out->extradata, pMuxAudio->pAACBsfc->par_out->extradata_size);
                    }
                    break;
                }
                if (ret != AVERROR(EAGAIN) && !(inpkt && ret == AVERROR_EOF)) {
                    AddMessage(QSV_LOG_ERROR, _T("failed to run aac_adtstoasc.\n"));
                    return MFX_ERR_NULL_PTR;
                }
                av_packet_unref(&outpkt);
            }
            AddMessage(QSV_LOG_DEBUG, _T("successfully attached packet sample from AAC\n."));
        }
    }

    //パラメータのコピー
    //下記のようにavcodec_copy_contextを使用するとavformat_write_header()が
    //Tag mp4a/0x6134706d incompatible with output codec id '86018' ([64][0][0][0])のようなエラーを出すことがある
    //そのため、必要な値だけをひとつづつコピーする
    //avcodec_copy_context(pMuxAudio->pStream->codec, srcCodecCtx);
    const AVCodecContext *srcCodecCtx = (pMuxAudio->pOutCodecEncodeCtx) ? pMuxAudio->pOutCodecEncodeCtx : pInputAudio->src.pCodecCtx;
#if USE_AVCODECPAR
    pMuxAudio->pStream->codecpar->codec_type      = srcCodecCtx->codec_type;
    pMuxAudio->pStream->codecpar->codec_id        = srcCodecCtx->codec_id;
    pMuxAudio->pStream->codecpar->frame_size      = srcCodecCtx->frame_size;
    pMuxAudio->pStream->codecpar->channels        = srcCodecCtx->channels;
    pMuxAudio->pStream->codecpar->channel_layout  = srcCodecCtx->channel_layout;
    pMuxAudio->pStream->codecpar->sample_rate     = srcCodecCtx->sample_rate;
    pMuxAudio->pStream->codecpar->format          = srcCodecCtx->sample_fmt;
    pMuxAudio->pStream->codecpar->block_align     = srcCodecCtx->block_align;
#else
    pMuxAudio->pStream->codec->codec_type      = srcCodecCtx->codec_type;
    pMuxAudio->pStream->codec->codec_id        = srcCodecCtx->codec_id;
    pMuxAudio->pStream->codec->frame_size      = srcCodecCtx->frame_size;
    pMuxAudio->pStream->codec->channels        = srcCodecCtx->channels;
    pMuxAudio->pStream->codec->channel_layout  = srcCodecCtx->channel_layout;
    pMuxAudio->pStream->codec->ticks_per_frame = srcCodecCtx->ticks_per_frame;
    pMuxAudio->pStream->codec->sample_rate     = srcCodecCtx->sample_rate;
    pMuxAudio->pStream->codec->sample_fmt      = srcCodecCtx->sample_fmt;
    pMuxAudio->pStream->codec->block_align     = srcCodecCtx->block_align;
#endif //#if USE_AVCODECPAR
    if (srcCodecCtx->extradata_size) {
        AddMessage(QSV_LOG_DEBUG, _T("set extradata from stream codec...\n"));
#if USE_AVCODECPAR
        SetExtraData(pMuxAudio->pStream->codecpar, srcCodecCtx->extradata, srcCodecCtx->extradata_size);
#else
        SetExtraData(pMuxAudio->pStream->codec,    srcCodecCtx->extradata, srcCodecCtx->extradata_size);
#endif //#if USE_AVCODECPAR
    } else if (pMuxAudio->pCodecCtxIn->extradata_size) {
        //aac_adtstoascから得たヘッダをコピーする
        //これをしておかないと、avformat_write_headerで"Error parsing AAC extradata, unable to determine samplerate."という
        //意味不明なエラーメッセージが表示される
        AddMessage(QSV_LOG_DEBUG, _T("set extradata from original packet...\n"));
#if USE_AVCODECPAR
        SetExtraData(pMuxAudio->pStream->codecpar, pMuxAudio->pCodecCtxIn->extradata, pMuxAudio->pCodecCtxIn->extradata_size);
#else
        SetExtraData(pMuxAudio->pStream->codec,    pMuxAudio->pCodecCtxIn->extradata, pMuxAudio->pCodecCtxIn->extradata_size);
#endif //#if USE_AVCODECPAR
    }
#if USE_AVCODECPAR
    pMuxAudio->pStream->time_base = av_make_q(1, pMuxAudio->pStream->codecpar->sample_rate);
#else
    pMuxAudio->pStream->time_base = av_make_q(1, pMuxAudio->pStream->codec->sample_rate);
    pMuxAudio->pStream->codec->time_base = pMuxAudio->pStream->time_base;
#endif
    if (m_Mux.video.pStream) {
        pMuxAudio->pStream->start_time = (int)av_rescale_q(pInputAudio->src.nDelayOfStream, pMuxAudio->pCodecCtxIn->pkt_timebase, pMuxAudio->pStream->time_base);
        pMuxAudio->nDelaySamplesOfAudio = (int)pMuxAudio->pStream->start_time;
        pMuxAudio->nLastPtsOut = pMuxAudio->pStream->start_time;

        AddMessage(QSV_LOG_DEBUG, _T("delay      %6d (timabase %d/%d)\n"), pInputAudio->src.nDelayOfStream, pMuxAudio->pCodecCtxIn->pkt_timebase.num, pMuxAudio->pCodecCtxIn->pkt_timebase.den);
        AddMessage(QSV_LOG_DEBUG, _T("start_time %6d (timabase %d/%d)\n"), pMuxAudio->pStream->start_time,  pMuxAudio->pStream->codec->time_base.num, pMuxAudio->pStream->codec->time_base.den);
    }

    if (pInputAudio->src.pStream->metadata) {
        for (AVDictionaryEntry *pEntry = nullptr;
        nullptr != (pEntry = av_dict_get(pInputAudio->src.pStream->metadata, "", pEntry, AV_DICT_IGNORE_SUFFIX));) {
            av_dict_set(&pMuxAudio->pStream->metadata, pEntry->key, pEntry->value, AV_DICT_IGNORE_SUFFIX);
            AddMessage(QSV_LOG_DEBUG, _T("Copy Audio Metadata: key %s, value %s\n"), char_to_tstring(pEntry->key).c_str(), char_to_tstring(pEntry->value).c_str());
        }
        auto language_data = av_dict_get(pInputAudio->src.pStream->metadata, "language", NULL, AV_DICT_MATCH_CASE);
        if (language_data) {
            av_dict_set(&pMuxAudio->pStream->metadata, language_data->key, language_data->value, AV_DICT_IGNORE_SUFFIX);
            AddMessage(QSV_LOG_DEBUG, _T("Set Audio language: key %s, value %s\n"), char_to_tstring(language_data->key).c_str(), char_to_tstring(language_data->value).c_str());
        }
    }
    return MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::InitSubtitle(AVMuxSub *pMuxSub, AVOutputStreamPrm *pInputSubtitle) {
    AddMessage(QSV_LOG_DEBUG, _T("start initializing subtitle ouput...\n"));
    AddMessage(QSV_LOG_DEBUG, _T("output stream index %d, pkt_timebase %d/%d, trackId %d\n"),
        pInputSubtitle->src.nIndex, pInputSubtitle->src.pCodecCtx->pkt_timebase.num, pInputSubtitle->src.pCodecCtx->pkt_timebase.den, pInputSubtitle->src.nTrackId);

    if (NULL == (pMuxSub->pStream = avformat_new_stream(m_Mux.format.pFormatCtx, NULL))) {
        AddMessage(QSV_LOG_ERROR, _T("failed to create new stream for subtitle.\n"));
        return MFX_ERR_NULL_PTR;
    }

    AVCodecID codecId = pInputSubtitle->src.pCodecCtx->codec_id;
    if (   0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "mp4")
        || 0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "mov")
        || 0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "3gp")
        || 0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "3g2")
        || 0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "psp")
        || 0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "ipod")
        || 0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "f4v")) {
        if (avcodec_descriptor_get(codecId)->props & AV_CODEC_PROP_TEXT_SUB) {
            codecId = AV_CODEC_ID_MOV_TEXT;
        }
    } else if (codecId == AV_CODEC_ID_MOV_TEXT) {
        codecId = AV_CODEC_ID_ASS;
    }

    auto copy_subtitle_header = [](AVCodecContext *pDstCtx, const AVCodecContext *pSrcCtx) {
        if (pSrcCtx->subtitle_header_size) {
            pDstCtx->subtitle_header_size = pSrcCtx->subtitle_header_size;
            pDstCtx->subtitle_header = (uint8_t *)av_mallocz(pDstCtx->subtitle_header_size + AV_INPUT_BUFFER_PADDING_SIZE);
            memcpy(pDstCtx->subtitle_header, pSrcCtx->subtitle_header, pSrcCtx->subtitle_header_size);
        }
    };

    if (codecId != pInputSubtitle->src.pCodecCtx->codec_id || codecId == AV_CODEC_ID_MOV_TEXT) {
        //setup decoder
        if (NULL == (pMuxSub->pOutCodecDecode = avcodec_find_decoder(pInputSubtitle->src.pCodecCtx->codec_id))) {
            AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to find decoder"), pInputSubtitle->src.pCodecCtx->codec_id));
            AddMessage(QSV_LOG_ERROR, _T("Please use --check-decoders to check available decoder.\n"));
            return MFX_ERR_NULL_PTR;
        }
        if (NULL == (pMuxSub->pOutCodecDecodeCtx = avcodec_alloc_context3(pMuxSub->pOutCodecDecode))) {
            AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to get decode codec context"), pInputSubtitle->src.pCodecCtx->codec_id));
            return MFX_ERR_NULL_PTR;
        }
        //設定されていない必須情報があれば設定する
#define COPY_IF_ZERO(dst, src) { if ((dst)==0) (dst)=(src); }
        COPY_IF_ZERO(pMuxSub->pOutCodecDecodeCtx->width,  pInputSubtitle->src.pCodecCtx->width);
        COPY_IF_ZERO(pMuxSub->pOutCodecDecodeCtx->height, pInputSubtitle->src.pCodecCtx->height);
#undef COPY_IF_ZERO
        pMuxSub->pOutCodecDecodeCtx->pkt_timebase = pInputSubtitle->src.pCodecCtx->pkt_timebase;
        SetExtraData(pMuxSub->pOutCodecDecodeCtx, pInputSubtitle->src.pCodecCtx->extradata, pInputSubtitle->src.pCodecCtx->extradata_size);
        int ret;
        if (0 > (ret = avcodec_open2(pMuxSub->pOutCodecDecodeCtx, pMuxSub->pOutCodecDecode, NULL))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to open decoder for %s: %s\n"),
                char_to_tstring(avcodec_get_name(pInputSubtitle->src.pCodecCtx->codec_id)).c_str(), qsv_av_err2str(ret).c_str());
            return MFX_ERR_NULL_PTR;
        }
        AddMessage(QSV_LOG_DEBUG, _T("Subtitle Decoder opened\n"));
        AddMessage(QSV_LOG_DEBUG, _T("Subtitle Decode Info: %s, %dx%d\n"), char_to_tstring(avcodec_get_name(pInputSubtitle->src.pCodecCtx->codec_id)).c_str(),
            pMuxSub->pOutCodecDecodeCtx->width, pMuxSub->pOutCodecDecodeCtx->height);

        //エンコーダを探す
        if (NULL == (pMuxSub->pOutCodecEncode = avcodec_find_encoder(codecId))) {
            AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to find encoder"), codecId));
            AddMessage(QSV_LOG_ERROR, _T("Please use --check-encoders to find available encoder.\n"));
            return MFX_ERR_NULL_PTR;
        }
        AddMessage(QSV_LOG_DEBUG, _T("found encoder for codec %s for subtitle track %d\n"), char_to_tstring(pMuxSub->pOutCodecEncode->name).c_str(), pInputSubtitle->src.nTrackId);

        if (NULL == (pMuxSub->pOutCodecEncodeCtx = avcodec_alloc_context3(pMuxSub->pOutCodecEncode))) {
            AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to get encode codec context"), codecId));
            return MFX_ERR_NULL_PTR;
        }
        pMuxSub->pOutCodecEncodeCtx->time_base = av_make_q(1, 1000);
        copy_subtitle_header(pMuxSub->pOutCodecEncodeCtx, pInputSubtitle->src.pCodecCtx);

        AddMessage(QSV_LOG_DEBUG, _T("Subtitle Encoder Param: %s, %dx%d\n"), char_to_tstring(pMuxSub->pOutCodecEncode->name).c_str(),
            pMuxSub->pOutCodecEncodeCtx->width, pMuxSub->pOutCodecEncodeCtx->height);
        if (pMuxSub->pOutCodecEncode->capabilities & CODEC_CAP_EXPERIMENTAL) {
            //問答無用で使うのだ
            av_opt_set(pMuxSub->pOutCodecEncodeCtx, "strict", "experimental", 0);
        }
        if (0 > (ret = avcodec_open2(pMuxSub->pOutCodecEncodeCtx, pMuxSub->pOutCodecEncode, NULL))) {
            AddMessage(QSV_LOG_ERROR, errorMesForCodec(_T("failed to open encoder"), codecId));
            AddMessage(QSV_LOG_ERROR, _T("%s\n"), qsv_av_err2str(ret).c_str());
            return MFX_ERR_NULL_PTR;
        }
        AddMessage(QSV_LOG_DEBUG, _T("Opened Subtitle Encoder Param: %s\n"), char_to_tstring(pMuxSub->pOutCodecEncode->name).c_str());
        if (nullptr == (pMuxSub->pBuf = (uint8_t *)av_malloc(SUB_ENC_BUF_MAX_SIZE))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to allocate buffer memory for subtitle encoding.\n"));
            return MFX_ERR_NULL_PTR;
        }
        pMuxSub->pStream->codec->codec = pMuxSub->pOutCodecEncodeCtx->codec;
    }

    pMuxSub->nInTrackId     = pInputSubtitle->src.nTrackId;
    pMuxSub->nStreamIndexIn = pInputSubtitle->src.nIndex;
    pMuxSub->pCodecCtxIn    = pInputSubtitle->src.pCodecCtx;

    const AVCodecContext *srcCodecCtx = (pMuxSub->pOutCodecEncodeCtx) ? pMuxSub->pOutCodecEncodeCtx : pMuxSub->pCodecCtxIn;
    avcodec_get_context_defaults3(pMuxSub->pStream->codec, NULL);
    copy_subtitle_header(pMuxSub->pStream->codec, srcCodecCtx);
#if USE_AVCODECPAR
    SetExtraData(pMuxSub->pStream->codecpar, srcCodecCtx->extradata, srcCodecCtx->extradata_size);
    pMuxSub->pStream->codecpar->codec_type      = srcCodecCtx->codec_type;
    pMuxSub->pStream->codecpar->codec_id        = srcCodecCtx->codec_id;
#else
    SetExtraData(pMuxSub->pStream->codec,    srcCodecCtx->extradata, srcCodecCtx->extradata_size);
    pMuxSub->pStream->codec->codec_type      = srcCodecCtx->codec_type;
    pMuxSub->pStream->codec->codec_id        = srcCodecCtx->codec_id;
#endif //#if USE_AVCODECPAR
    if (!pMuxSub->pStream->codec->codec_tag) {
        uint32_t codec_tag = 0;
        if (!m_Mux.format.pFormatCtx->oformat->codec_tag
            || av_codec_get_id(m_Mux.format.pFormatCtx->oformat->codec_tag, srcCodecCtx->codec_tag) == srcCodecCtx->codec_id
            || !av_codec_get_tag2(m_Mux.format.pFormatCtx->oformat->codec_tag, srcCodecCtx->codec_id, &codec_tag)) {
#if USE_AVCODECPAR
            pMuxSub->pStream->codecpar->codec_tag = srcCodecCtx->codec_tag;
#else
            pMuxSub->pStream->codec->codec_tag    = srcCodecCtx->codec_tag;
#endif
        }
    }
    pMuxSub->pStream->time_base              = srcCodecCtx->time_base;
    pMuxSub->pStream->start_time             = 0;
#if USE_AVCODECPAR
    pMuxSub->pStream->codecpar->width           = srcCodecCtx->width;
    pMuxSub->pStream->codecpar->height          = srcCodecCtx->height;
#else
    pMuxSub->pStream->codec->width           = srcCodecCtx->width;
    pMuxSub->pStream->codec->height          = srcCodecCtx->height;
    pMuxSub->pStream->codec->time_base       = pMuxSub->pStream->time_base;
    pMuxSub->pStream->codec->framerate       = srcCodecCtx->framerate;
#endif //#if USE_AVCODECPAR

    if (pInputSubtitle->src.nTrackId == -1) {
        pMuxSub->pStream->disposition |= AV_DISPOSITION_DEFAULT;
    }
    if (pInputSubtitle->src.pStream->metadata) {
        for (AVDictionaryEntry *pEntry = nullptr;
        nullptr != (pEntry = av_dict_get(pInputSubtitle->src.pStream->metadata, "", pEntry, AV_DICT_IGNORE_SUFFIX));) {
            av_dict_set(&pMuxSub->pStream->metadata, pEntry->key, pEntry->value, AV_DICT_IGNORE_SUFFIX);
            AddMessage(QSV_LOG_DEBUG, _T("Copy Subtitle Metadata: key %s, value %s\n"), char_to_tstring(pEntry->key).c_str(), char_to_tstring(pEntry->value).c_str());
        }
        auto language_data = av_dict_get(pInputSubtitle->src.pStream->metadata, "language", NULL, AV_DICT_MATCH_CASE);
        if (language_data) {
            av_dict_set(&pMuxSub->pStream->metadata, language_data->key, language_data->value, AV_DICT_IGNORE_SUFFIX);
            AddMessage(QSV_LOG_DEBUG, _T("Set Subtitle language: key %s, value %s\n"), char_to_tstring(language_data->key).c_str(), char_to_tstring(language_data->value).c_str());
        }
    }
    return MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::SetChapters(const vector<const AVChapter *>& pChapterList, bool bChapterNoTrim) {
    vector<AVChapter *> outChapters;
    for (int i = 0; i < (int)pChapterList.size(); i++) {
        int64_t start = (bChapterNoTrim) ? pChapterList[i]->start : AdjustTimestampTrimmed(pChapterList[i]->start, pChapterList[i]->time_base, pChapterList[i]->time_base, true);
        int64_t end   = (bChapterNoTrim) ? pChapterList[i]->end   : AdjustTimestampTrimmed(pChapterList[i]->end,   pChapterList[i]->time_base, pChapterList[i]->time_base, true);
        if (start < end) {
            AVChapter *pChap = (AVChapter *)av_mallocz(sizeof(pChap[0]));
            pChap->start     = start;
            pChap->end       = end;
            pChap->id        = pChapterList[i]->id;
            pChap->time_base = pChapterList[i]->time_base;
            av_dict_copy(&pChap->metadata, pChapterList[i]->metadata, 0);
            outChapters.push_back(pChap);
        }
    }
    if (outChapters.size() > 0) {
        m_Mux.format.pFormatCtx->nb_chapters = (uint32_t)outChapters.size();
        m_Mux.format.pFormatCtx->chapters = (AVChapter **)av_realloc_f(m_Mux.format.pFormatCtx->chapters, outChapters.size(), sizeof(m_Mux.format.pFormatCtx->chapters[0]) * outChapters.size());
        for (int i = 0; i < (int)outChapters.size(); i++) {
            m_Mux.format.pFormatCtx->chapters[i] = outChapters[i];

            AddMessage(QSV_LOG_DEBUG, _T("chapter #%d: id %d, start %I64d, end %I64d\n, timebase %d/%d\n"),
                outChapters[i]->id, outChapters[i]->start, outChapters[i]->end, outChapters[i]->time_base.num, outChapters[i]->time_base.den);
        }
    }
    return MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::Init(const TCHAR *strFileName, const void *option, shared_ptr<CEncodeStatusInfo> pEncSatusInfo) {
    m_Mux.format.bStreamError = true;
    AvcodecWriterPrm *prm = (AvcodecWriterPrm *)option;

    if (!check_avcodec_dll()) {
        AddMessage(QSV_LOG_ERROR, error_mes_avcodec_dll_not_found());
        return MFX_ERR_NULL_PTR;
    }

    std::string filename;
    if (0 == tchar_to_string(strFileName, filename, CP_UTF8)) {
        AddMessage(QSV_LOG_ERROR, _T("failed to convert output filename to utf-8 characters.\n"));
        return MFX_ERR_NULL_PTR;
    }

    av_register_all();
    avcodec_register_all();
    avformatNetworkInit();
    av_log_set_level((m_pPrintMes->getLogLevel() == QSV_LOG_DEBUG) ?  AV_LOG_DEBUG : QSV_AV_LOG_LEVEL);
    av_qsv_log_set(m_pPrintMes);
    for (const auto& stream : prm->inputStreamList) {
        if (stream.pFilter) {
            avfilter_register_all();
            break;
        }
    }

    if (prm->pOutputFormat != nullptr) {
        AddMessage(QSV_LOG_DEBUG, _T("output format specified: %s\n"), prm->pOutputFormat);
    }
    AddMessage(QSV_LOG_DEBUG, _T("output filename: \"%s\"\n"), strFileName);
    m_Mux.format.pFilename = strFileName;
    if (NULL == (m_Mux.format.pOutputFmt = av_guess_format((prm->pOutputFormat) ? tchar_to_string(prm->pOutputFormat).c_str() : NULL, filename.c_str(), NULL))) {
        AddMessage(QSV_LOG_ERROR,
            _T("failed to assume format from output filename.\n")
            _T("please set proper extension for output file, or specify format using option %s.\n"), (prm->pVideoInfo) ? _T("--format") : _T("--audio-file <format>:<filename>"));
        if (prm->pOutputFormat != nullptr) {
            AddMessage(QSV_LOG_ERROR, _T("Please use --check-formats to check available formats.\n"));
        }
        return MFX_ERR_NULL_PTR;
    }
    int err = avformat_alloc_output_context2(&m_Mux.format.pFormatCtx, m_Mux.format.pOutputFmt, nullptr, filename.c_str());
    if (m_Mux.format.pFormatCtx == nullptr) {
        AddMessage(QSV_LOG_ERROR, _T("failed to allocate format context: %s.\n"), qsv_av_err2str(err).c_str());
        return MFX_ERR_NULL_PTR;
    }
    m_Mux.format.bIsMatroska = 0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "matroska");
    m_Mux.format.bIsPipe = (0 == strcmp(filename.c_str(), "-")) || filename.c_str() == strstr(filename.c_str(), R"(\\.\pipe\)");

#if USE_CUSTOM_IO
    if (m_Mux.format.bIsPipe || usingAVProtocols(filename, 1) || (m_Mux.format.pFormatCtx->oformat->flags & (AVFMT_NEEDNUMBER | AVFMT_NOFILE))) {
#endif //#if USE_CUSTOM_IO
        if (m_Mux.format.bIsPipe) {
            AddMessage(QSV_LOG_DEBUG, _T("output is pipe\n"));
#if defined(_WIN32) || defined(_WIN64)
            if (_setmode(_fileno(stdout), _O_BINARY) < 0) {
                AddMessage(QSV_LOG_ERROR, _T("failed to switch stdout to binary mode.\n"));
                return MFX_ERR_UNKNOWN;
            }
#endif //#if defined(_WIN32) || defined(_WIN64)
            if (0 == strcmp(filename.c_str(), "-")) {
                m_bOutputIsStdout = true;
                filename = "pipe:1";
                AddMessage(QSV_LOG_DEBUG, _T("output is set to stdout\n"));
            } else if (m_pPrintMes->getLogLevel() == QSV_LOG_DEBUG) {
                AddMessage(QSV_LOG_DEBUG, _T("file name is %sunc path.\n"), (PathIsUNC(strFileName)) ? _T("") : _T("not "));
                if (PathFileExists(strFileName)) {
                    AddMessage(QSV_LOG_DEBUG, _T("file already exists and will overwrite.\n"));
                }
            }
        }
        if (!(m_Mux.format.pFormatCtx->oformat->flags & AVFMT_NOFILE)) {
            if (0 > (err = avio_open2(&m_Mux.format.pFormatCtx->pb, filename.c_str(), AVIO_FLAG_WRITE, NULL, NULL))) {
                AddMessage(QSV_LOG_ERROR, _T("failed to avio_open2 file \"%s\": %s\n"), char_to_tstring(filename, CP_UTF8).c_str(), qsv_av_err2str(err).c_str());
                return MFX_ERR_NULL_PTR; // Couldn't open file
            }
        }
        AddMessage(QSV_LOG_DEBUG, _T("Opened file \"%s\".\n"), char_to_tstring(filename, CP_UTF8).c_str());
#if USE_CUSTOM_IO
    } else {
        m_Mux.format.nOutputBufferSize = clamp(prm->nBufSizeMB, 0, QSV_OUTPUT_BUF_MB_MAX) * 1024 * 1024;
        if (m_Mux.format.nOutputBufferSize == 0) {
            //出力バッファが0とされている場合、libavformat用の内部バッファも量を減らす
            m_Mux.format.nAVOutBufferSize = 128 * 1024;
            if (prm->pVideoInfo) {
                m_Mux.format.nAVOutBufferSize *= 4;
            }
        } else {
            m_Mux.format.nAVOutBufferSize = 1024 * 1024;
            if (prm->pVideoInfo) {
                m_Mux.format.nAVOutBufferSize *= 8;
            } else {
                //動画を出力しない(音声のみの場合)場合、バッファを減らす
                m_Mux.format.nOutputBufferSize /= 4;
            }
        }

        if (NULL == (m_Mux.format.pAVOutBuffer = (mfxU8 *)av_malloc(m_Mux.format.nAVOutBufferSize))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to allocate muxer buffer of %d MB.\n"), m_Mux.format.nAVOutBufferSize / (1024 * 1024));
            return MFX_ERR_NULL_PTR;
        }
        AddMessage(QSV_LOG_DEBUG, _T("allocated internal buffer %d MB.\n"), m_Mux.format.nAVOutBufferSize / (1024 * 1024));
        CreateDirectoryRecursive(PathRemoveFileSpecFixed(strFileName).second.c_str());
        errno_t error;
        if (0 != (error = _tfopen_s(&m_Mux.format.fpOutput, strFileName, _T("wb"))) || m_Mux.format.fpOutput == NULL) {
            AddMessage(QSV_LOG_ERROR, _T("failed to open %soutput file \"%s\": %s.\n"), (prm->pVideoInfo) ? _T("") : _T("audio "), strFileName, _tcserror(error));
            return MFX_ERR_NULL_PTR; // Couldn't open file
        }
        if (0 < (m_Mux.format.nOutputBufferSize = (uint32_t)malloc_degeneracy((void **)&m_Mux.format.pOutputBuffer, m_Mux.format.nOutputBufferSize, 1024 * 1024))) {
            setvbuf(m_Mux.format.fpOutput, m_Mux.format.pOutputBuffer, _IOFBF, m_Mux.format.nOutputBufferSize);
            AddMessage(QSV_LOG_DEBUG, _T("set external output buffer %d MB.\n"), m_Mux.format.nOutputBufferSize / (1024 * 1024));
        }
        if (NULL == (m_Mux.format.pFormatCtx->pb = avio_alloc_context(m_Mux.format.pAVOutBuffer, m_Mux.format.nAVOutBufferSize, 1, this, funcReadPacket, funcWritePacket, funcSeek))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to alloc avio context.\n"));
            return MFX_ERR_NULL_PTR;
        }
    }
#endif //#if USE_CUSTOM_IO

    m_Mux.trim = prm->trimList;

    if (prm->pVideoInfo) {
        mfxStatus sts = InitVideo(prm);
        if (sts != MFX_ERR_NONE) {
            return sts;
        }
        AddMessage(QSV_LOG_DEBUG, _T("Initialized video output.\n"));
    }

    const int audioStreamCount = (int)count_if(prm->inputStreamList.begin(), prm->inputStreamList.end(), [](AVOutputStreamPrm prm) { return prm.src.nTrackId > 0; });
    if (audioStreamCount) {
        m_Mux.audio.resize(audioStreamCount, { 0 });
        int iAudioIdx = 0;
        for (int iStream = 0; iStream < (int)prm->inputStreamList.size(); iStream++) {
            if (prm->inputStreamList[iStream].src.nTrackId > 0) {
                m_Mux.audio[iAudioIdx].nAudioResampler = prm->nAudioResampler;
                //サブストリームの場合は、デコーダ情報は親ストリームのものをコピーする
                if (prm->inputStreamList[iStream].src.nSubStreamId > 0) {
                    auto pAudioMuxStream = getAudioStreamData(prm->inputStreamList[iStream].src.nTrackId, 0);
                    if (pAudioMuxStream) {
                        //デコード情報をコピー
                        m_Mux.audio[iAudioIdx].pOutCodecDecode    = pAudioMuxStream->pOutCodecDecode;
                        m_Mux.audio[iAudioIdx].pOutCodecDecodeCtx = pAudioMuxStream->pOutCodecDecodeCtx;
                        //フィルタ情報をコピー
                        m_Mux.audio[iAudioIdx].pFilter              = pAudioMuxStream->pFilter;
                        m_Mux.audio[iAudioIdx].pFilterBufferSrcCtx  = pAudioMuxStream->pFilterBufferSrcCtx;
                        m_Mux.audio[iAudioIdx].pFilterBufferSinkCtx = pAudioMuxStream->pFilterBufferSinkCtx;
                    } else {
                        AddMessage(QSV_LOG_ERROR, _T("Substream #%d found for track %d, but root stream not found.\n"),
                            prm->inputStreamList[iStream].src.nSubStreamId, prm->inputStreamList[iStream].src.nTrackId);
                        return MFX_ERR_UNDEFINED_BEHAVIOR;
                    }
                }
                mfxStatus sts = InitAudio(&m_Mux.audio[iAudioIdx], &prm->inputStreamList[iStream], prm->nAudioIgnoreDecodeError);
                if (sts != MFX_ERR_NONE) {
                    return sts;
                }
                if (prm->inputStreamList[iStream].src.nSubStreamId > 0) {
                    //フィルタはデコード結果によって更新されるかもしれない
                    //そのため、初期化以外ではサブストリームは親ストリームの情報を参照してはならない
                    m_Mux.audio[iAudioIdx].pFilter = nullptr;
                    m_Mux.audio[iAudioIdx].pFilterBufferSrcCtx = nullptr;
                    m_Mux.audio[iAudioIdx].pFilterBufferSinkCtx = nullptr;
                }
                AddMessage(QSV_LOG_DEBUG, _T("Initialized audio output - #%d: track %d, substream %d.\n"),
                    iAudioIdx, prm->inputStreamList[iStream].src.nTrackId, prm->inputStreamList[iStream].src.nSubStreamId);
                iAudioIdx++;
            }
        }
    }
    const int subStreamCount = (int)count_if(prm->inputStreamList.begin(), prm->inputStreamList.end(), [](AVOutputStreamPrm prm) { return prm.src.nTrackId < 0; });
    if (subStreamCount) {
        m_Mux.sub.resize(subStreamCount, { 0 });
        int iSubIdx = 0;
        for (int iStream = 0; iStream < (int)prm->inputStreamList.size(); iStream++) {
            if (prm->inputStreamList[iStream].src.nTrackId < 0) {
                mfxStatus sts = InitSubtitle(&m_Mux.sub[iSubIdx], &prm->inputStreamList[iStream]);
                if (sts != MFX_ERR_NONE) {
                    return sts;
                }
                AddMessage(QSV_LOG_DEBUG, _T("Initialized subtitle output - %d.\n"), iSubIdx);
                iSubIdx++;
            }
        }
    }

    SetChapters(prm->chapterList, prm->bChapterNoTrim);
    
    sprintf_s(m_Mux.format.pFormatCtx->filename, filename.c_str());
    if (m_Mux.format.pOutputFmt->flags & AVFMT_GLOBALHEADER) {
        if (m_Mux.video.pStream) { m_Mux.video.pStream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER; }
        for (uint32_t i = 0; i < m_Mux.audio.size(); i++) {
            if (m_Mux.audio[i].pStream) { m_Mux.audio[i].pStream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER; }
        }
        for (uint32_t i = 0; i < m_Mux.sub.size(); i++) {
            if (m_Mux.sub[i].pStream) { m_Mux.sub[i].pStream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER; }
        }
    }

    if (m_Mux.format.pFormatCtx->metadata) {
        av_dict_copy(&m_Mux.format.pFormatCtx->metadata, prm->pInputFormatMetadata, AV_DICT_DONT_OVERWRITE);
        av_dict_set(&m_Mux.format.pFormatCtx->metadata, "duration", NULL, 0);
        av_dict_set(&m_Mux.format.pFormatCtx->metadata, "creation_time", NULL, 0);
    }

    for (const auto& muxOpt : prm->vMuxOpt) {
        std::string optName = tchar_to_string(muxOpt.first);
        std::string optValue = tchar_to_string(muxOpt.second);
        if (0 > (err = av_dict_set(&m_Mux.format.pHeaderOptions, optName.c_str(), optValue.c_str(), 0))) {
            AddMessage(QSV_LOG_ERROR, _T("failed to set mux opt: %s = %s.\n"), muxOpt.first.c_str(), muxOpt.second.c_str());
            return MFX_ERR_INVALID_VIDEO_PARAM;
        }
        AddMessage(QSV_LOG_DEBUG, _T("set mux opt: %s = %s.\n"), muxOpt.first.c_str(), muxOpt.second.c_str());
    }

    m_pEncSatusInfo = pEncSatusInfo;
    //音声のみの出力を行う場合、SetVideoParamは呼ばれないので、ここで最後まで初期化をすませてしまう
    if (!m_Mux.video.pStream) {
        return SetVideoParam(NULL, NULL);
    }
#if ENABLE_AVCODEC_OUT_THREAD
    m_Mux.thread.pQueueInfo = prm->pQueueInfo;
    //スレッドの使用数を設定
    if (prm->nOutputThread == QSV_OUTPUT_THREAD_AUTO) {
        prm->nOutputThread = 1;
    }
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    if (prm->nAudioThread == QSV_AUDIO_THREAD_AUTO) {
        prm->nAudioThread = 0;
    }
    m_Mux.thread.bEnableAudProcessThread = prm->nOutputThread > 0 && prm->nAudioThread > 0;
    m_Mux.thread.bEnableAudEncodeThread  = prm->nOutputThread > 0 && prm->nAudioThread > 1;
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    m_Mux.thread.bEnableOutputThread     = prm->nOutputThread > 0;
    if (m_Mux.thread.bEnableOutputThread) {
        AddMessage(QSV_LOG_DEBUG, _T("starting output thread...\n"));
        m_Mux.thread.bAbortOutput = false;
        m_Mux.thread.bThAudProcessAbort = false;
        m_Mux.thread.bThAudEncodeAbort = false;
        m_Mux.thread.qAudioPacketOut.init(8192, 256 * std::max(1, (int)m_Mux.audio.size())); //字幕のみコピーするときのため、最低でもある程度は確保する
        m_Mux.thread.qVideobitstream.init(4096, (std::max)(64, (m_Mux.video.nFPS.den) ? m_Mux.video.nFPS.num * 4 / m_Mux.video.nFPS.den : 0));
        m_Mux.thread.qVideobitstreamFreeI.init(256);
        m_Mux.thread.qVideobitstreamFreePB.init(3840);
        m_Mux.thread.heEventPktAddedOutput = CreateEvent(NULL, TRUE, FALSE, NULL);
        m_Mux.thread.heEventClosingOutput  = CreateEvent(NULL, TRUE, FALSE, NULL);
        m_Mux.thread.thOutput = std::thread(&CAvcodecWriter::WriteThreadFunc, this);
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        if (m_Mux.thread.bEnableAudProcessThread) {
            AddMessage(QSV_LOG_DEBUG, _T("starting audio process thread...\n"));
            m_Mux.thread.qAudioPacketProcess.init(8192, 512, 4);
            m_Mux.thread.heEventPktAddedAudProcess = CreateEvent(NULL, TRUE, FALSE, NULL);
            m_Mux.thread.heEventClosingAudProcess  = CreateEvent(NULL, TRUE, FALSE, NULL);
            m_Mux.thread.thAudProcess = std::thread(&CAvcodecWriter::ThreadFuncAudThread, this);
            if (m_Mux.thread.bEnableAudEncodeThread) {
                AddMessage(QSV_LOG_DEBUG, _T("starting audio encode thread...\n"));
                m_Mux.thread.qAudioFrameEncode.init(8192, 512, 4);
                m_Mux.thread.heEventPktAddedAudEncode = CreateEvent(NULL, TRUE, FALSE, NULL);
                m_Mux.thread.heEventClosingAudEncode  = CreateEvent(NULL, TRUE, FALSE, NULL);
                m_Mux.thread.thAudEncode = std::thread(&CAvcodecWriter::ThreadFuncAudEncodeThread, this);
            }
        }
#endif
    }
#endif
    return MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::SetSPSPPSToExtraData(const mfxVideoParam *pMfxVideoPrm) {
    //SPS/PPSをセット
    if (m_Mux.video.pStream) {
        mfxExtCodingOptionSPSPPS *pSpsPPS = NULL;
        for (int iExt = 0; iExt < pMfxVideoPrm->NumExtParam; iExt++) {
            if (pMfxVideoPrm->ExtParam[iExt]->BufferId == MFX_EXTBUFF_CODING_OPTION_SPSPPS) {
                pSpsPPS = (mfxExtCodingOptionSPSPPS *)(pMfxVideoPrm->ExtParam[iExt]);
                break;
            }
        }
        if (pSpsPPS) {
#if USE_AVCODECPAR
            m_Mux.video.pStream->codecpar->extradata_size = pSpsPPS->SPSBufSize + pSpsPPS->PPSBufSize;
            m_Mux.video.pStream->codecpar->extradata = (mfxU8 *)av_malloc(m_Mux.video.pStream->codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            memcpy(m_Mux.video.pStream->codecpar->extradata,                       pSpsPPS->SPSBuffer, pSpsPPS->SPSBufSize);
            memcpy(m_Mux.video.pStream->codecpar->extradata + pSpsPPS->SPSBufSize, pSpsPPS->PPSBuffer, pSpsPPS->PPSBufSize);
#else
            m_Mux.video.pCodecCtx->extradata_size = pSpsPPS->SPSBufSize + pSpsPPS->PPSBufSize;
            m_Mux.video.pCodecCtx->extradata = (mfxU8 *)av_malloc(m_Mux.video.pCodecCtx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            memcpy(m_Mux.video.pCodecCtx->extradata,                       pSpsPPS->SPSBuffer, pSpsPPS->SPSBufSize);
            memcpy(m_Mux.video.pCodecCtx->extradata + pSpsPPS->SPSBufSize, pSpsPPS->PPSBuffer, pSpsPPS->PPSBufSize);
#endif
            AddMessage(QSV_LOG_DEBUG, _T("copied video header from QSV encoder.\n"));
        } else {
            AddMessage(QSV_LOG_ERROR, _T("failed to get video header from QSV encoder.\n"));
            return MFX_ERR_UNKNOWN;
        }
        m_Mux.video.bIsPAFF = 0 != (pMfxVideoPrm->mfx.FrameInfo.PicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF));
        if (m_Mux.video.bIsPAFF) {
            AddMessage(QSV_LOG_DEBUG, _T("output is PAFF.\n"));
        }
    }
    return MFX_ERR_NONE;
}

//extradataにHEVCのヘッダーを追加する
mfxStatus CAvcodecWriter::AddHEVCHeaderToExtraData(const mfxBitstream *pMfxBitstream) {
    mfxU8 *ptr = pMfxBitstream->Data;
    mfxU8 *vps_start_ptr = nullptr;
    mfxU8 *vps_fin_ptr = nullptr;
    const int i_fin = pMfxBitstream->DataOffset + pMfxBitstream->DataLength - 3;
    for (int i = pMfxBitstream->DataOffset; i < i_fin; i++) {
        if (ptr[i+0] == 0 && ptr[i+1] == 0 && ptr[i+2] == 1) {
            mfxU8 nalu_type = (ptr[i+3] & 0x7f) >> 1;
            if (nalu_type == 32 && vps_start_ptr == nullptr) {
                vps_start_ptr = ptr + i - (i > 0 && ptr[i-1] == 0);
                i += 3;
            } else if (nalu_type != 32 && vps_start_ptr && vps_fin_ptr == nullptr) {
                vps_fin_ptr = ptr + i - (i > 0 && ptr[i-1] == 0);
                break;
            }
        }
    }
    if (vps_fin_ptr == nullptr) {
        vps_fin_ptr = ptr + pMfxBitstream->DataOffset + pMfxBitstream->DataLength;
    }
    if (vps_start_ptr) {
#if USE_AVCODECPAR
        const uint32_t vps_length = (uint32_t)(vps_fin_ptr - vps_start_ptr);
        uint8_t *new_ptr = (uint8_t *)av_malloc(m_Mux.video.pStream->codecpar->extradata_size + vps_length + AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(new_ptr, vps_start_ptr, vps_length);
        memcpy(new_ptr + vps_length, m_Mux.video.pStream->codecpar->extradata, m_Mux.video.pStream->codecpar->extradata_size);
        m_Mux.video.pStream->codecpar->extradata_size += vps_length;
        av_free(m_Mux.video.pStream->codecpar->extradata);
        m_Mux.video.pStream->codecpar->extradata = new_ptr;
#else
        const uint32_t vps_length = (uint32_t)(vps_fin_ptr - vps_start_ptr);
        uint8_t *new_ptr = (uint8_t *)av_malloc(m_Mux.video.pCodecCtx->extradata_size + vps_length + AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(new_ptr, vps_start_ptr, vps_length);
        memcpy(new_ptr + vps_length, m_Mux.video.pCodecCtx->extradata, m_Mux.video.pCodecCtx->extradata_size);
        m_Mux.video.pCodecCtx->extradata_size += vps_length;
        av_free(m_Mux.video.pCodecCtx->extradata);
        m_Mux.video.pCodecCtx->extradata = new_ptr;
#endif
    }
    return MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::WriteFileHeader(const mfxVideoParam *pMfxVideoPrm, const mfxExtCodingOption2 *cop2, const mfxBitstream *pMfxBitstream) {
    if ((m_Mux.video.pCodecCtx && m_Mux.video.pCodecCtx->codec_id == AV_CODEC_ID_HEVC) && pMfxBitstream) {
        mfxStatus sts = AddHEVCHeaderToExtraData(pMfxBitstream);
        if (sts != MFX_ERR_NONE) {
            return sts;
        }
    }

    //QSVEncCでエンコーダしたことを記録してみる
    //これは直接metadetaにセットする
    sprintf_s(m_Mux.format.metadataStr, "QSVEncC (%s) %s", tchar_to_string(BUILD_ARCH_STR).c_str(), VER_STR_FILEVERSION);
    av_dict_set(&m_Mux.format.pFormatCtx->metadata, "encoding_tool", m_Mux.format.metadataStr, 0); //mp4
    //encoderではなく、encoding_toolを使用する。mp4はcomment, titleなどは設定可能, mkvではencode_byも可能

    //mp4のmajor_brandをisonからmp42に変更
    //これはmetadataではなく、avformat_write_headerのoptionsに渡す
    //この差ははっきり言って謎
    if (m_Mux.video.pStream && 0 == strcmp(m_Mux.format.pFormatCtx->oformat->name, "mp4")) {
        av_dict_set(&m_Mux.format.pHeaderOptions, "brand", "mp42", 0);
        AddMessage(QSV_LOG_DEBUG, _T("set format brand \"mp42\".\n"));
    }

    //なんらかの問題があると、ここでよく死ぬ
    int ret = 0;
    if (0 > (ret = avformat_write_header(m_Mux.format.pFormatCtx, &m_Mux.format.pHeaderOptions))) {
        AddMessage(QSV_LOG_ERROR, _T("failed to write header for output file: %s\n"), qsv_av_err2str(ret).c_str());
        if (m_Mux.format.pHeaderOptions) av_dict_free(&m_Mux.format.pHeaderOptions);
        return MFX_ERR_UNKNOWN;
    }
    //不正なオプションを渡していないかチェック
    for (const AVDictionaryEntry *t = NULL; NULL != (t = av_dict_get(m_Mux.format.pHeaderOptions, "", t, AV_DICT_IGNORE_SUFFIX));) {
        AddMessage(QSV_LOG_ERROR, _T("Unknown option to muxer: ") + char_to_tstring(t->key) + _T("\n"));
        return MFX_ERR_UNKNOWN;
    }
    if (m_Mux.format.pHeaderOptions) {
        av_dict_free(&m_Mux.format.pHeaderOptions);
    }

    av_dump_format(m_Mux.format.pFormatCtx, 0, m_Mux.format.pFormatCtx->filename, 1);

    //frame_sizeを表示
    for (const auto& audio : m_Mux.audio) {
        if (audio.pOutCodecDecodeCtx || audio.pOutCodecEncodeCtx) {
            tstring audioFrameSize = strsprintf(_T("audio track #%d:"), audio.nInTrackId);
            if (audio.pOutCodecDecodeCtx) {
                audioFrameSize += strsprintf(_T(" %s frame_size %d sample/byte"), char_to_tstring(audio.pOutCodecDecode->name).c_str(), audio.pOutCodecDecodeCtx->frame_size);
            }
            if (audio.pOutCodecEncodeCtx) {
                audioFrameSize += strsprintf(_T(" -> %s frame_size %d sample/byte"), char_to_tstring(audio.pOutCodecEncode->name).c_str(), audio.pOutCodecEncodeCtx->frame_size);
            }
            AddMessage(QSV_LOG_DEBUG, audioFrameSize);
        }
    }

    //API v1.6以下でdtsがQSVが提供されない場合、自前で計算する必要がある
    //API v1.6ではB-pyramidが存在しないので、Bフレームがあるかないかだけ考慮するればよい
    if (m_Mux.video.pStream) {
        if (m_Mux.video.bDtsUnavailable) {
            m_Mux.video.nFpsBaseNextDts = (0 - (pMfxVideoPrm->mfx.GopRefDist > 0) - ((pMfxVideoPrm->mfx.GopRefDist > 0) & (cop2->BRefType == MFX_B_REF_PYRAMID))) * (1 + m_Mux.video.bIsPAFF);
            AddMessage(QSV_LOG_DEBUG, _T("calc dts, first dts %d x (timebase).\n"), m_Mux.video.nFpsBaseNextDts);
        }
    }
    return MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::SetVideoParam(const mfxVideoParam *pMfxVideoPrm, const mfxExtCodingOption2 *cop2) {
    mfxStatus sts = SetSPSPPSToExtraData(pMfxVideoPrm);
    if (sts != MFX_ERR_NONE) {
        return sts;
    }

    if (pMfxVideoPrm) m_Mux.video.mfxParam = *pMfxVideoPrm;
    if (cop2) m_Mux.video.mfxCop2 = *cop2;

    if (m_Mux.video.pCodecCtx == nullptr || m_Mux.video.pCodecCtx->codec_id != AV_CODEC_ID_HEVC) {
        if (MFX_ERR_NONE != (sts = WriteFileHeader(pMfxVideoPrm, cop2, nullptr))) {
            return sts;
        }
        m_Mux.format.bFileHeaderWritten = true;
    }

    tstring mes = GetWriterMes();
    AddMessage(QSV_LOG_DEBUG, mes);
    m_strOutputInfo += mes;
    m_Mux.format.bStreamError = false;

    m_bInited = true;

    return MFX_ERR_NONE;
}

int64_t CAvcodecWriter::AdjustTimestampTrimmed(int64_t nTimeIn, AVRational timescaleIn, AVRational timescaleOut, bool lastValidFrame) {
    AVRational timescaleFps = av_inv_q(m_Mux.video.nFPS);
    const int vidFrameIdx = (int)av_rescale_q(nTimeIn, timescaleIn, timescaleFps);
    int cutFrames = 0;
    if (m_Mux.trim.size()) {
        int nLastFinFrame = 0;
        for (const auto& trim : m_Mux.trim) {
            if (vidFrameIdx < trim.start) {
                if (lastValidFrame) {
                    cutFrames += (vidFrameIdx - nLastFinFrame);
                    nLastFinFrame = vidFrameIdx;
                    break;
                }
                return AV_NOPTS_VALUE;
            }
            cutFrames += trim.start - nLastFinFrame;
            if (vidFrameIdx <= trim.fin) {
                nLastFinFrame = vidFrameIdx;
                break;
            }
            nLastFinFrame = trim.fin;
        }
        cutFrames += vidFrameIdx - nLastFinFrame;
    }
    int64_t tsTimeOut = av_rescale_q(nTimeIn,   timescaleIn,  timescaleOut);
    int64_t tsTrim    = av_rescale_q(cutFrames, timescaleFps, timescaleOut);
    return tsTimeOut - tsTrim;
}

tstring CAvcodecWriter::GetWriterMes() {
    std::string mes = "avwriter: ";
    int i_stream = 0;
    auto add_mes = [&mes](std::string str) {
        if (mes.length() - mes.find_last_of("\n") + str.length() >= 65) {
            if (0 == str.find_first_of(',')) {
                str = str.substr(1);
                mes += ",\n";
            } else {
                mes += "\n";
            }
        }
        mes += str;
    };

    if (m_Mux.video.pStream) {
        add_mes(avcodec_get_name(m_Mux.video.pStream->codec->codec_id));
        i_stream++;
    }
    for (const auto& audioStream : m_Mux.audio) {
        if (audioStream.pStream) {
            std::string audiostr = (i_stream) ? ", " : "";
            if (audioStream.pOutCodecEncodeCtx) {
                //入力情報
                audiostr += strsprintf("#%d:%s/%s",
                    audioStream.nInTrackId,
                    audioStream.pOutCodecDecode->name,
                    getChannelLayoutChar(audioStream.pOutCodecDecodeCtx->channels, audioStream.pOutCodecDecodeCtx->channel_layout).c_str());
                if (audioStream.pnStreamChannelSelect[audioStream.nInSubStream] != 0) {
                    audiostr += strsprintf(":%s", getChannelLayoutChar(av_get_channel_layout_nb_channels(audioStream.pnStreamChannelSelect[audioStream.nInSubStream]), audioStream.pnStreamChannelSelect[audioStream.nInSubStream]).c_str());
                }
                //フィルタ情報
                if (audioStream.pFilter) {
                    audiostr += ":";
                    std::string filter_str;
                    auto filters = split(tchar_to_string(audioStream.pFilter, CP_UTF8), ",");
                    for (auto filter : filters) {
                        size_t pos = 0;
                        if ((pos = filter.find_first_of('=')) != std::string::npos) {
                            filter = filter.substr(0, pos);
                        }
                        if (filter_str.length()) filter_str += "+";
                        filter_str += filter;
                    }
                    audiostr += filter_str;
                }
                //エンコード情報
                audiostr += strsprintf(" -> %s/%s/%dkbps",
                    audioStream.pOutCodecEncode->name,
                    getChannelLayoutChar(audioStream.pOutCodecEncodeCtx->channels, audioStream.pOutCodecEncodeCtx->channel_layout).c_str(),
                    audioStream.pOutCodecEncodeCtx->bit_rate / 1000);
            } else {
                audiostr += strsprintf("%s", avcodec_get_name(audioStream.pCodecCtxIn->codec_id));
            }
            add_mes(audiostr);
            i_stream++;
        }
    }
    for (const auto& subtitleStream : m_Mux.sub) {
        if (subtitleStream.pStream) {
            add_mes(std::string((i_stream) ? ", " : "") + strsprintf("sub#%d", std::abs(subtitleStream.nInTrackId)));
            i_stream++;
        }
    }
    if (m_Mux.format.pFormatCtx->nb_chapters > 0) {
        add_mes(std::string((i_stream) ? ", " : "") + "chap");
        i_stream++;
    }
    std::string output = " => ";
    output += m_Mux.format.pFormatCtx->oformat->name;
    //if (m_Mux.format.nOutputBufferSize) {
    //    output += strsprintf(" (%dMB buf)", m_Mux.format.nOutputBufferSize / (1024 * 1024));
    //}
    add_mes(output);
    return char_to_tstring(mes.c_str());
}

mfxU32 CAvcodecWriter::getH264PAFFFieldLength(mfxU8 *ptr, mfxU32 size) {
    int sliceNalu = 0;
    mfxU8 a = ptr[0], b = ptr[1], c = ptr[2], d = 0;
    for (mfxU32 i = 3; i < size; i++) {
        d = ptr[i];
        if (((a | b) == 0) & (c == 1)) {
            if (sliceNalu) {
                return i-3-(ptr[i-4]==0)+1;
            }
            int nalType = d & 0x1F;
            sliceNalu += ((nalType == 1) | (nalType == 5));
        }
        a = b, b = c, c = d;
    }
    return size;
}

mfxStatus CAvcodecWriter::WriteNextFrame(mfxBitstream *pMfxBitstream) {
#if ENABLE_AVCODEC_OUT_THREAD
    //最初のヘッダーを書いたパケットは出力スレッドでなくエンコードスレッドが出力する
    //出力スレッドは、このパケットがヘッダーを書き終わり、m_Mux.format.bFileHeaderWrittenフラグが立った時点で動き出す
    if (m_Mux.thread.thOutput.joinable() && m_Mux.format.bFileHeaderWritten) {
        mfxBitstream copyStream = { 0 };
        bool bFrameI = (pMfxBitstream->FrameType & MFX_FRAMETYPE_I) != 0;
        bool bFrameP = (pMfxBitstream->FrameType & MFX_FRAMETYPE_P) != 0;
        //IフレームかPBフレームかでサイズが大きく違うため、空きのmfxBistreamは異なるキューで管理する
        auto& qVideoQueueFree = (bFrameI) ? m_Mux.thread.qVideobitstreamFreeI : m_Mux.thread.qVideobitstreamFreePB;
        //空いているmfxBistreamを取り出す
        if (!qVideoQueueFree.front_copy_and_pop_no_lock(&copyStream) || copyStream.MaxLength < pMfxBitstream->DataLength) {
            //空いているmfxBistreamがない、あるいはそのバッファサイズが小さい場合は、領域を取り直す
            if (MFX_ERR_NONE != mfxBitstreamInit(&copyStream, pMfxBitstream->DataLength * ((bFrameI | bFrameP) ? 2 : 8))) {
                AddMessage(QSV_LOG_ERROR, _T("Failed to allocate memory for video bitstream output buffer.\n"));
                m_Mux.format.bStreamError = true;
                return MFX_ERR_MEMORY_ALLOC;
            }
        }
        //必要な情報をコピー
        copyStream.DataFlag = pMfxBitstream->DataFlag;
        copyStream.TimeStamp = pMfxBitstream->TimeStamp;
        copyStream.DecodeTimeStamp = pMfxBitstream->DecodeTimeStamp;
        copyStream.FrameType = pMfxBitstream->FrameType;
        copyStream.DataLength = pMfxBitstream->DataLength;
        copyStream.DataOffset = 0;
        memcpy(copyStream.Data, pMfxBitstream->Data + pMfxBitstream->DataOffset, copyStream.DataLength);
        //キューに押し込む
        if (!m_Mux.thread.qVideobitstream.push(copyStream)) {
            AddMessage(QSV_LOG_ERROR, _T("Failed to allocate memory for video bitstream queue.\n"));
            m_Mux.format.bStreamError = true;
        }
        pMfxBitstream->DataLength = 0;
        pMfxBitstream->DataOffset = 0;
        SetEvent(m_Mux.thread.heEventPktAddedOutput);
        return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
    }
#endif
    int64_t dts = 0;
    return WriteNextFrameInternal(pMfxBitstream, &dts);
}

mfxStatus CAvcodecWriter::WriteNextFrameInternal(mfxBitstream *pMfxBitstream, int64_t *pWrittenDts) {
    if (!m_Mux.format.bFileHeaderWritten) {
        //HEVCエンコードでは、DecodeTimeStampが正しく設定されない
        if (pMfxBitstream->DecodeTimeStamp == MFX_TIMESTAMP_UNKNOWN) {
            m_Mux.video.bDtsUnavailable = true;
        }
        mfxStatus sts = WriteFileHeader(&m_Mux.video.mfxParam, &m_Mux.video.mfxCop2, pMfxBitstream);
        if (sts != MFX_ERR_NONE) {
            return sts;
        }
    }

    const int bIsPAFF = !!m_Mux.video.bIsPAFF;
    for (mfxU32 i = 0, frameSize = pMfxBitstream->DataLength; frameSize > 0; i++) {
        const mfxU32 bytesToWrite = (bIsPAFF) ? getH264PAFFFieldLength(pMfxBitstream->Data + pMfxBitstream->DataOffset, frameSize) : frameSize;
        AVPacket pkt = { 0 };
        av_init_packet(&pkt);
        av_new_packet(&pkt, bytesToWrite);
        memcpy(pkt.data, pMfxBitstream->Data + pMfxBitstream->DataOffset, bytesToWrite);
        pkt.size = bytesToWrite;

        const AVRational fpsTimebase = av_div_q({1, 1 + bIsPAFF}, m_Mux.video.nFPS);
        const AVRational streamTimebase = m_Mux.video.pStream->codec->pkt_timebase;
        pkt.stream_index = m_Mux.video.pStream->index;
        pkt.flags        = !!(pMfxBitstream->FrameType & (MFX_FRAMETYPE_IDR << (i<<3)));
        pkt.duration     = (int)av_rescale_q(1, fpsTimebase, streamTimebase);
        pkt.pts          = av_rescale_q(av_rescale_q(pMfxBitstream->TimeStamp, QSV_NATIVE_TIMEBASE, fpsTimebase), fpsTimebase, streamTimebase) + bIsPAFF * i * pkt.duration;
        if (!m_Mux.video.bDtsUnavailable) {
            pkt.dts = av_rescale_q(av_rescale_q(pMfxBitstream->DecodeTimeStamp, QSV_NATIVE_TIMEBASE, fpsTimebase), fpsTimebase, streamTimebase) + bIsPAFF * i * pkt.duration;
        } else {
            pkt.dts = av_rescale_q(m_Mux.video.nFpsBaseNextDts, fpsTimebase, streamTimebase);
            m_Mux.video.nFpsBaseNextDts++;
        }
        const auto pts = pkt.pts, dts = pkt.dts, duration = pkt.duration;
        *pWrittenDts = av_rescale_q(pkt.dts, streamTimebase, QSV_NATIVE_TIMEBASE);
        m_Mux.format.bStreamError |= 0 != av_interleaved_write_frame(m_Mux.format.pFormatCtx, &pkt);

        frameSize -= bytesToWrite;
        pMfxBitstream->DataOffset += bytesToWrite;
        if (m_Mux.video.fpTsLogFile) {
            const uint32_t frameType = pMfxBitstream->FrameType >> (i<<3);
            const TCHAR *pFrameTypeStr = (frameType & (MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_I)) ? _T("I") : ((frameType & MFX_FRAMETYPE_P) ? _T("P") : _T("B"));
            _ftprintf(m_Mux.video.fpTsLogFile, _T("%s, %20lld, %20lld, %20lld, %20lld, %d, %7d\n"), pFrameTypeStr, (lls)pMfxBitstream->TimeStamp, (lls)pMfxBitstream->DecodeTimeStamp, (lls)pts, (lls)dts, (int)duration, bytesToWrite);
        }
    }
    m_pEncSatusInfo->SetOutputData(pMfxBitstream->DataLength, pMfxBitstream->FrameType);
#if ENABLE_AVCODEC_OUT_THREAD
    //最初のヘッダーを書いたパケットはコピーではないので、キューに入れない
    if (m_Mux.thread.thOutput.joinable() && m_Mux.format.bFileHeaderWritten) {
        //確保したメモリ領域を使いまわすためにスタックに格納
        auto& qVideoQueueFree = (pMfxBitstream->FrameType & MFX_FRAMETYPE_I) ? m_Mux.thread.qVideobitstreamFreeI : m_Mux.thread.qVideobitstreamFreePB;
        qVideoQueueFree.push(*pMfxBitstream);
    } else {
#endif
        pMfxBitstream->DataLength = 0;
        pMfxBitstream->DataOffset = 0;
#if ENABLE_AVCODEC_OUT_THREAD
    }
#endif
    //最初のヘッダーを書いたパケットが書き終わってからフラグを立てる
    //このタイミングで立てないと出力スレッドが先に動作してしまうことがある
    m_Mux.format.bFileHeaderWritten = true;
    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

#pragma warning(push)
#pragma warning(disable: 4100)
mfxStatus CAvcodecWriter::WriteNextFrame(mfxFrameSurface1 *pSurface) {
    return MFX_ERR_UNSUPPORTED;
}
#pragma warning(pop)

vector<int> CAvcodecWriter::GetStreamTrackIdList() {
    vector<int> streamTrackId;
    streamTrackId.reserve(m_Mux.audio.size());
    for (auto audio : m_Mux.audio) {
        streamTrackId.push_back(audio.nInTrackId);
    }
    for (auto sub : m_Mux.sub) {
        streamTrackId.push_back(sub.nInTrackId);
    }
    return std::move(streamTrackId);
}

AVMuxAudio *CAvcodecWriter::getAudioPacketStreamData(const AVPacket *pkt) {
    const int streamIndex = pkt->stream_index;
    //privには、trackIdへのポインタが格納してある…はず
    const int inTrackId = (int16_t)(pkt->flags >> 16);
    for (int i = 0; i < (int)m_Mux.audio.size(); i++) {
        //streamIndexの一致とtrackIdの一致を確認する
        if (m_Mux.audio[i].nStreamIndexIn == streamIndex
            && m_Mux.audio[i].nInTrackId == inTrackId) {
            return &m_Mux.audio[i];
        }
    }
    return nullptr;
}

AVMuxAudio *CAvcodecWriter::getAudioStreamData(int nTrackId, int nSubStreamId) {
    for (int i = 0; i < (int)m_Mux.audio.size(); i++) {
        //streamIndexの一致とtrackIdの一致を確認する
        if (m_Mux.audio[i].nInTrackId == nTrackId
            && m_Mux.audio[i].nInSubStream == nSubStreamId) {
            return &m_Mux.audio[i];
        }
    }
    return nullptr;
}

AVMuxSub *CAvcodecWriter::getSubPacketStreamData(const AVPacket *pkt) {
    const int streamIndex = pkt->stream_index;
    //privには、trackIdへのポインタが格納してある…はず
    const int inTrackId = (int16_t)(pkt->flags >> 16);
    for (int i = 0; i < (int)m_Mux.sub.size(); i++) {
        //streamIndexの一致とtrackIdの一致を確認する
        if (m_Mux.sub[i].nStreamIndexIn == streamIndex
            && m_Mux.sub[i].nInTrackId == inTrackId) {
            return &m_Mux.sub[i];
        }
    }
    return NULL;
}

int CAvcodecWriter::applyBitstreamFilterAAC(AVPacket *pkt, AVMuxAudio *pMuxAudio) {
    int ret = 0;
    //毎回bitstream filterを初期化して、extradataに新しいヘッダを供給する
    //動画とmuxする際に必須
    av_bsf_free(&pMuxAudio->pAACBsfc);
    auto filter = av_bsf_get_by_name("aac_adtstoasc");
    if (filter == nullptr) {
        AddMessage(QSV_LOG_ERROR, _T("failed to find aac_adtstoasc.\n"));
        return 1;
    }
    if (0 > (ret = av_bsf_alloc(filter, &pMuxAudio->pAACBsfc))) {
        AddMessage(QSV_LOG_ERROR, _T("failed to allocate memory for aac_adtstoasc: %s.\n"), qsv_av_err2str(ret).c_str());
        return 1;
    }
    if (0 > (ret = avcodec_parameters_from_context(pMuxAudio->pAACBsfc->par_in, pMuxAudio->pCodecCtxIn))) {
        AddMessage(QSV_LOG_ERROR, _T("failed to set parameter for aac_adtstoasc: %s.\n"), qsv_av_err2str(ret).c_str());
        return 1;
    }
    pMuxAudio->pAACBsfc->time_base_in = pMuxAudio->pCodecCtxIn->time_base;
    if (0 > (ret = av_bsf_init(pMuxAudio->pAACBsfc))) {
        AddMessage(QSV_LOG_ERROR, _T("failed to init aac_adtstoasc: %s.\n"), qsv_av_err2str(ret).c_str());
        return 1;
    }
    if (0 > (ret = av_bsf_send_packet(pMuxAudio->pAACBsfc, pkt))) {
        av_packet_unref(pkt);
        AddMessage(QSV_LOG_ERROR, _T("failed to send packet to aac_adtstoasc bitstream filter: %s.\n"), qsv_av_err2str(ret).c_str());
        return 1;
    }
    ret = av_bsf_receive_packet(pMuxAudio->pAACBsfc, pkt);
    if (ret == AVERROR(EAGAIN)) {
        pkt->size = 0;
        pkt->duration = 0;
    } else if ((ret < 0 && ret != AVERROR_EOF) || pkt->size < 0) {
        //最初のフレームとかでなければ、エラーを許容し、単に処理しないようにする
        //多くの場合、ここでのエラーはtsなどの最終音声フレームが不完全なことで発生する
        //先頭から連続30回エラーとなった場合はおかしいのでエラー終了するようにする
        if (pMuxAudio->nPacketWritten == 0) {
            pMuxAudio->nAACBsfErrorFromStart++;
            static int AACBSFFILTER_ERROR_THRESHOLD = 30;
            if (pMuxAudio->nAACBsfErrorFromStart > AACBSFFILTER_ERROR_THRESHOLD) {
                m_Mux.format.bStreamError = true;
                AddMessage(QSV_LOG_ERROR, _T("failed to run aac_adtstoasc bitstream filter for %d times: %s.\n"), AACBSFFILTER_ERROR_THRESHOLD, qsv_av_err2str(ret).c_str());
                return 1;
            }
        }
        AddMessage(QSV_LOG_WARN, _T("failed to run aac_adtstoasc bitstream filter: %s.\n"), qsv_av_err2str(ret).c_str());
        pkt->duration = 0; //書き込み処理が行われないように
        return -1;
    }
    pMuxAudio->nAACBsfErrorFromStart = 0;
    return 0;
}

//音声/字幕パケットを実際に書き出す
// pMuxAudio ... [i]  pktに対応するストリーム情報
// pkt       ... [io] 書き出す音声/字幕パケット この関数でデータはav_interleaved_write_frameに渡されるか解放される
// samples   ... [i]  pktのsamples数 音声処理時のみ有効 / 字幕の際は0を渡すべき
// dts       ... [o]  書き出したパケットの最終的なdtsをQSV_NATIVE_TIMEBASEで返す
void CAvcodecWriter::WriteNextPacketProcessed(AVMuxAudio *pMuxAudio, AVPacket *pkt, int samples, int64_t *pWrittenDts) {
    if (pkt == nullptr || pkt->buf == nullptr) {
        for (uint32_t i = 0; i < m_Mux.audio.size(); i++) {
            AudioFlushStream(&m_Mux.audio[i], pWrittenDts);
        }
        *pWrittenDts = INT64_MAX;
        AddMessage(QSV_LOG_DEBUG, _T("Flushed audio buffer.\n"));
        return;
    }
    AVRational samplerate = { 1, (pMuxAudio->pOutCodecEncodeCtx) ? pMuxAudio->pOutCodecEncodeCtx->sample_rate : pMuxAudio->pCodecCtxIn->sample_rate };
    if (samples) {
        //durationについて、sample数から出力ストリームのtimebaseに変更する
        pkt->stream_index = pMuxAudio->pStream->index;
        pkt->flags        = AV_PKT_FLAG_KEY; //元のpacketの上位16bitにはトラック番号を紛れ込ませているので、av_interleaved_write_frame前に消すこと
        pkt->dts          = av_rescale_q(pMuxAudio->nOutputSamples + pMuxAudio->nDelaySamplesOfAudio, samplerate, pMuxAudio->pStream->time_base);
        pkt->pts          = pkt->dts;
        pkt->duration     = (int)av_rescale_q(samples, samplerate, pMuxAudio->pStream->time_base);
        if (pkt->duration == 0)
            pkt->duration = (int)(pkt->pts - pMuxAudio->nLastPtsOut);
        pMuxAudio->nLastPtsOut = pkt->pts;
        *pWrittenDts = av_rescale_q(pkt->dts, pMuxAudio->pStream->time_base, QSV_NATIVE_TIMEBASE);
        m_Mux.format.bStreamError |= 0 != av_interleaved_write_frame(m_Mux.format.pFormatCtx, pkt);
        pMuxAudio->nOutputSamples += samples;
    } else {
        //av_interleaved_write_frameに渡ったパケットは開放する必要がないが、
        //それ以外は解放してやる必要がある
        av_packet_unref(pkt);
    }
}

//音声/字幕パケットを実際に書き出す (構造体版)
// pktData->pMuxAudio ... [i]  pktに対応するストリーム情報
// &pktData->pkt      ... [io] 書き出す音声/字幕パケット この関数でデータはav_interleaved_write_frameに渡されるか解放される
// pktData->samples   ... [i]  pktのsamples数 音声処理時のみ有効 / 字幕の際は0を渡すべき
// &pktData->dts      ... [o]  書き出したパケットの最終的なdtsをQSV_NATIVE_TIMEBASEで返す
void CAvcodecWriter::WriteNextPacketProcessed(AVPktMuxData *pktData) {
    return WriteNextPacketProcessed(pktData->pMuxAudio, &pktData->pkt, pktData->samples, &pktData->dts);
}

AVFrame *CAvcodecWriter::AudioDecodePacket(AVMuxAudio *pMuxAudio, const AVPacket *pkt, int *got_result) {
    *got_result = FALSE;
    if (pMuxAudio->nDecodeError > pMuxAudio->nIgnoreDecodeError) {
        return nullptr;
    }
    const AVPacket *pktIn = pkt;
    if (pMuxAudio->OutPacket.size != 0) {
        int currentSize = pMuxAudio->OutPacket.size;
        if (pMuxAudio->OutPacket.buf->size < currentSize + pkt->size) {
            av_grow_packet(&pMuxAudio->OutPacket, currentSize + pkt->size);
        }
        memcpy(pMuxAudio->OutPacket.data + currentSize, pkt->data, pkt->size);
        pMuxAudio->OutPacket.size = currentSize + pkt->size;
        pktIn = &pMuxAudio->OutPacket;
        av_packet_copy_props(&pMuxAudio->OutPacket, pkt);
    }
    //最終的な出力フレーム
    AVFrame *decodedFrame = av_frame_alloc();
    while (!(*got_result) || pktIn->size > 0) {
        //ひとつのパケットをデコード
        AVFrame *decodedData = av_frame_alloc();
        int len = avcodec_decode_audio4(pMuxAudio->pOutCodecDecodeCtx, decodedData, got_result, pktIn);
        if (len < 0) {
            pMuxAudio->nDecodeError++;
            AddMessage(QSV_LOG_WARN, _T("avcodec writer: failed to decode audio #%d: %s\n"), pMuxAudio->nInTrackId, qsv_av_err2str(len).c_str());
            if (decodedData) {
                av_frame_free(&decodedData);
            }
            decodedData = nullptr;
            if (pMuxAudio->nDecodeError <= pMuxAudio->nIgnoreDecodeError) {
                //デコードエラーを無視する場合、入力パケットのサイズ分、無音を挿入する
                AVRational samplerate = { 1, pMuxAudio->nResamplerInSampleRate };
                decodedData                 = av_frame_alloc();
                decodedData->nb_samples     = (int)av_rescale_q(pktIn->duration, pMuxAudio->pCodecCtxIn->pkt_timebase, samplerate);
                decodedData->channels       = pMuxAudio->nResamplerInChannels;
                decodedData->channel_layout = pMuxAudio->nResamplerInChannelLayout;
                decodedData->sample_rate    = pMuxAudio->nResamplerInSampleRate;
                decodedData->format         = pMuxAudio->ResamplerInSampleFmt;
                av_frame_get_buffer(decodedData, 32); //format, channel_layout, nb_samplesを埋めて、av_frame_get_buffer()により、メモリを確保する
                av_samples_set_silence((uint8_t **)decodedData->data, 0, decodedData->nb_samples, decodedData->channels, (AVSampleFormat)decodedData->format);

                len = pktIn->size;
            } else {
                AddMessage(QSV_LOG_ERROR, _T("avcodec writer: failed to decode audio #%d for %d times.\n"), pMuxAudio->nInTrackId, pMuxAudio->nDecodeError);
                if (decodedFrame) {
                    av_frame_free(&decodedFrame);
                }
                decodedFrame = nullptr;
                m_Mux.format.bStreamError = true;
                break;
            }
        } else {
            pMuxAudio->nDecodeError = 0;
        }
        //decodedFrameにすでにデータがあれば、decodedDataのデータを連結する
        if (decodedFrame->nb_samples && decodedData->nb_samples) {
            AVFrame *decodedFrameNew        = av_frame_alloc();
            decodedFrameNew->nb_samples     = decodedFrame->nb_samples + decodedData->nb_samples;
            decodedFrameNew->channels       = decodedData->channels;
            decodedFrameNew->channel_layout = decodedData->channel_layout;
            decodedFrameNew->sample_rate    = decodedData->sample_rate;
            decodedFrameNew->format         = decodedData->format;
            av_frame_get_buffer(decodedFrameNew, 32); //format, channel_layout, nb_samplesを埋めて、av_frame_get_buffer()により、メモリを確保する
            const int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)decodedFrameNew->format)
                * (av_sample_fmt_is_planar((AVSampleFormat)decodedFrameNew->format) ? 1 : decodedFrameNew->channels);
            const int channel_loop_count = av_sample_fmt_is_planar((AVSampleFormat)decodedFrameNew->format) ? decodedFrameNew->channels : 1;
            for (int i = 0; i < channel_loop_count; i++) {
                if (decodedFrame->nb_samples > 0) {
                    memcpy(decodedFrameNew->data[i], decodedFrame->data[i], decodedFrame->nb_samples * bytes_per_sample);
                }
                if (decodedData->nb_samples > 0) {
                    memcpy(decodedFrameNew->data[i] + decodedFrame->nb_samples * bytes_per_sample,
                        decodedData->data[i], decodedData->nb_samples * bytes_per_sample);
                }
            }
            av_frame_free(&decodedFrame);
            decodedFrame = decodedFrameNew;
        } else if (decodedData->nb_samples) {
            av_frame_free(&decodedFrame);
            decodedFrame = decodedData;
        }
        if (pktIn->size != len) {
            int newLen = pktIn->size - len;
            memmove(pMuxAudio->OutPacket.data, pktIn->data + len, newLen);
            pMuxAudio->OutPacket.size = newLen;
            pktIn = &pMuxAudio->OutPacket;
        } else {
            pMuxAudio->OutPacket.size = 0;
            break;
        }
    }
    *got_result = decodedFrame && decodedFrame->nb_samples > 0;
    return decodedFrame;
}

//音声をフィルタ
mfxStatus CAvcodecWriter::AudioFilterFrame(AVPktMuxData *pktData) {
    const bool bFlush = pktData->pFrame == nullptr;
    AVMuxAudio *pMuxAudio = pktData->pMuxAudio;
    if (pktData->pMuxAudio->pFilterGraph == nullptr) {
        return MFX_ERR_NONE;
    }
    if (pktData->pFrame != nullptr) {
        //音声入力フォーマットに変更がないか確認し、もしあればresamplerを再初期化する
        auto sts = InitAudioFilter(pMuxAudio, pktData->pFrame->channels, pktData->pFrame->channel_layout, pktData->pFrame->sample_rate, (AVSampleFormat)pktData->pFrame->format);
        if (sts != MFX_ERR_NONE) {
            m_Mux.format.bStreamError = true;
            return MFX_ERR_UNKNOWN;
        }
    }
    //フィルターチェーンにフレームを追加
    if (av_buffersrc_add_frame_flags(pMuxAudio->pFilterBufferSrcCtx, pktData->pFrame, AV_BUFFERSRC_FLAG_PUSH) < 0) {
        AddMessage(QSV_LOG_ERROR, _T("failed to feed the audio filtergraph\n"));
        m_Mux.format.bStreamError = true;
        av_frame_unref(pktData->pFrame);
        return MFX_ERR_UNKNOWN;
    }
    pktData->pFrame = nullptr;
    const int bytes_per_sample = av_get_bytes_per_sample(pMuxAudio->pOutCodecDecodeCtx->sample_fmt)
        * (av_sample_fmt_is_planar(pMuxAudio->pOutCodecDecodeCtx->sample_fmt) ? 1 : pMuxAudio->pOutCodecDecodeCtx->channels);
    const int channel_loop_count = av_sample_fmt_is_planar(pMuxAudio->pOutCodecDecodeCtx->sample_fmt) ? pMuxAudio->pOutCodecDecodeCtx->channels : 1;
    for (;;) {
        AVFrame *pFilteredFrame = av_frame_alloc();
        int ret = av_buffersink_get_frame_flags(pMuxAudio->pFilterBufferSinkCtx, pFilteredFrame, AV_BUFFERSINK_FLAG_NO_REQUEST );
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            m_Mux.format.bStreamError = true;
            av_frame_unref(pFilteredFrame);
            return MFX_ERR_UNKNOWN;
        }
        //それまでにたまっているキャッシュがあれば、それを結合する
        if (pktData->pFrame) {
            //pktData->pFrameとpFilteredFrameを結合
            AVFrame *pCombinedFrame = av_frame_alloc();
            pCombinedFrame->format = pMuxAudio->pOutCodecDecodeCtx->sample_fmt;
            pCombinedFrame->channel_layout = pMuxAudio->pOutCodecDecodeCtx->channel_layout;
            pCombinedFrame->nb_samples = pFilteredFrame->nb_samples + pktData->pFrame->nb_samples;
            av_frame_get_buffer(pCombinedFrame, 32); //format, channel_layout, nb_samplesを埋めて、av_frame_get_buffer()により、メモリを確保する
            for (int i = 0; i < channel_loop_count; i++) {
                uint32_t cachedBytes = pktData->pFrame->nb_samples * bytes_per_sample;
                memcpy(pCombinedFrame->data[i], pktData->pFrame->data[i], cachedBytes);
                memcpy(pCombinedFrame->data[i] + cachedBytes, pFilteredFrame->data[i], pFilteredFrame->nb_samples * bytes_per_sample);
            }
            //結合し終わっていらないものは破棄
            av_frame_free(&pktData->pFrame);
            av_frame_free(&pFilteredFrame);
            pktData->pFrame = pCombinedFrame;
        } else {
            pktData->pFrame = pFilteredFrame;
        }
    }
    return MFX_ERR_NONE;
}

//音声をresample
int CAvcodecWriter::AudioResampleFrame(AVMuxAudio *pMuxAudio, AVFrame **frame) {
    if (*frame != nullptr) {
        //音声入力フォーマットに変更がないか確認し、もしあればresamplerを再初期化する
        auto sts = InitAudioResampler(pMuxAudio, (*frame)->channels, (*frame)->channel_layout, (*frame)->sample_rate, (AVSampleFormat)((*frame)->format));
        if (sts != MFX_ERR_NONE) {
            m_Mux.format.bStreamError = true;
            return -1;
        }
    }
    int ret = 0;
    if (pMuxAudio->pSwrContext) {
        const uint32_t dst_nb_samples = (uint32_t)av_rescale_rnd(
            swr_get_delay(pMuxAudio->pSwrContext, pMuxAudio->pOutCodecEncodeCtx->sample_rate) + ((*frame) ? (*frame)->nb_samples : 0),
            pMuxAudio->pOutCodecEncodeCtx->sample_rate, pMuxAudio->pOutCodecEncodeCtx->sample_rate, AV_ROUND_UP);
        if (dst_nb_samples > 0) {
            if (dst_nb_samples > pMuxAudio->nSwrBufferSize) {
                av_free(pMuxAudio->pSwrBuffer[0]);
                av_samples_alloc(pMuxAudio->pSwrBuffer, &pMuxAudio->nSwrBufferLinesize,
                    pMuxAudio->pOutCodecEncodeCtx->channels, dst_nb_samples * 2, pMuxAudio->pOutCodecEncodeCtx->sample_fmt, 0);
                pMuxAudio->nSwrBufferSize = dst_nb_samples * 2;
            }
            if (0 > (ret = swr_convert(pMuxAudio->pSwrContext,
                pMuxAudio->pSwrBuffer, dst_nb_samples,
                (*frame) ? (const uint8_t **)(*frame)->data : nullptr,
                (*frame) ? (*frame)->nb_samples : 0))) {
                AddMessage(QSV_LOG_ERROR, _T("avcodec writer: failed to convert sample format #%d: %s\n"), pMuxAudio->nInTrackId, qsv_av_err2str(ret).c_str());
                m_Mux.format.bStreamError = true;
            }
            if (*frame) {
                av_frame_free(frame);
            }

            if (ret >= 0 && dst_nb_samples > 0) {
                AVFrame *pResampledFrame        = av_frame_alloc();
                pResampledFrame->nb_samples     = ret;
                pResampledFrame->channels       = pMuxAudio->pOutCodecEncodeCtx->channels;
                pResampledFrame->channel_layout = pMuxAudio->pOutCodecEncodeCtx->channel_layout;
                pResampledFrame->sample_rate    = pMuxAudio->pOutCodecEncodeCtx->sample_rate;
                pResampledFrame->format         = pMuxAudio->pOutCodecEncodeCtx->sample_fmt;
                av_frame_get_buffer(pResampledFrame, 32); //format, channel_layout, nb_samplesを埋めて、av_frame_get_buffer()により、メモリを確保する
                const int bytes_per_sample = av_get_bytes_per_sample(pMuxAudio->pOutCodecEncodeCtx->sample_fmt)
                    * (av_sample_fmt_is_planar(pMuxAudio->pOutCodecEncodeCtx->sample_fmt) ? 1 : pMuxAudio->pOutCodecEncodeCtx->channels);
                const int channel_loop_count = av_sample_fmt_is_planar(pMuxAudio->pOutCodecEncodeCtx->sample_fmt) ? pMuxAudio->pOutCodecEncodeCtx->channels : 1;
                for (int i = 0; i < channel_loop_count; i++) {
                    memcpy(pResampledFrame->data[i], pMuxAudio->pSwrBuffer[i], pResampledFrame->nb_samples * bytes_per_sample);
                }
                (*frame) = pResampledFrame;
            }
        }
    }
    return ret;
}

//音声をエンコード
int CAvcodecWriter::AudioEncodeFrame(AVMuxAudio *pMuxAudio, AVPacket *pEncPkt, const AVFrame *frame, int *got_result) {
    memset(pEncPkt, 0, sizeof(pEncPkt[0])); //av_init_packetでsizeなどは初期化されないので0をセット
    av_init_packet(pEncPkt);
    int samples = 0;
    int ret = avcodec_encode_audio2(pMuxAudio->pOutCodecEncodeCtx, pEncPkt, frame, got_result);
    if (ret < 0) {
        AddMessage(QSV_LOG_WARN, _T("avcodec writer: failed to encode audio #%d: %s\n"), pMuxAudio->nInTrackId, qsv_av_err2str(ret).c_str());
        pMuxAudio->bEncodeError = true;
    } else if (*got_result) {
        samples = (int)av_rescale_q(pEncPkt->duration, pMuxAudio->pOutCodecEncodeCtx->pkt_timebase, { 1, pMuxAudio->pCodecCtxIn->sample_rate });
    }
    return samples;
}

void CAvcodecWriter::AudioFlushStream(AVMuxAudio *pMuxAudio, int64_t *pWrittenDts) {
    while (pMuxAudio->pOutCodecDecodeCtx && !pMuxAudio->bEncodeError) {
        int got_result = 0;
        AVPacket pkt = { 0 };
        AVFrame *decodedFrame = AudioDecodePacket(pMuxAudio, &pkt, &got_result);
        if (!got_result && (decodedFrame != nullptr || pMuxAudio->nDecodeError > pMuxAudio->nIgnoreDecodeError)) {
            if (decodedFrame != nullptr) {
                av_frame_free(&decodedFrame);
            }
            break;
        }

        AVPktMuxData pktData = { 0 };
        pktData.type = MUX_DATA_TYPE_FRAME;
        pktData.pFrame = decodedFrame;
        pktData.pMuxAudio = pMuxAudio;

        //フィルタリングを行う
        auto sts = AudioFilterFrame(&pktData);
        if (sts != MFX_ERR_NONE) break;

        if (pktData.pFrame) {
            WriteNextPacketToAudioSubtracks(&pktData);
        }
    }
    if (pMuxAudio->pFilterGraph) {
        AVPktMuxData pktData = { 0 };
        pktData.type = MUX_DATA_TYPE_FRAME;
        pktData.pMuxAudio = pMuxAudio;

        //フィルタリングを行う
        auto sts = AudioFilterFrame(&pktData);
        if (sts == MFX_ERR_NONE && pktData.pFrame) {
            WriteNextPacketToAudioSubtracks(&pktData);
        }
    }
    while (pMuxAudio->pSwrContext && !pMuxAudio->bEncodeError) {
        int samples = 0;
        int got_result = 0;
        AVPacket pkt = { 0 };
        AVFrame *decodedFrame = nullptr;
        if (0 != AudioResampleFrame(pMuxAudio, &decodedFrame) || decodedFrame == nullptr) {
            break;
        }
        samples = AudioEncodeFrame(pMuxAudio, &pkt, decodedFrame, &got_result);
        WriteNextPacketProcessed(pMuxAudio, &pkt, samples, pWrittenDts);
    }
    while (pMuxAudio->pOutCodecEncodeCtx) {
        int got_result = 0;
        AVPacket pkt = { 0 };
        int samples = AudioEncodeFrame(pMuxAudio, &pkt, nullptr, &got_result);
        if (samples == 0 || pMuxAudio->nDecodeError > pMuxAudio->nIgnoreDecodeError)
            break;
        WriteNextPacketProcessed(pMuxAudio, &pkt, samples, pWrittenDts);
    }
}

mfxStatus CAvcodecWriter::SubtitleTranscode(const AVMuxSub *pMuxSub, AVPacket *pkt) {
    int got_sub = 0;
    AVSubtitle sub = { 0 };
    if (0 > avcodec_decode_subtitle2(pMuxSub->pOutCodecDecodeCtx, &sub, &got_sub, pkt)) {
        AddMessage(QSV_LOG_ERROR, _T("Failed to decode subtitle.\n"));
        m_Mux.format.bStreamError = true;
    }
    if (!pMuxSub->pBuf) {
        AddMessage(QSV_LOG_ERROR, _T("No buffer for encoding subtitle.\n"));
        m_Mux.format.bStreamError = true;
    }
    av_packet_unref(pkt);
    if (m_Mux.format.bStreamError)
        return MFX_ERR_UNKNOWN;
    if (!got_sub || sub.num_rects == 0)
        return MFX_ERR_NONE;

    const int nOutPackets = 1 + (pMuxSub->pOutCodecEncodeCtx->codec_id == AV_CODEC_ID_DVB_SUBTITLE);
    for (int i = 0; i < nOutPackets; i++) {
        sub.pts               += av_rescale_q(sub.start_display_time, av_make_q(1, 1000), av_make_q(1, AV_TIME_BASE));
        sub.end_display_time  -= sub.start_display_time;
        sub.start_display_time = 0;
        if (i > 0) {
            sub.num_rects = 0;
        }

        int sub_out_size = avcodec_encode_subtitle(pMuxSub->pOutCodecEncodeCtx, pMuxSub->pBuf, SUB_ENC_BUF_MAX_SIZE, &sub);
        if (sub_out_size < 0) {
            AddMessage(QSV_LOG_ERROR, _T("failed to encode subtitle.\n"));
            m_Mux.format.bStreamError = true;
            return MFX_ERR_UNKNOWN;
        }

        AVPacket pktOut;
        av_init_packet(&pktOut);
        pktOut.data = pMuxSub->pBuf;
        pktOut.stream_index = pMuxSub->pStream->index;
        pktOut.size = sub_out_size;
        pktOut.duration = (int)av_rescale_q(sub.end_display_time, av_make_q(1, 1000), pMuxSub->pStream->time_base);
        pktOut.pts  = av_rescale_q(sub.pts, av_make_q(1, AV_TIME_BASE), pMuxSub->pStream->time_base);
        if (pMuxSub->pOutCodecEncodeCtx->codec_id == AV_CODEC_ID_DVB_SUBTITLE) {
            pktOut.pts += 90 * ((i == 0) ? sub.start_display_time : sub.end_display_time);
        }
        pktOut.dts = pktOut.pts;
        m_Mux.format.bStreamError |= 0 != av_interleaved_write_frame(m_Mux.format.pFormatCtx, &pktOut);
    }
    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::SubtitleWritePacket(AVPacket *pkt) {
    //字幕を処理する
    const AVMuxSub *pMuxSub = getSubPacketStreamData(pkt);
    const AVRational vid_pkt_timebase = (m_Mux.video.pInputCodecCtx) ? m_Mux.video.pInputCodecCtx->pkt_timebase : av_inv_q(m_Mux.video.nFPS);
    const int64_t pts_adjust = av_rescale_q(m_Mux.video.nInputFirstKeyPts, vid_pkt_timebase, pMuxSub->pCodecCtxIn->pkt_timebase);
    //ptsが存在しない場合はないものとすると、AdjustTimestampTrimmedの結果がAV_NOPTS_VALUEとなるのは、
    //Trimによりカットされたときのみ
    const int64_t pts_orig = pkt->pts;
    if (AV_NOPTS_VALUE != (pkt->pts = AdjustTimestampTrimmed(std::max(INT64_C(0), pkt->pts - pts_adjust), pMuxSub->pCodecCtxIn->pkt_timebase, pMuxSub->pStream->time_base, false))) {
        if (pMuxSub->pOutCodecEncodeCtx) {
            return SubtitleTranscode(pMuxSub, pkt);
        }
        //dts側にもpts側に加えたのと同じ分だけの補正をかける
        pkt->dts = pkt->dts + (av_rescale_q(pkt->pts, pMuxSub->pStream->time_base, pMuxSub->pCodecCtxIn->pkt_timebase) - pts_orig);
        //timescaleの変換を行い、負の値をとらないようにする
        pkt->dts = std::max(INT64_C(0), av_rescale_q(pkt->dts, pMuxSub->pCodecCtxIn->pkt_timebase, pMuxSub->pStream->time_base));
        pkt->flags &= 0x0000ffff; //元のpacketの上位16bitにはトラック番号を紛れ込ませているので、av_interleaved_write_frame前に消すこと
        pkt->duration = (int)av_rescale_q(pkt->duration, pMuxSub->pCodecCtxIn->pkt_timebase, pMuxSub->pStream->time_base);
        pkt->stream_index = pMuxSub->pStream->index;
        pkt->pos = -1;
        m_Mux.format.bStreamError |= 0 != av_interleaved_write_frame(m_Mux.format.pFormatCtx, pkt);
    }
    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

AVPktMuxData CAvcodecWriter::pktMuxData(const AVPacket *pkt) {
    AVPktMuxData data = { 0 };
    data.type = MUX_DATA_TYPE_PACKET;
    if (pkt) {
        data.pkt = *pkt;
        data.pMuxAudio = getAudioPacketStreamData(pkt);
    }
    return data;
}

AVPktMuxData CAvcodecWriter::pktMuxData(AVFrame *pFrame) {
    AVPktMuxData data = { 0 };
    data.type = MUX_DATA_TYPE_FRAME;
    data.pFrame = pFrame;
    return data;
}

mfxStatus CAvcodecWriter::WriteNextPacket(AVPacket *pkt) {
    AVPktMuxData pktData = pktMuxData(pkt);
#if ENABLE_AVCODEC_OUT_THREAD
    if (m_Mux.thread.thOutput.joinable()) {
        auto& audioQueue   = (m_Mux.thread.thAudProcess.joinable()) ? m_Mux.thread.qAudioPacketProcess : m_Mux.thread.qAudioPacketOut;
        auto heEventPktAdd = (m_Mux.thread.thAudProcess.joinable()) ? m_Mux.thread.heEventPktAddedAudProcess : m_Mux.thread.heEventPktAddedOutput;
        //pkt = nullptrの代理として、pkt.buf == nullptrなパケットを投入
        AVPktMuxData zeroFilled = { 0 };
        if (!audioQueue.push((pkt == nullptr) ? zeroFilled : pktData)) {
            AddMessage(QSV_LOG_ERROR, _T("Failed to allocate memory for audio packet queue.\n"));
            m_Mux.format.bStreamError = true;
        }
        SetEvent(heEventPktAdd);
        return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
    }
#endif
    return WriteNextPacketInternal(&pktData);
}

//指定された音声キューに追加する
mfxStatus CAvcodecWriter::AddAudQueue(AVPktMuxData *pktData, int type) {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    if (m_Mux.thread.thAudProcess.joinable()) {
        //出力キューに追加する
        auto& qAudio       = (type == AUD_QUEUE_OUT) ? m_Mux.thread.qAudioPacketOut       : ((type == AUD_QUEUE_PROCESS) ? m_Mux.thread.qAudioPacketProcess       : m_Mux.thread.qAudioFrameEncode);
        auto& heEventAdded = (type == AUD_QUEUE_OUT) ? m_Mux.thread.heEventPktAddedOutput : ((type == AUD_QUEUE_PROCESS) ? m_Mux.thread.heEventPktAddedAudProcess : m_Mux.thread.heEventPktAddedAudEncode);
        if (!qAudio.push(*pktData)) {
            AddMessage(QSV_LOG_ERROR, _T("Failed to allocate memory for audio queue.\n"));
            m_Mux.format.bStreamError = true;
        }
        SetEvent(heEventAdded);
        return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
    } else
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    {
        return MFX_ERR_NOT_INITIALIZED;
    }
}

//音声処理スレッドが存在する場合、この関数は音声処理スレッドによって処理される
//音声処理スレッドがなく、出力スレッドがあれば、出力スレッドにより処理される
//出力スレッドがなければメインエンコードスレッドが処理する
mfxStatus CAvcodecWriter::WriteNextPacketInternal(AVPktMuxData *pktData) {
    if (!m_Mux.format.bFileHeaderWritten) {
        //まだフレームヘッダーが書かれていなければ、パケットをキャッシュして終了
        m_AudPktBufFileHead.push_back(*pktData);
        return MFX_ERR_NONE;
    }
    //m_AudPktBufFileHeadにキャッシュしてあるパケットかどうかを調べる
    if (m_AudPktBufFileHead.end() == std::find_if(m_AudPktBufFileHead.begin(), m_AudPktBufFileHead.end(),
        [pktData](const AVPktMuxData& data) { return pktData->pkt.buf == data.pkt.buf; })) {
        //キャッシュしてあるパケットでないなら、キャッシュしてあるパケットをまず処理する
        for (auto bufPkt : m_AudPktBufFileHead) {
            mfxStatus sts = WriteNextPacketInternal(&bufPkt);
            if (sts != MFX_ERR_NONE) {
                return sts;
            }
        }
        //キャッシュをすべて書き出したらクリア
        m_AudPktBufFileHead.clear();
    }

    if (pktData->pkt.data == nullptr) {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        if (m_Mux.thread.thAudProcess.joinable()) {
            //音声処理を別スレッドでやっている場合は、AddAudOutputQueueを後段の出力スレッドで行う必要がある
            //WriteNextPacketInternalでは音声キューに追加するだけにして、WriteNextPacketProcessedで対応する
            //ひとまず、ここでは処理せず、次のキューに回す
            return AddAudQueue(pktData, (m_Mux.thread.thAudEncode.joinable()) ? AUD_QUEUE_ENCODE : AUD_QUEUE_OUT);
        }
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        for (uint32_t i = 0; i < m_Mux.audio.size(); i++) {
            AudioFlushStream(&m_Mux.audio[i], &pktData->dts);
        }
        pktData->dts = INT64_MAX;
        AddMessage(QSV_LOG_DEBUG, _T("Flushed audio buffer.\n"));
        return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
    }

    if (((int16_t)(pktData->pkt.flags >> 16)) < 0) {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        if (m_Mux.thread.thAudProcess.joinable()) {
            //音声処理を別スレッドでやっている場合は、字幕パケットもその流れに乗せてやる必要がある
            //ひとまず、ここでは処理せず、次のキューに回す
            return AddAudQueue(pktData, (m_Mux.thread.thAudEncode.joinable()) ? AUD_QUEUE_ENCODE : AUD_QUEUE_OUT);
        }
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        return SubtitleWritePacket(&pktData->pkt);
    }
    return WriteNextPacketAudio(pktData);
}

//音声処理スレッドが存在する場合、この関数は音声処理スレッドによって処理される
//音声処理スレッドがなく、出力スレッドがあれば、出力スレッドにより処理される
//出力スレッドがなければメインエンコードスレッドが処理する
mfxStatus CAvcodecWriter::WriteNextPacketAudio(AVPktMuxData *pktData) {
    pktData->samples = 0;
    AVMuxAudio *pMuxAudio = pktData->pMuxAudio;
    if (pMuxAudio == NULL) {
        AddMessage(QSV_LOG_ERROR, _T("failed to get stream for input stream.\n"));
        m_Mux.format.bStreamError = true;
        av_packet_unref(&pktData->pkt);
        return MFX_ERR_NULL_PTR;
    }

    //AACBsfでのエラーを無音挿入で回避する(音声エンコード時のみ)
    bool bSetSilenceDueToAACBsfError = false;
    AVRational samplerate = { 1, pMuxAudio->pCodecCtxIn->sample_rate };
    //このパケットのサンプル数
    const int nSamples = (int)av_rescale_q(pktData->pkt.duration, pMuxAudio->pCodecCtxIn->pkt_timebase, samplerate);
    if (pMuxAudio->pAACBsfc) {
        auto sts = applyBitstreamFilterAAC(&pktData->pkt, pMuxAudio);
        //bitstream filterを正常に起動できなかった
        if (sts > 0) {
            m_Mux.format.bStreamError = true;
            return MFX_ERR_UNDEFINED_BEHAVIOR;
        }
        //pktData->pkt.duration == 0 の場合はなにもせず終了する
        if (pktData->pkt.duration == 0) {
            av_packet_unref(&pktData->pkt);
            //特にエラーでなければそのまま終了
            if (sts == 0) {
                return MFX_ERR_NONE;
            }
            //先頭でエラーが出た場合は音声のDelayを増やすことで同期を保つ
            if (pMuxAudio->nPacketWritten == 0) {
                pMuxAudio->nDelaySamplesOfAudio += nSamples;
                return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
            }
            //音声エンコードしない場合はどうしようもないので終了
            if (!pMuxAudio->pOutCodecDecodeCtx || m_Mux.format.bStreamError) {
                return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
            }
            //音声エンコードの場合は無音挿入で時間をかせぐ
            bSetSilenceDueToAACBsfError = true;
        }
    }
    pMuxAudio->nPacketWritten++;
    auto writeOrSetNextPacketAudioProcessed = [this](AVPktMuxData *pktData) {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        if (m_Mux.thread.thAudProcess.joinable()) {
            //ひとまず、ここでは処理せず、次のキューに回す
            AddAudQueue(pktData, (m_Mux.thread.thAudEncode.joinable()) ? AUD_QUEUE_ENCODE : AUD_QUEUE_OUT);
        } else {
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
            WriteNextPacketProcessed(pktData);
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        }
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    };
    if (!pMuxAudio->pOutCodecDecodeCtx) {
        pktData->samples = (int)av_rescale_q(pktData->pkt.duration, pMuxAudio->pCodecCtxIn->pkt_timebase, samplerate);
        // 1/1000 timebaseは信じるに値しないので、frame_sizeがあればその値を使用する
        if (0 == av_cmp_q(pMuxAudio->pCodecCtxIn->pkt_timebase, { 1, 1000 })
            && pMuxAudio->pCodecCtxIn->frame_size) {
            pktData->samples = pMuxAudio->pCodecCtxIn->frame_size;
        } else {
            //このdurationから計算したsampleが信頼できるか計算する
            //mkvではたまにptsの差分とdurationが一致しないことがある
            //ptsDiffが動画の1フレーム分より小さいときのみ対象とする (カット編集によるものを混同する可能性がある)
            mfxI64 ptsDiff = pktData->pkt.pts - pMuxAudio->nLastPtsIn;
            if (0 < ptsDiff
                && ptsDiff < av_rescale_q(1, av_inv_q(m_Mux.video.nFPS), samplerate)
                && pMuxAudio->nLastPtsIn != AV_NOPTS_VALUE
                && 1 < std::abs(ptsDiff - pktData->pkt.duration)) {
                //ptsの差分から計算しなおす
                pktData->samples = (int)av_rescale_q(ptsDiff, pMuxAudio->pCodecCtxIn->pkt_timebase, samplerate);
            }
        }
        pMuxAudio->nLastPtsIn = pktData->pkt.pts;
        writeOrSetNextPacketAudioProcessed(pktData);
    } else if (!(pMuxAudio->nDecodeError > pMuxAudio->nIgnoreDecodeError) && !pMuxAudio->bEncodeError) {
        AVFrame *decodedFrame = nullptr;
        if (bSetSilenceDueToAACBsfError) {
            //無音挿入
            decodedFrame = av_frame_alloc();
            decodedFrame                 = av_frame_alloc();
            decodedFrame->nb_samples     = nSamples;
            decodedFrame->channels       = pMuxAudio->nResamplerInChannels;
            decodedFrame->channel_layout = pMuxAudio->nResamplerInChannelLayout;
            decodedFrame->sample_rate    = pMuxAudio->nResamplerInSampleRate;
            decodedFrame->format         = pMuxAudio->ResamplerInSampleFmt;
            av_frame_get_buffer(decodedFrame, 32); //format, channel_layout, nb_samplesを埋めて、av_frame_get_buffer()により、メモリを確保する
            av_samples_set_silence((uint8_t **)decodedFrame->data, 0, decodedFrame->nb_samples, decodedFrame->channels, (AVSampleFormat)decodedFrame->format);
        } else {
            decodedFrame = AudioDecodePacket(pMuxAudio, &pktData->pkt, &pktData->got_result);
            if (pktData->pkt.data != nullptr) {
                av_packet_unref(&pktData->pkt);
            }
        }
        pktData->type = MUX_DATA_TYPE_FRAME;
        pktData->pFrame = decodedFrame;

        //フィルタリングを行う
        if (pktData->got_result) {
            auto sts = AudioFilterFrame(pktData);
            if (sts != MFX_ERR_NONE) return sts;
        }
        WriteNextPacketToAudioSubtracks(pktData);
    }

    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

//フィルタリング後のパケットをサブトラックに分配する
mfxStatus CAvcodecWriter::WriteNextPacketToAudioSubtracks(AVPktMuxData *pktData) {
    //サブストリームが存在すれば、frameをコピーしてそれぞれに渡す
    AVMuxAudio *pMuxAudioSubStream = nullptr;
    for (int iSubStream = 1; nullptr != (pMuxAudioSubStream = getAudioStreamData(pktData->pMuxAudio->nInTrackId, iSubStream)); iSubStream++) {
        auto pktDataCopy = *pktData;
        pktDataCopy.pMuxAudio = pMuxAudioSubStream;
        pktDataCopy.pFrame = (pktData->pFrame) ? av_frame_clone(pktData->pFrame) : nullptr;
        WriteNextPacketAudioFrame(&pktDataCopy);
    }
    //親ストリームはここで処理
    return WriteNextPacketAudioFrame(pktData);
}

//フレームをresampleして後段に渡す
mfxStatus CAvcodecWriter::WriteNextPacketAudioFrame(AVPktMuxData *pktData) {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    const bool bAudEncThread = m_Mux.thread.thAudEncode.joinable();
#else
    const bool bAudEncThread = false;
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    AVMuxAudio *pMuxAudio = pktData->pMuxAudio;
    if (pktData->got_result) {
        if (0 <= AudioResampleFrame(pMuxAudio, &pktData->pFrame) && pktData->pFrame) {
            const int bytes_per_sample = av_get_bytes_per_sample(pMuxAudio->pOutCodecEncodeCtx->sample_fmt)
                * (av_sample_fmt_is_planar(pMuxAudio->pOutCodecEncodeCtx->sample_fmt) ? 1 : pMuxAudio->pOutCodecEncodeCtx->channels);
            const int channel_loop_count = av_sample_fmt_is_planar(pMuxAudio->pOutCodecEncodeCtx->sample_fmt) ? pMuxAudio->pOutCodecEncodeCtx->channels : 1;
            if (pMuxAudio->pDecodedFrameCache == nullptr && (pktData->pFrame->nb_samples == pMuxAudio->pOutCodecEncodeCtx->frame_size || pMuxAudio->pOutCodecEncodeCtx->frame_size == 0)) {
                //デコードの出力サンプル数とエンコーダのframe_sizeが一致していれば、そのままエンコードする
                if (bAudEncThread) {
                    //エンコードスレッド使用時はFrameをコピーして渡す
                    AVFrame *pCutFrame = av_frame_alloc();
                    pCutFrame->format = pMuxAudio->pOutCodecEncodeCtx->sample_fmt;
                    pCutFrame->channel_layout = pMuxAudio->pOutCodecEncodeCtx->channel_layout;
                    pCutFrame->nb_samples = pMuxAudio->pOutCodecEncodeCtx->frame_size;
                    av_frame_get_buffer(pCutFrame, 32); //format, channel_layout, nb_samplesを埋めて、av_frame_get_buffer()により、メモリを確保する
                    for (int i = 0; i < channel_loop_count; i++) {
                        memcpy(pCutFrame->data[i], pktData->pFrame->data[i], pCutFrame->nb_samples * bytes_per_sample);
                    }
                    AVPktMuxData pktDataCopy = *pktData;
                    pktDataCopy.pFrame = pCutFrame;
                    AddAudQueue(&pktDataCopy, AUD_QUEUE_ENCODE);
                } else {
                    WriteNextAudioFrame(pktData);
                }
            } else {
                //それまでにたまっているキャッシュがあれば、それを結合する
                if (pMuxAudio->pDecodedFrameCache) {
                    //pMuxAudio->pDecodedFrameCacheとdecodedFrameを結合
                    AVFrame *pCombinedFrame = av_frame_alloc();
                    pCombinedFrame->format = pMuxAudio->pOutCodecEncodeCtx->sample_fmt;
                    pCombinedFrame->channel_layout = pMuxAudio->pOutCodecEncodeCtx->channel_layout;
                    pCombinedFrame->nb_samples = pktData->pFrame->nb_samples + pMuxAudio->pDecodedFrameCache->nb_samples;
                    av_frame_get_buffer(pCombinedFrame, 32); //format, channel_layout, nb_samplesを埋めて、av_frame_get_buffer()により、メモリを確保する
                    for (int i = 0; i < channel_loop_count; i++) {
                        mfxU32 cachedBytes = pMuxAudio->pDecodedFrameCache->nb_samples * bytes_per_sample;
                        memcpy(pCombinedFrame->data[i], pMuxAudio->pDecodedFrameCache->data[i], cachedBytes);
                        memcpy(pCombinedFrame->data[i] + cachedBytes, pktData->pFrame->data[i], pktData->pFrame->nb_samples * bytes_per_sample);
                    }
                    //結合し終わっていらないものは破棄
                    av_frame_free(&pMuxAudio->pDecodedFrameCache);
                    av_frame_free(&pktData->pFrame);
                    pktData->pFrame = pCombinedFrame;
                }

                int samplesRemain = pktData->pFrame->nb_samples; //残りのサンプル数
                int samplesWritten = 0; //エンコーダに渡したサンプル数
                                        //残りサンプル数がframe_size未満になるまで回す
                for (; samplesRemain >= pMuxAudio->pOutCodecEncodeCtx->frame_size;
                samplesWritten += pMuxAudio->pOutCodecEncodeCtx->frame_size, samplesRemain -= pMuxAudio->pOutCodecEncodeCtx->frame_size) {
                    //frameにエンコーダのframe_size分だけ切り出しながら、エンコードを進める
                    AVFrame *pCutFrame = av_frame_alloc();
                    pCutFrame->format = pMuxAudio->pOutCodecEncodeCtx->sample_fmt;
                    pCutFrame->channel_layout = pMuxAudio->pOutCodecEncodeCtx->channel_layout;
                    pCutFrame->nb_samples = pMuxAudio->pOutCodecEncodeCtx->frame_size;
                    av_frame_get_buffer(pCutFrame, 32); //format, channel_layout, nb_samplesを埋めて、av_frame_get_buffer()により、メモリを確保する
                    for (int i = 0; i < channel_loop_count; i++) {
                        memcpy(pCutFrame->data[i], pktData->pFrame->data[i] + samplesWritten * bytes_per_sample, pCutFrame->nb_samples * bytes_per_sample);
                    }
                    AVPktMuxData pktDataPartial = *pktData;
                    pktDataPartial.type = MUX_DATA_TYPE_FRAME;
                    pktDataPartial.pFrame = pCutFrame;
                    bAudEncThread ? AddAudQueue(&pktDataPartial, AUD_QUEUE_ENCODE) : WriteNextAudioFrame(&pktDataPartial);
                }
                if (samplesRemain) {
                    pktData->pFrame->nb_samples = samplesRemain;
                    for (int i = 0; i < channel_loop_count; i++) {
                        memcpy(pktData->pFrame->data[i], pktData->pFrame->data[i] + samplesWritten * bytes_per_sample, pktData->pFrame->nb_samples * bytes_per_sample);
                    }
                    std::swap(pMuxAudio->pDecodedFrameCache, pktData->pFrame);
                }
            }
        }
    }
    if (pktData->pFrame != nullptr) {
        av_frame_free(&pktData->pFrame);
    }
    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

//音声フレームをエンコード
//音声エンコードスレッドが存在する場合、この関数は音声エンコードスレッドによって処理される
//音声エンコードスレッドが存在せず、音声処理スレッドが存在する場合、この関数は音声処理スレッドによって処理される
//音声処理スレッドが存在しない場合、この関数は出力スレッドによって処理される
//出力スレッドがなければメインエンコードスレッドが処理する
mfxStatus CAvcodecWriter::WriteNextAudioFrame(AVPktMuxData *pktData) {
    if (pktData->type != MUX_DATA_TYPE_FRAME) {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        if (m_Mux.thread.thAudEncode.joinable()) {
            //音声エンコードスレッドがこの関数を処理
            //AVPacketは字幕やnull終端パケットなどが流れてきたもの
            //これはそのまま出力キューに追加する
            AddAudQueue(pktData, AUD_QUEUE_OUT);
        }
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        //音声エンコードスレッドが存在しない場合、ここにAVPacketは流れてこないはず
        return MFX_ERR_UNSUPPORTED;
    }
    int got_result = 0;
    pktData->samples = AudioEncodeFrame(pktData->pMuxAudio, &pktData->pkt, pktData->pFrame, &got_result);
    av_frame_free(&pktData->pFrame);
    pktData->type = MUX_DATA_TYPE_PACKET;
    if (got_result && pktData->samples) {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        if (m_Mux.thread.thAudProcess.joinable()) {
            AddAudQueue(pktData, AUD_QUEUE_OUT);
        } else {
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
            WriteNextPacketProcessed(pktData);
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
        }
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    }
    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::ThreadFuncAudEncodeThread() {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    WaitForSingleObject(m_Mux.thread.heEventPktAddedAudEncode, INFINITE);
    while (!m_Mux.thread.bThAudEncodeAbort) {
        if (!m_Mux.format.bFileHeaderWritten) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            AVPktMuxData pktData = { 0 };
            while (m_Mux.thread.qAudioFrameEncode.front_copy_and_pop_no_lock(&pktData, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_aud_enc : nullptr)) {
                //音声エンコードを実行、出力キューに追加する
                WriteNextAudioFrame(&pktData);
            }
        }
        ResetEvent(m_Mux.thread.heEventPktAddedAudEncode);
        WaitForSingleObject(m_Mux.thread.heEventPktAddedAudEncode, 16);
    }
    {   //音声をすべてエンコード
        AVPktMuxData pktData = { 0 };
        while (m_Mux.thread.qAudioFrameEncode.front_copy_and_pop_no_lock(&pktData, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_aud_enc : nullptr)) {
            WriteNextAudioFrame(&pktData);
        }
    }
    SetEvent(m_Mux.thread.heEventClosingAudEncode);
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::ThreadFuncAudThread() {
#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    WaitForSingleObject(m_Mux.thread.heEventPktAddedAudProcess, INFINITE);
    while (!m_Mux.thread.bThAudProcessAbort) {
        if (!m_Mux.format.bFileHeaderWritten) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            AVPktMuxData pktData = { 0 };
            while (m_Mux.thread.qAudioPacketProcess.front_copy_and_pop_no_lock(&pktData, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_aud_proc : nullptr)) {
                //音声処理を実行、出力キューに追加する
                WriteNextPacketInternal(&pktData);
            }
        }
        ResetEvent(m_Mux.thread.heEventPktAddedAudProcess);
        WaitForSingleObject(m_Mux.thread.heEventPktAddedAudProcess, 16);
    }
    {   //音声をすべて書き出す
        AVPktMuxData pktData = { 0 };
        while (m_Mux.thread.qAudioPacketProcess.front_copy_and_pop_no_lock(&pktData, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_aud_proc : nullptr)) {
            //音声処理を実行、出力キューに追加する
            WriteNextPacketInternal(&pktData);
        }
    }
    SetEvent(m_Mux.thread.heEventClosingAudProcess);
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

mfxStatus CAvcodecWriter::WriteThreadFunc() {
#if ENABLE_AVCODEC_OUT_THREAD
    //映像と音声の同期をとる際に、それをあきらめるまでの閾値
    const int nWaitThreshold = 32;
    const size_t videoPacketThreshold = std::min<size_t>(3072, m_Mux.thread.qVideobitstream.capacity()) - nWaitThreshold;
    const size_t audioPacketThreshold = std::min<size_t>(6144, m_Mux.thread.qAudioPacketOut.capacity()) - nWaitThreshold;
    //現在のdts、"-1"は無視することを映像と音声の同期を行う必要がないことを意味する
    int64_t audioDts = (m_Mux.audio.size()) ? -1 : INT64_MAX;
    int64_t videoDts = (m_Mux.video.pCodecCtx) ? -1 : INT64_MAX;
    //キューにデータが存在するか
    bool bAudioExists = false;
    bool bVideoExists = false;
    const auto fpsTimebase = av_inv_q(m_Mux.video.nFPS);
    const auto dtsThreshold = std::max<int64_t>(av_rescale_q(4, fpsTimebase, QSV_NATIVE_TIMEBASE), QSV_TIMEBASE / 4);
    WaitForSingleObject(m_Mux.thread.heEventPktAddedOutput, INFINITE);
    //bThAudProcessは出力開始した後で取得する(この前だとまだ起動していないことがある)
    const bool bThAudProcess = m_Mux.thread.thAudProcess.joinable();
    auto writeProcessedPacket = [this](AVPktMuxData *pktData) {
        //音声処理スレッドが別にあるなら、出力スレッドがすべきことは単に出力するだけ
        auto sts = MFX_ERR_NONE;
        if (((int16_t)(pktData->pkt.flags >> 16)) < 0) {
            sts = SubtitleWritePacket(&pktData->pkt);
        } else {
            WriteNextPacketProcessed(pktData);
        }
        return sts;
    };
    int audPacketsPerSec = 64;
    int nWaitAudio = 0;
    int nWaitVideo = 0;
    while (!m_Mux.thread.bAbortOutput) {
        do {
            if (!m_Mux.format.bFileHeaderWritten) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                //ヘッダー取得前に音声キューのサイズが足りず、エンコードが進まなくなってしまうことがある
                //キューのcapcityを増やすことでこれを回避する
                auto type = (m_Mux.thread.thAudEncode.joinable()) ? AUD_QUEUE_ENCODE : AUD_QUEUE_OUT;
                auto& qAudio = (type == AUD_QUEUE_OUT) ? m_Mux.thread.qAudioPacketOut : m_Mux.thread.qAudioFrameEncode;
                const auto nQueueCapacity = qAudio.capacity();
                if (qAudio.size() >= nQueueCapacity) {
                    qAudio.set_capacity(nQueueCapacity * 3 / 2);
                }
                break;
            }
            //映像・音声の同期待ちが必要な場合、falseとなってループから抜けるよう、ここでfalseに設定する
            bAudioExists = false;
            bVideoExists = false;
            AVPktMuxData pktData = { 0 };
            while ((videoDts < 0 || audioDts <= videoDts + dtsThreshold)
                && false != (bAudioExists = m_Mux.thread.qAudioPacketOut.front_copy_and_pop_no_lock(&pktData, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_aud_out : nullptr))) {
                if (pktData.pMuxAudio && pktData.pMuxAudio->pCodecCtxIn) {
                    audPacketsPerSec = std::max(audPacketsPerSec, (int)(1.0 / (av_q2d(pktData.pMuxAudio->pCodecCtxIn->pkt_timebase) * pktData.pkt.duration) + 0.5));
                    if ((int)m_Mux.thread.qAudioPacketOut.capacity() < audPacketsPerSec * 4) {
                        m_Mux.thread.qAudioPacketOut.set_capacity(audPacketsPerSec * 4);
                    }
                }
                //音声処理スレッドが別にあるなら、出力スレッドがすべきことは単に出力するだけ
                (bThAudProcess) ? writeProcessedPacket(&pktData) : WriteNextPacketInternal(&pktData);
                audioDts = (std::max)(audioDts, pktData.dts);
                nWaitAudio = 0;
            }
            mfxBitstream bitstream = { 0 };
            while ((audioDts < 0 || videoDts <= audioDts + dtsThreshold)
                && false != (bVideoExists = m_Mux.thread.qVideobitstream.front_copy_and_pop_no_lock(&bitstream, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_vid_out : nullptr))) {
                WriteNextFrameInternal(&bitstream, &videoDts);
                nWaitVideo = 0;
            }
            //一定以上の動画フレームがキューにたまっており、音声キューになにもなければ、
            //音声を無視して動画フレームの処理を開始させる
            //音声が途中までしかなかったり、途中からしかなかったりする場合にこうした処理が必要
            if (m_Mux.thread.qAudioPacketOut.size() == 0 && m_Mux.thread.qVideobitstream.size() > videoPacketThreshold) {
                nWaitAudio++;
                if (nWaitAudio <= nWaitThreshold) {
                    //時折まだパケットが来ているのにタイミングによってsize() == 0が成立することがある
                    //なのである程度連続でパケットが来ていないときのみ無視するようにする
                    //このようにすることで適切に同期がとれる
                    break;
                }
                audioDts = -1;
            }
            //一定以上の音声フレームがキューにたまっており、動画キューになにもなければ、
            //動画を無視して音声フレームの処理を開始させる
            if (m_Mux.thread.qVideobitstream.size() == 0 && m_Mux.thread.qAudioPacketOut.size() > audioPacketThreshold) {
                nWaitVideo++;
                if (nWaitVideo <= nWaitThreshold) {
                    //時折まだパケットが来ているのにタイミングによってsize() == 0が成立することがある
                    //なのである程度連続でパケットが来ていないときのみ無視するようにする
                    //このようにすることで適切に同期がとれる
                    break;
                }
                videoDts = -1;
            }
        } while (bAudioExists || bVideoExists); //両方のキューがひとまず空になるか、映像・音声の同期待ちが必要になるまで回す
                                                //次のフレーム・パケットが送られてくるまで待機する
        //キューの容量が両方とも半分以下なら、すこし待ってみる
        //一方、どちらかのキューが半分以上使われていれば、なるべく早く処理する必要がある
        if (   m_Mux.thread.qVideobitstream.size() / (double)m_Mux.thread.qVideobitstream.capacity() < 0.5
            && m_Mux.thread.qAudioPacketOut.size() / (double)m_Mux.thread.qAudioPacketOut.capacity() < 0.5) {
            ResetEvent(m_Mux.thread.heEventPktAddedOutput);
            WaitForSingleObject(m_Mux.thread.heEventPktAddedOutput, 16);
        } else {
            std::this_thread::yield();
        }
    }
    //メインループを抜けたことを通知する
    SetEvent(m_Mux.thread.heEventClosingOutput);
    m_Mux.thread.qAudioPacketOut.set_keep_length(0);
    m_Mux.thread.qVideobitstream.set_keep_length(0);
    bAudioExists = !m_Mux.thread.qAudioPacketOut.empty();
    bVideoExists = !m_Mux.thread.qVideobitstream.empty();
    //まずは映像と音声の同期をとって出力するためのループ
    while (bAudioExists && bVideoExists) {
        AVPktMuxData pktData = { 0 };
        while (audioDts <= videoDts + dtsThreshold
            && false != (bAudioExists = m_Mux.thread.qAudioPacketOut.front_copy_and_pop_no_lock(&pktData, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_aud_out : nullptr))) {
            //音声処理スレッドが別にあるなら、出力スレッドがすべきことは単に出力するだけ
            (bThAudProcess) ? writeProcessedPacket(&pktData) : WriteNextPacketInternal(&pktData);
            audioDts = (std::max)(audioDts, pktData.dts);
        }
        mfxBitstream bitstream = { 0 };
        while (videoDts <= audioDts + dtsThreshold
            && false != (bVideoExists = m_Mux.thread.qVideobitstream.front_copy_and_pop_no_lock(&bitstream, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_vid_out : nullptr))) {
            WriteNextFrameInternal(&bitstream, &videoDts);
        }
        bAudioExists = !m_Mux.thread.qAudioPacketOut.empty();
        bVideoExists = !m_Mux.thread.qVideobitstream.empty();
    }
    { //音声を書き出す
        AVPktMuxData pktData = { 0 };
        while (m_Mux.thread.qAudioPacketOut.front_copy_and_pop_no_lock(&pktData, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_aud_out : nullptr)) {
            //音声処理スレッドが別にあるなら、出力スレッドがすべきことは単に出力するだけ
            (bThAudProcess) ? writeProcessedPacket(&pktData) : WriteNextPacketInternal(&pktData);
        }
    }
    { //動画を書き出す
        mfxBitstream bitstream = { 0 };
        while (m_Mux.thread.qVideobitstream.front_copy_and_pop_no_lock(&bitstream, (m_Mux.thread.pQueueInfo) ? &m_Mux.thread.pQueueInfo->usage_vid_out : nullptr)) {
            WriteNextFrameInternal(&bitstream, &videoDts);
        }
    }
#endif
    return (m_Mux.format.bStreamError) ? MFX_ERR_UNKNOWN : MFX_ERR_NONE;
}

void CAvcodecWriter::WaitFin() {
    CloseThread();
}

HANDLE CAvcodecWriter::getThreadHandleOutput() {
#if ENABLE_AVCODEC_OUT_THREAD
    return (HANDLE)m_Mux.thread.thOutput.native_handle();
#else
    return NULL;
#endif
}

HANDLE CAvcodecWriter::getThreadHandleAudProcess() {
#if ENABLE_AVCODEC_OUT_THREAD && ENABLE_AVCODEC_AUDPROCESS_THREAD
    return (HANDLE)m_Mux.thread.thAudProcess.native_handle();
#else
    return NULL;
#endif
}

HANDLE CAvcodecWriter::getThreadHandleAudEncode() {
#if ENABLE_AVCODEC_OUT_THREAD && ENABLE_AVCODEC_AUDPROCESS_THREAD
    return (HANDLE)m_Mux.thread.thAudEncode.native_handle();
#else
    return NULL;
#endif
}

#if USE_CUSTOM_IO
int CAvcodecWriter::readPacket(uint8_t *buf, int buf_size) {
    return (int)fread(buf, 1, buf_size, m_Mux.format.fpOutput);
}
int CAvcodecWriter::writePacket(uint8_t *buf, int buf_size) {
    return (int)fwrite(buf, 1, buf_size, m_Mux.format.fpOutput);
}
int64_t CAvcodecWriter::seek(int64_t offset, int whence) {
    return _fseeki64(m_Mux.format.fpOutput, offset, whence);
}
#endif //USE_CUSTOM_IO

#endif //ENABLE_AVCODEC_QSV_READER
