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

struct MediaFrame {
    AVFrame* frame;
    AVMediaType type;  // AVMEDIA_TYPE_VIDEO / AVMEDIA_TYPE_AUDIO
    int64_t pts;
    int64_t dts;
};

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

    AVFormatContext* in_fmt_ctx = nullptr;//全局唯一流信息
    int m_video_index = -1;
    int m_audio_index = -1;
    int64_t m_first_video_dts = AV_NOPTS_VALUE;
    int64_t m_first_audio_dts = AV_NOPTS_VALUE;

    bool m_bTransCoderState = false; //主线程开关
    
    SafeQueue<MediaFrame> m_frame_queue{30};   // 主解码队列 1路流

    // 解码 → 编码的分发队列（一个编码器一个队列）//转发三个编码格式 3个队列
    std::vector<SafeQueue<MediaFrame>> m_veFrame_queue_transfer;

    std::thread m_threadReadFrameDecode;  //拉流解码线程
    std::vector<std::thread> m_veThreadEncodePushStream;     //编码推流线程
    OutPutStreamInfo m_stOutPutStreamInfo;

    //打开流 
    bool InitInput(AVFormatContext*& in_fmt_ctx, const std::string& input_url, int timeout_ms = 5000);
    
    //打开输出
    bool InitOutput(AVFormatContext*& out_fmt_ctx,AVFormatContext* in_fmt_ctx,const std::string& output_url);

    //初始化视频编码器
    bool InitVideoEncoder(AVFormatContext* out_fmt_ctx,
                        AVStream*& out_video_stream,
                        AVCodecContext*& video_enc_ctx,
                        const std::string& strUrl,
                        int width = 1280,
                        int height = 720,
                        int bitrate = 1000000,
                        int fps = 25,
                        const std::string& preset = "fast",
                        const std::string& tune = "zerolatency");

    //初始化音频编码器
    bool InitAudioEncoder(AVFormatContext* in_fmt_ctx,
                                       AVFormatContext* out_fmt_ctx,
                                       AVCodecContext*& audio_enc_ctx,
                                       AVStream*& out_audio_stream,
                                       SwrContext*& swr_ctx,
                                       AVFrame*& audio_frame,
                                       int m_audio_index,
                                       const std::string& strUrl);
     //拉流+解码+编码+推流
    bool push_stream(const std::string& input_url, const std::string& output_url);

    //拉流+解码
    void OperationStream(const std::string&  strInputStream);

    //解码数据中转 转发多路进行编码
    void DecodeFrameTransfer();

    //编码+推流
    void EncodeFramePushStream(int nIndex,const std::string& strUrl);

};