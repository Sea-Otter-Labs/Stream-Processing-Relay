#pragma once
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "common.h"
#include "quenu.h"

using json = nlohmann::json;

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

class StreamTranscoder
{

public:
    StreamTranscoder();
    ~StreamTranscoder();
    void InitStreamConfig();

    bool Start();
    void Stop();
    void Update(OutPutStreamInfo stOutPutStreamInfo);

private:
    OutPutStreamInfo m_stOutPutStreamInfo;

    bool m_bTransCoderState = false; //主线程开关

    SafeQueue<AVFrame*> m_frame_queue{30};   // 主解码队列 1路流
    // 解码 → 编码的分发队列（一个编码器一个队列）//转发三个编码格式 3个队列
    std::vector<SafeQueue<AVFrame*>> m_veFrame_queue_transfer;

     // 主编码队列 3路流
    std::vector<SafeQueue<AVPacket*>> m_vePacket_queue;
    // 编码 → 推流的分发队列（一个推流目标一个队列）//三路流分别转发 9个队列
    std::vector<SafeQueue<AVPacket*>> m_vePacket_queue_transfer;

    std::thread m_threadReadFrame;  //拉流解码
    std::vector<std::thread> m_veThreadEncode;     //编码
    std::vector<std::thread> m_veThreadPushStream; //推流
    std::map<int,std::vector<int>> m_mapEncodePushIndex;//编码推流对应map
    
    //打开流 
    bool InitInput(AVFormatContext*& in_fmt_ctx, const std::string& input_url, int timeout_ms = 5000);
    
    //打开输出
    bool InitOutput(AVFormatContext*& out_fmt_ctx,AVFormatContext* in_fmt_ctx,const std::string& output_url);

     //拉流+解码+编码+推流
    bool push_stream(const std::string& input_url, const std::string& output_url);

    //拉流+解码
    void OperationStream(std::string strInputStream);

    //解码数据中转 转发多路进行编码
    void DecodeFrameTransfer();

    //编码
    void EncodeFrame(int nIndex);

    //编码数据中转 转发多路进行推流
    void EncodePacketTransfer(int nIndex);

    //推流
    void PushStream(int nIndex,std::string strUrl);

};