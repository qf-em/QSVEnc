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

#ifndef _VPY_READER_H_
#define _VPY_READER_H_

#include "qsv_version.h"
#if ENABLE_VAPOURSYNTH_READER
#include "qsv_osdep.h"
#include "qsv_input.h"
#include "VapourSynth.h"
#include "VSScript.h"

const int ASYNC_BUFFER_2N = 7;
const int ASYNC_BUFFER_SIZE = 1<<ASYNC_BUFFER_2N;

#if _M_IX86
#define VPY_X64 0
#else
#define VPY_X64 1
#endif

//インポートライブラリを使うとvsscript.dllがない場合に起動できない
typedef int (__stdcall *func_vs_init)(void);
typedef int (__stdcall *func_vs_finalize)(void);
typedef int (__stdcall *func_vs_evaluateScript)(VSScript **handle, const char *script, const char *errorFilename, int flags);
typedef int (__stdcall *func_vs_evaluateFile)(VSScript **handle, const char *scriptFilename, int flags);
typedef void (__stdcall *func_vs_freeScript)(VSScript *handle);
typedef const char * (__stdcall *func_vs_getError)(VSScript *handle);
typedef VSNodeRef * (__stdcall *func_vs_getOutput)(VSScript *handle, int index);
typedef void (__stdcall *func_vs_clearOutput)(VSScript *handle, int index);
typedef VSCore * (__stdcall *func_vs_getCore)(VSScript *handle);
typedef const VSAPI * (__stdcall *func_vs_getVSApi)(void);

typedef struct {
    HMODULE                hVSScriptDLL;
    func_vs_init           init;
    func_vs_finalize       finalize;
    func_vs_evaluateScript evaluateScript;
    func_vs_evaluateFile   evaluateFile;
    func_vs_freeScript     freeScript;
    func_vs_getError       getError;
    func_vs_getOutput      getOutput;
    func_vs_clearOutput    clearOutput;
    func_vs_getCore        getCore;
    func_vs_getVSApi       getVSApi;
} vsscript_t;


typedef struct VSReaderPrm {
    bool use_mt;
} VSReaderPrm;

class CVSReader : public CQSVInput
{
public:
    CVSReader();
    virtual ~CVSReader();

    virtual mfxStatus Init(const TCHAR *strFileName, mfxU32 ColorFormat, const void *option, CEncodingThread *pEncThread, shared_ptr<CEncodeStatusInfo> pEncSatusInfo, sInputCrop *pInputCrop) override;

    virtual void Close();
    virtual mfxStatus LoadNextFrame(mfxFrameSurface1* pSurface) override;

    void setFrameToAsyncBuffer(int n, const VSFrameRef* f);
private:
    void release_vapoursynth();
    int load_vapoursynth();
    int initAsyncEvents();
    void closeAsyncEvents();
    const VSFrameRef* getFrameFromAsyncBuffer(int n) {
        WaitForSingleObject(m_hAsyncEventFrameSetFin[n & (ASYNC_BUFFER_SIZE-1)], INFINITE);
        const VSFrameRef *frame = m_pAsyncBuffer[n & (ASYNC_BUFFER_SIZE-1)];
        SetEvent(m_hAsyncEventFrameSetStart[n & (ASYNC_BUFFER_SIZE-1)]);
        return frame;
    }
    const VSFrameRef* m_pAsyncBuffer[ASYNC_BUFFER_SIZE];
    HANDLE m_hAsyncEventFrameSetFin[ASYNC_BUFFER_SIZE];
    HANDLE m_hAsyncEventFrameSetStart[ASYNC_BUFFER_SIZE];

    int getRevInfo(const char *vs_version_string);

    bool m_bAbortAsync;
    mfxU32 m_nCopyOfInputFrames;

    const VSAPI *m_sVSapi;
    VSScript *m_sVSscript;
    VSNodeRef *m_sVSnode;
    int m_nAsyncFrames;

    vsscript_t m_sVS;
};

#endif //ENABLE_VAPOURSYNTH_READER

#endif //_VPY_READER_H_
