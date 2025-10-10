#pragma once
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "common.h"


extern "C" {
    #include "../3rdparty/ffmpeg/include/libavformat/avformat.h"
    #include "../3rdparty/ffmpeg/include/libavcodec/avcodec.h"
    #include "../3rdparty/ffmpeg/include/libavutil/imgutils.h"
    #include "../3rdparty/ffmpeg/include/libavutil/opt.h"
    #include "../3rdparty/ffmpeg/include/libavutil/channel_layout.h"
    #include "../3rdparty/ffmpeg/include/libavutil/time.h"
    #include "../3rdparty/ffmpeg/include/libswscale/swscale.h"
    #include "../3rdparty/ffmpeg/include/libswresample/swresample.h"
    #include "../3rdparty/ffmpeg/include/libavfilter/buffersink.h"
    #include "../3rdparty/ffmpeg/include/libavfilter/buffersrc.h"
    #include "../3rdparty/ffmpeg/include/libavcodec/bsf.h"
}

struct EncodeTask {
    int width;
    int height;
    AVCodecContext* encCtx;
    AVStream* outStream;
};

class StreamRelay
{

public:
    StreamRelay();
    StreamRelay(const OutPutStreamInfo& info) : m_streamInfo(info) {}
    ~StreamRelay();
    void InitStreamConfig();

    bool Start();

    void Stop();
    
    void Restart();

    void Update(const OutPutStreamInfo& newInfo);
    
    void setStatusCallback(std::function<void(const OutPutStreamInfo&)> cb) 
    {
        m_statusCallback = cb;
    }

    void setFailCallback(std::function<void(const std::string&)> cb) 
    {
        m_failCallback = cb;
    }

private:

    bool m_PushState = false;
    StreamDbDataInfo m_stPushStreamInfoNow; //当前正在拉推的流
    OutPutStreamInfo m_streamInfo;          //所有节目列表
    std::thread m_threadRelay;

    std::function<void(const OutPutStreamInfo&)> m_statusCallback;
    std::function<void(const std::string&)> m_failCallback;

    void OnStatusCallBack(StreamDbDataInfo stStreamPushData);
    void addSqlDbData(const StreamDbDataInfo &stStreamPushData,const StreamDbDataInfo &oldStStreamPushData);

    void OnFailCallBack(std::string stStreamPushData);

    bool tryPlaySource(StreamDbDataInfo& src, int nRetry);
    //打开流 
    bool open_input(AVFormatContext*& in_fmt_ctx, const std::string& input_url, int timeout_ms = 5000);
    
    //打开输出
    bool reset_output(AVFormatContext*& out_fmt_ctx,AVFormatContext* in_fmt_ctx,const std::string& output_url);
    
    //测试推流
    bool push_stream(const std::string& input_url, const std::string& output_url);
    
    //只复制不转码推流
    StreamError relay_stream(const std::string& input_url, const std::string& output_url);

    void encodeVideoThread(AVCodecContext* encCtx, AVStream* outStream, AVFormatContext* outFmtCtx);

    void InitStream();
    void OperationStream(std::string  strInputStream, std::string strOutputStream);
};