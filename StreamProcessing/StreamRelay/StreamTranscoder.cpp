#include "StreamTranscoder.h"
#include "Logger.h"
#include <regex>
namespace fs = std::filesystem;


StreamTranscoder::StreamTranscoder()
{

}

StreamTranscoder::~StreamTranscoder()
{
    avformat_close_input(&in_fmt_ctx);
}

void StreamTranscoder::InitStreamConfig()
{

}

bool StreamTranscoder::Start()
{
    //编码队列初始化
    for (int i = 0; i < m_stOutPutStreamInfo.stStreamTask.encoders.size() ;i++) 
    {
        m_veFrame_queue_transfer.emplace_back(30);
    }

    m_bTransCoderState = true;

    //统一管理
    while (m_bTransCoderState)
    {
        //进行解码编码
    
        //后续处理 解析任务文件确定转码数量

    }
}

void StreamTranscoder::Stop()
{

}

void StreamTranscoder::Update(OutPutStreamInfo stOutPutStreamInfo)
{
    //更新
    m_stOutPutStreamInfo = stOutPutStreamInfo;
}

bool StreamTranscoder::InitInput(AVFormatContext*& in_fmt_ctx, const std::string& input_url, int timeout_ms)
{
    // 清理旧的输入上下文
    if (in_fmt_ctx) 
    {
        avformat_close_input(&in_fmt_ctx);
        in_fmt_ctx = nullptr;
    }

    // 设置输入参数（比如超时）
    AVDictionary* options = nullptr;

    // 超时（毫秒 -> 微秒）

    // 网络缓存（可选）
    av_dict_set(&options, "buffer_size", "1024000", 0);   // 1MB 缓存
    av_dict_set(&options, "max_delay", "500000", 0);      // 最大 0.5s 延迟
    av_dict_set(&options, "reconnect", "1", 0); // 允许自动重连
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "10", 0); // 最大重连延迟10秒
    av_dict_set(&options, "timeout", "10000000", 0); // 超时

    TimeoutData timeout_data;
    timeout_data.timeout_ms = timeout_ms;
    timeout_data.start_time = std::chrono::steady_clock::now();
    AVIOInterruptCB int_cb = {interrupt_cb, &timeout_data};
    in_fmt_ctx = avformat_alloc_context();
    in_fmt_ctx->interrupt_callback = int_cb;

    // 打开输入
    if (avformat_open_input(&in_fmt_ctx, input_url.c_str(), nullptr, &options) < 0) 
    {
        Logger::getInstance()->error("Cannot open input URL:{}", input_url);
        av_dict_free(&options);
        return false;
    }

    av_dict_free(&options);

    // 读取流信息
    if (avformat_find_stream_info(in_fmt_ctx, nullptr) < 0) 
    {
        Logger::getInstance()->error("Failed to get stream info:{}", input_url);
        avformat_close_input(&in_fmt_ctx);
        return false;
    }
    return true;
}

bool StreamTranscoder::InitOutput(AVFormatContext*& out_fmt_ctx,AVFormatContext* in_fmt_ctx,const std::string& output_url) 
{
    // 清理旧的输出上下文
    if (out_fmt_ctx) 
    {
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE) && out_fmt_ctx->pb) 
        {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
        out_fmt_ctx = nullptr;
    }

    // 重新分配输出上下文
    if (avformat_alloc_output_context2(&out_fmt_ctx, nullptr, "flv", output_url.c_str()) < 0) 
    {
        Logger::getInstance()->error("Cannot create output context url: {}",output_url);
        return false;
    }

    // 给输出加上对应的流（拷贝输入的流参数）
    for (unsigned int i = 0; i < in_fmt_ctx->nb_streams; i++) 
    {
        AVStream* in_stream = in_fmt_ctx->streams[i];
        AVStream* out_stream = avformat_new_stream(out_fmt_ctx, nullptr);
        if (!out_stream) {
            Logger::getInstance()->error("Failed to allocate output stream url: {}",output_url);
            return false;
        }
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) 
        {
            Logger::getInstance()->error("Failed to copy codec parameters url: {}",output_url);
            return false;
        }
        out_stream->codecpar->codec_tag = 0;
    }

    Logger::getInstance()->debug("Output reset OK url: {}",output_url);
    return true;
}

bool StreamTranscoder::push_stream(const std::string& input_url, const std::string& output_url) 
{
    AVFormatContext* in_fmt_ctx = nullptr;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "stimeout", "5000000", 0);    // 5秒超时
    av_dict_set(&options, "buffer_size", "1024000", 0); // 增大缓冲

    TimeoutData timeout_data;
    timeout_data.timeout_ms = 15000;
    timeout_data.start_time = std::chrono::steady_clock::now();
    AVIOInterruptCB int_cb = {interrupt_cb, &timeout_data};
    in_fmt_ctx = avformat_alloc_context();
    //in_fmt_ctx->interrupt_callback = int_cb;

    if (avformat_open_input(&in_fmt_ctx, input_url.c_str(), nullptr, &options) < 0) 
    {
        Logger::getInstance()->error("Cannot open input url: {}",input_url);
        av_dict_free(&options);
        return false;
    }
    
    av_dict_free(&options);

    if (avformat_find_stream_info(in_fmt_ctx, nullptr) < 0) 
    {
        Logger::getInstance()->error("Cannot find stream info url: {}",input_url);
        avformat_close_input(&in_fmt_ctx);
        return false;
    }

    int video_index = -1, audio_index = -1;
    for (unsigned i = 0; i < in_fmt_ctx->nb_streams; i++) 
    {
        if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_index < 0)
            video_index = i;
        if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_index < 0)
            audio_index = i;
    }

    if (video_index < 0) 
    {
        Logger::getInstance()->error("No video stream found url: {}",input_url);
        avformat_close_input(&in_fmt_ctx);
        return false;
    }

    // 输出
    AVFormatContext* out_fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&out_fmt_ctx, nullptr, "flv", output_url.c_str()) < 0) 
    {
        Logger::getInstance()->error("Cannot create output context url: {}",input_url);
        avformat_close_input(&in_fmt_ctx);
        return false;
    }

    // 视频解码器
    const AVCodec* dec = avcodec_find_decoder(in_fmt_ctx->streams[video_index]->codecpar->codec_id);
    if (!dec) 
    {
        Logger::getInstance()->error("Cannot find video decoder url: {}",input_url);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) 
    {
        Logger::getInstance()->error("Cannot allocate video decoder context url: {}",input_url);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    if (avcodec_parameters_to_context(dec_ctx, in_fmt_ctx->streams[video_index]->codecpar) < 0) 
    {
        Logger::getInstance()->error("Cannot copy parameters to video decoder context url: {}",input_url);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    if (avcodec_open2(dec_ctx, dec, nullptr) < 0) 
    {
        Logger::getInstance()->error("Cannot open video decoder url: {}",input_url);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    // 视频编码器
    const AVCodec* video_encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!video_encoder) 
    {
        Logger::getInstance()->error("Cannot find H264 encoder url: {}",input_url);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    AVStream* out_video_stream = avformat_new_stream(out_fmt_ctx, video_encoder);
    if (!out_video_stream) 
    {
        Logger::getInstance()->error("Cannot create output video stream url: {}",input_url);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    AVCodecContext* video_enc_ctx = avcodec_alloc_context3(video_encoder);
    if (!video_enc_ctx) {
        Logger::getInstance()->error("Cannot allocate video encoder context url: {}",input_url);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    video_enc_ctx->width = 1280;
    video_enc_ctx->height = 720;
    video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    video_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    video_enc_ctx->time_base = {1, 25};
    video_enc_ctx->bit_rate = 1000000;
    video_enc_ctx->gop_size = 50;

    av_opt_set(video_enc_ctx->priv_data, "preset", "fast", 0);
    av_opt_set(video_enc_ctx->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(video_enc_ctx, video_encoder, nullptr) < 0) 
    {
        Logger::getInstance()->error("Cannot open video encoder url: {}",input_url);
        avcodec_free_context(&video_enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    if (avcodec_parameters_from_context(out_video_stream->codecpar, video_enc_ctx) < 0) 
    {
        Logger::getInstance()->error("Cannot copy parameters to output video stream url: {}",input_url);
        avcodec_free_context(&video_enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    // 检查extradata是否包含SPS/PPS
    if (video_enc_ctx->extradata && video_enc_ctx->extradata_size > 0) 
    {
        // 确保输出流的extradata也包含这些信息
        out_video_stream->codecpar->extradata = (uint8_t*)av_mallocz(video_enc_ctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (out_video_stream->codecpar->extradata) 
        {
            memcpy(out_video_stream->codecpar->extradata, video_enc_ctx->extradata, video_enc_ctx->extradata_size);
            out_video_stream->codecpar->extradata_size = video_enc_ctx->extradata_size;
        }
    }
    else 
    {
        std::cerr << "警告: SPS/PPS 丢失!" << std::endl;
        return false;
    }

    out_video_stream->codecpar->codec_tag = 0;
    //out_video_stream->time_base = video_enc_ctx->time_base;
    out_video_stream->time_base = av_make_q(1, 1000); // 使用毫秒为单位

    if (!out_video_stream->codecpar->extradata) 
    {
        std::cerr << "警告: SPS/PPS 丢失!" << std::endl;
        return false;
    }

    // 音频编码器（转 AAC）
    AVStream* out_audio_stream = nullptr;
    AVCodecContext* audio_enc_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    AVFrame* audio_frame = nullptr;
    AVCodecContext* audio_dec_ctx = nullptr;

    if (audio_index >= 0) 
    {
        // 音频解码器
        const AVCodec* audio_dec = avcodec_find_decoder(in_fmt_ctx->streams[audio_index]->codecpar->codec_id);
        if (!audio_dec) 
        {
            std::cerr << "Cannot find audio decoder" << std::endl;
        }
        else 
        {
            audio_dec_ctx = avcodec_alloc_context3(audio_dec);
            if (!audio_dec_ctx) 
            {
                std::cerr << "Cannot allocate audio decoder context" << std::endl;
            }
            else if (avcodec_parameters_to_context(audio_dec_ctx, in_fmt_ctx->streams[audio_index]->codecpar) < 0) 
            {
                std::cerr << "Cannot copy parameters to audio decoder context" << std::endl;
                avcodec_free_context(&audio_dec_ctx);
                audio_dec_ctx = nullptr;
            } 
            else if (avcodec_open2(audio_dec_ctx, audio_dec, nullptr) < 0) 
            {
                std::cerr << "Cannot open audio decoder" << std::endl;
                avcodec_free_context(&audio_dec_ctx);
                audio_dec_ctx = nullptr;
            }
        }

        // 音频编码器
        const AVCodec* audio_encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!audio_encoder) 
        {
            std::cerr << "Cannot find AAC encoder" << std::endl;
        } 
        else 
        {
            out_audio_stream = avformat_new_stream(out_fmt_ctx, audio_encoder);
            if (!out_audio_stream) 
            {
                std::cerr << "Cannot create output audio stream" << std::endl;
            } 
            else 
            {
                audio_enc_ctx = avcodec_alloc_context3(audio_encoder);
                if (!audio_enc_ctx) 
                {
                    std::cerr << "Cannot allocate audio encoder context" << std::endl;
                } 
                else 
                {
                    AVCodecParameters* in_audio_par = in_fmt_ctx->streams[audio_index]->codecpar;
                    
                    // 设置音频编码器参数
                    audio_enc_ctx->ch_layout = in_audio_par->ch_layout;
                    audio_enc_ctx->sample_rate = in_audio_par->sample_rate;
                    audio_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
                    audio_enc_ctx->bit_rate = 128000;
                    audio_enc_ctx->time_base = {1, audio_enc_ctx->sample_rate};
                    
                    if (avcodec_open2(audio_enc_ctx, audio_encoder, nullptr) < 0) 
                    {
                        std::cerr << "Cannot open audio encoder" << std::endl;
                        avcodec_free_context(&audio_enc_ctx);
                        audio_enc_ctx = nullptr;
                    } 
                    else 
                    {
                        // 初始化重采样器
                        swr_ctx = swr_alloc();
                        if (!swr_ctx) 
                        {
                            std::cerr << "Cannot allocate resampler" << std::endl;
                        } 
                        else 
                        {
                            int ret = swr_alloc_set_opts2(
                                &swr_ctx,
                                &audio_enc_ctx->ch_layout, audio_enc_ctx->sample_fmt, audio_enc_ctx->sample_rate,
                                &in_audio_par->ch_layout, (AVSampleFormat)in_audio_par->format, in_audio_par->sample_rate,
                                0, nullptr
                            );
                            
                            if (ret < 0 || swr_init(swr_ctx) < 0) 
                            {
                                std::cerr << "Cannot initialize resampler" << std::endl;
                                swr_free(&swr_ctx);
                                swr_ctx = nullptr;
                            }
                            else 
                            {
                                // 分配音频帧
                                audio_frame = av_frame_alloc();
                                if (!audio_frame) 
                                {
                                    std::cerr << "Cannot allocate audio frame" << std::endl;
                                } 
                                else 
                                {
                                    audio_frame->ch_layout = audio_enc_ctx->ch_layout;
                                    audio_frame->format = audio_enc_ctx->sample_fmt;
                                    audio_frame->sample_rate = audio_enc_ctx->sample_rate;
                                    audio_frame->nb_samples = 1024; // 典型值
                                    
                                    if (av_frame_get_buffer(audio_frame, 0) < 0) 
                                    {
                                        std::cerr << "Cannot allocate audio frame buffers" << std::endl;
                                        av_frame_free(&audio_frame);
                                        audio_frame = nullptr;
                                    }
                                }
                            }
                        }
                        
                        // 设置输出流参数
                        if (avcodec_parameters_from_context(out_audio_stream->codecpar, audio_enc_ctx) < 0) {
                            std::cerr << "Cannot copy parameters to output audio stream" << std::endl;
                        }
                        out_audio_stream->codecpar->codec_tag = 0;
                        //out_audio_stream->time_base = audio_enc_ctx->time_base;
                        out_audio_stream->time_base = av_make_q(1, 1000); // 使用毫秒为单位
                    }
                }
            }
        }
    
    }

    // 打开输出 URL
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
    {
        if (avio_open(&out_fmt_ctx->pb, output_url.c_str(), AVIO_FLAG_WRITE) < 0) 
        {
            std::cerr << "Cannot open output URL: " << output_url << std::endl;
            // 清理资源
            if (audio_frame) av_frame_free(&audio_frame);
            if (swr_ctx) swr_free(&swr_ctx);
            if (audio_enc_ctx) avcodec_free_context(&audio_enc_ctx);
            if (audio_dec_ctx) avcodec_free_context(&audio_dec_ctx);
            avcodec_free_context(&video_enc_ctx);
            avcodec_free_context(&dec_ctx);
            avformat_close_input(&in_fmt_ctx);
            avformat_free_context(out_fmt_ctx);
            return false;
        }
    }

    AVDictionary* format_options = nullptr;
    av_dict_set(&format_options, "flvflags", "add_keyframe_index", 0);

    if (avformat_write_header(out_fmt_ctx, &format_options) < 0) 
    {
        std::cerr << "Error writing header" << std::endl;
        // 清理资源
        if (audio_frame) av_frame_free(&audio_frame);
        if (swr_ctx) swr_free(&swr_ctx);
        if (audio_enc_ctx) avcodec_free_context(&audio_enc_ctx);
        if (audio_dec_ctx) avcodec_free_context(&audio_dec_ctx);
        avcodec_free_context(&video_enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
        {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    SwsContext* sws_ctx = sws_getContext(
        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
        video_enc_ctx->width, video_enc_ctx->height, video_enc_ctx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!pkt || !frame || !sws_ctx) 
    {
        std::cerr << "Cannot allocate resources" << std::endl;
        // 清理资源
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (audio_frame) av_frame_free(&audio_frame);
        if (swr_ctx) swr_free(&swr_ctx);
        if (audio_enc_ctx) avcodec_free_context(&audio_enc_ctx);
        if (audio_dec_ctx) avcodec_free_context(&audio_dec_ctx);
        avcodec_free_context(&video_enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
        {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    int64_t video_frame_count = 0;
    int64_t audio_sample_count = 0;
    int64_t first_video_dts = AV_NOPTS_VALUE;
    int64_t first_audio_dts = AV_NOPTS_VALUE;

    bool first_video_keyframe_sent = false;
    int retRead = 0;
    while (retRead >= 0) 
    {
        retRead = av_read_frame(in_fmt_ctx, pkt);
        
        if (retRead < 0) 
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(retRead, errbuf, sizeof(errbuf));
            std::cerr << "av_read_frame error: " << errbuf << " (" << retRead << ")" << std::endl;
            break; // 跳出循环
        }

        if (pkt->stream_index == video_index) 
        {
            // 记录第一个视频DTS作为偏移量
            if (first_video_dts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) 
            {
                first_video_dts = pkt->dts;
            }

            if (avcodec_send_packet(dec_ctx, pkt) < 0) 
            {
                av_packet_unref(pkt);
                continue;
            }

            while (avcodec_receive_frame(dec_ctx, frame) >= 0) 
            {
                AVFrame* yuv_frame = av_frame_alloc();
                yuv_frame->format = video_enc_ctx->pix_fmt;
                yuv_frame->width  = video_enc_ctx->width;
                yuv_frame->height = video_enc_ctx->height;
                av_frame_get_buffer(yuv_frame, 32);

                // sws 转换
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
                        yuv_frame->data, yuv_frame->linesize);

                // 处理 PTS
                if (frame->pts != AV_NOPTS_VALUE && first_video_dts != AV_NOPTS_VALUE) 
                {
                    yuv_frame->pts = av_rescale_q(frame->pts - first_video_dts,
                                                in_fmt_ctx->streams[video_index]->time_base,
                                                video_enc_ctx->time_base);
                } 
                else 
                {
                    yuv_frame->pts = video_frame_count++;
                }

                if (yuv_frame->pts < 0) 
                    yuv_frame->pts = 0;

                // 送入编码器
                if (avcodec_send_frame(video_enc_ctx, yuv_frame) >= 0) 
                {
                    AVPacket* out_pkt = av_packet_alloc();
                    while (avcodec_receive_packet(video_enc_ctx, out_pkt) >= 0) 
                    {
                        if (!first_video_keyframe_sent) 
                        {
                            if (!(out_pkt->flags & AV_PKT_FLAG_KEY)) 
                            {
                                std::cerr << "等待关键帧..." << std::endl;
                                av_packet_unref(out_pkt);
                                continue;  // 丢掉非关键帧，直到遇到关键帧
                            }

                            first_video_keyframe_sent = true;
                            std::cerr << "首个关键帧发送, pts=" << out_pkt->pts << std::endl;
                        }

                        out_pkt->stream_index = out_video_stream->index;

                        // 统一缩放时间戳
                        av_packet_rescale_ts(out_pkt, video_enc_ctx->time_base,
                                            out_video_stream->time_base);

                        if (out_pkt->pts < 0) out_pkt->pts = 0;
                        if (out_pkt->dts < 0) out_pkt->dts = 0;
                        
                        auto ret = av_interleaved_write_frame(out_fmt_ctx, out_pkt);
                        if (ret < 0) 
                        {
                            char errbuf[128];
                            av_strerror(ret, errbuf, sizeof(errbuf));
                            std::cerr << "Error writing video packet: " << errbuf << std::endl;
                        }

                        av_packet_unref(out_pkt);
                    }
                    av_packet_free(&out_pkt);
                }
                av_frame_free(&yuv_frame);
            }
        }
        else if (audio_index >= 0 && pkt->stream_index == audio_index && audio_dec_ctx) 
        {
            if (first_audio_dts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) 
            {
                first_audio_dts = pkt->dts;
            }

            if (avcodec_send_packet(audio_dec_ctx, pkt) < 0) 
            {
                av_packet_unref(pkt);
                continue;
            }

            AVFrame* decoded_audio = av_frame_alloc();

            while (avcodec_receive_frame(audio_dec_ctx, decoded_audio) >= 0) 
            {
                if (swr_ctx && audio_frame) {
                    swr_convert_frame(swr_ctx, audio_frame, decoded_audio);

                    // 处理 PTS
                    if (decoded_audio->pts != AV_NOPTS_VALUE && first_audio_dts != AV_NOPTS_VALUE) 
                    {
                        audio_frame->pts = av_rescale_q(decoded_audio->pts - first_audio_dts,
                                                        in_fmt_ctx->streams[audio_index]->time_base,
                                                        audio_enc_ctx->time_base);
                    } 
                    else 
                    {
                        audio_frame->pts = audio_sample_count;
                    }

                    audio_sample_count += audio_frame->nb_samples;

                    if (audio_frame->pts < 0) 
                        audio_frame->pts = 0;

                    // 送入编码器
                    if (avcodec_send_frame(audio_enc_ctx, audio_frame) >= 0) 
                    {
                        AVPacket* out_pkt = av_packet_alloc();
                        while (avcodec_receive_packet(audio_enc_ctx, out_pkt) >= 0) 
                        {
                            out_pkt->stream_index = out_audio_stream->index;

                            av_packet_rescale_ts(out_pkt, audio_enc_ctx->time_base,
                                                out_audio_stream->time_base);

                            if (out_pkt->pts < 0) out_pkt->pts = 0;
                            if (out_pkt->dts < 0) out_pkt->dts = 0;
                            
                            auto ret = av_interleaved_write_frame(out_fmt_ctx, out_pkt);
                            if (ret < 0) 
                            {
                                char errbuf[128];
                                av_strerror(ret, errbuf, sizeof(errbuf));
                                std::cerr << "Error writing audio packet: " << errbuf << std::endl;
                            }

                            av_packet_unref(out_pkt);
                        }
                        av_packet_free(&out_pkt);
                    }
                }
            }
            av_frame_free(&decoded_audio);
        }
        av_packet_unref(pkt);
    }

    std::cerr << "success writing av_write_trailer" << std::endl;


    av_write_trailer(out_fmt_ctx);

    // 清理资源
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&out_fmt_ctx->pb);
    }

    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    if (audio_frame) av_frame_free(&audio_frame);
    av_packet_free(&pkt);
    avcodec_free_context(&video_enc_ctx);
    avcodec_free_context(&dec_ctx);
    if (audio_enc_ctx) avcodec_free_context(&audio_enc_ctx);
    if (audio_dec_ctx) avcodec_free_context(&audio_dec_ctx);
    if (swr_ctx) swr_free(&swr_ctx);
    avformat_close_input(&in_fmt_ctx);
    avformat_free_context(out_fmt_ctx);

    return true;
}

void StreamTranscoder::OperationStream(const std::string& input_url)
{
    auto ff_err2str = [](int errnum) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(errnum, errbuf, sizeof(errbuf));
        return std::string(errbuf);
    };

    AVFormatContext *fmt_ctx = nullptr;
    int video_stream_index = -1 ,audio_stream_index= -1;
    Logger::getInstance()->info("start open input:{}", input_url);
    
    auto bRetState = InitInput(fmt_ctx,input_url,10000);

    if(!bRetState)
        return;

    // 查找视频流
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) 
    {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) 
        {
            video_stream_index = i;
        }
        else if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = i;
        }
    }

    if (video_stream_index == -1) 
    {
        Logger::getInstance()->error("video_index video_index erro:{}", input_url);
        avformat_close_input(&fmt_ctx);
        return;
    }

    if (audio_stream_index == -1) 
    {
        Logger::getInstance()->error("audio_index video_index erro:{}", input_url);
        avformat_close_input(&fmt_ctx);
        return;
    }

    m_video_index = video_stream_index;
    m_audio_index = audio_stream_index;

    // ---------------- 视频解码器 ----------------
    AVCodecParameters* video_codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
    const AVCodec* video_codec = avcodec_find_decoder(video_codecpar->codec_id);
    if (!video_codec) 
    {
        Logger::getInstance()->error("未找到视频解码器, codec_id =:{},input_url;{}", video_codecpar->codec_id ,input_url);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVCodecContext* video_codec_ctx = avcodec_alloc_context3(video_codec);
    if (!video_codec_ctx) 
    {
        Logger::getInstance()->error("avcodec_alloc_context3 video error input_url;{}", input_url);
        avformat_close_input(&fmt_ctx);
        return;
    }

    int ret = avcodec_parameters_to_context(video_codec_ctx, video_codecpar);
    if (ret < 0) 
    {
        Logger::getInstance()->error("avcodec_parameters_to_context video error:{} input_url;{}", ff_err2str(ret),input_url);
        avcodec_free_context(&video_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    ret = avcodec_open2(video_codec_ctx, video_codec, nullptr);
    if (ret < 0) 
    {
        Logger::getInstance()->error("avcodec_open2 video error:{} input_url;{}", ff_err2str(ret),input_url);
        avcodec_free_context(&video_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    // ---------------- 音频解码器 ----------------
    AVCodecParameters* audio_codecpar = fmt_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec* audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
    if (!audio_codec) 
    {
        Logger::getInstance()->error("未找到音频解码器, codec_id =:{},input_url;{}", video_codecpar->codec_id ,input_url);
        avcodec_free_context(&video_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVCodecContext* audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    if (!audio_codec_ctx) 
    {
        Logger::getInstance()->error("avcodec_alloc_context3 audio error input_url;{}", input_url);
        avcodec_free_context(&video_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    ret = avcodec_parameters_to_context(audio_codec_ctx, audio_codecpar);
    if (ret < 0) 
    {
        Logger::getInstance()->error("avcodec_parameters_to_context audio error:{} input_url;{}", ff_err2str(ret),input_url);
        avcodec_free_context(&video_codec_ctx);
        avcodec_free_context(&audio_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    ret = avcodec_open2(audio_codec_ctx, audio_codec, nullptr);
    if (ret < 0) 
    {
        Logger::getInstance()->error("avcodec_open2 audio error:{} input_url;{}", ff_err2str(ret),input_url);
        avcodec_free_context(&video_codec_ctx);
        avcodec_free_context(&audio_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    // 准备读取帧
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

     m_first_video_dts = AV_NOPTS_VALUE;
     m_first_audio_dts = AV_NOPTS_VALUE;

    while (av_read_frame(fmt_ctx, pkt) >= 0) 
    {
        AVCodecContext* codec_ctx = nullptr;
        AVMediaType media_type = AVMEDIA_TYPE_UNKNOWN;

        if (pkt->stream_index == video_stream_index) 
        {
            codec_ctx = video_codec_ctx;
            media_type = AVMEDIA_TYPE_VIDEO;

            if (m_first_video_dts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
                m_first_video_dts = pkt->dts;
        } 
        else if (pkt->stream_index == audio_stream_index) 
        {
            codec_ctx = audio_codec_ctx;
            media_type = AVMEDIA_TYPE_AUDIO;
            
            if (m_first_audio_dts == AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE)
                m_first_audio_dts = pkt->dts;
        } 
        else 
        {
            av_packet_unref(pkt);
            continue;
        }

        // === 解码 ===
        int ret = avcodec_send_packet(codec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;

        while (avcodec_receive_frame(codec_ctx, frame) == 0)
        {
            AVFrame* cloned = av_frame_clone(frame);
            if (!cloned) 
                break;

            MediaFrame mf = {
                .frame = cloned,
                .type = media_type,
                .pts = frame->pts,
                .dts = frame->pkt_dts,
            };

            m_frame_queue.push(mf);
            av_frame_unref(frame);
        }

        av_packet_unref(pkt);
    }

    // 释放资源
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&video_codec_ctx);
    avcodec_free_context(&audio_codec_ctx);
    avformat_close_input(&fmt_ctx);
}

void StreamTranscoder::DecodeFrameTransfer()
{
    while (m_bTransCoderState)
    {
        MediaFrame frame;
        if (!m_frame_queue.pop(frame, true))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // 分发到多个编码队列
        for (auto& q : m_veFrame_queue_transfer)
        {
            MediaFrame copyFrame;
            copyFrame.type = frame.type;
            copyFrame.pts = frame.pts;
            copyFrame.dts = frame.dts;
            copyFrame.frame = av_frame_clone(frame.frame); // ✅ 独立副本

            q.push(copyFrame);
        }

        // 原始帧释放
        av_frame_free(&frame.frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool StreamTranscoder::InitVideoEncoder(
    AVFormatContext* out_fmt_ctx,
    AVStream*& out_video_stream,
    AVCodecContext*& video_enc_ctx,
    const std::string& strUrl,
    int width,
    int height,
    int bitrate,
    int fps,
    const std::string& preset,
    const std::string& tune )
{
    const AVCodec* video_encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!video_encoder) {
        Logger::getInstance()->error("[VideoInit] Cannot find H264 encoder url: {}", strUrl);
        return false;
    }

    out_video_stream = avformat_new_stream(out_fmt_ctx, video_encoder);
    if (!out_video_stream) {
        Logger::getInstance()->error("[VideoInit] Cannot create output video stream url: {}", strUrl);
        return false;
    }

    video_enc_ctx = avcodec_alloc_context3(video_encoder);
    if (!video_enc_ctx) {
        Logger::getInstance()->error("[VideoInit] Cannot allocate video encoder context url: {}", strUrl);
        return false;
    }

    // 设置编码参数
    video_enc_ctx->width = width;
    video_enc_ctx->height = height;
    video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    video_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    video_enc_ctx->time_base = {1, fps};
    video_enc_ctx->bit_rate = bitrate;
    video_enc_ctx->gop_size = fps * 2; // GOP=2秒

    // H.264 特定设置
    av_opt_set(video_enc_ctx->priv_data, "preset", preset.c_str(), 0);
    av_opt_set(video_enc_ctx->priv_data, "tune", tune.c_str(), 0);

    // 打开编码器
    if (avcodec_open2(video_enc_ctx, video_encoder, nullptr) < 0) {
        Logger::getInstance()->error("[VideoInit] Cannot open video encoder url: {}", strUrl);
        avcodec_free_context(&video_enc_ctx);
        return false;
    }

    // 从编码上下文复制参数
    if (avcodec_parameters_from_context(out_video_stream->codecpar, video_enc_ctx) < 0) {
        Logger::getInstance()->error("[VideoInit] Cannot copy parameters to output video stream url: {}", strUrl);
        avcodec_free_context(&video_enc_ctx);
        return false;
    }

    // 确保 SPS/PPS 存在
    if (video_enc_ctx->extradata && video_enc_ctx->extradata_size > 0) {
        out_video_stream->codecpar->extradata = (uint8_t*)av_mallocz(video_enc_ctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!out_video_stream->codecpar->extradata) {
            Logger::getInstance()->error("[VideoInit] Failed to allocate extradata buffer url: {}", strUrl);
            avcodec_free_context(&video_enc_ctx);
            return false;
        }

        memcpy(out_video_stream->codecpar->extradata, video_enc_ctx->extradata, video_enc_ctx->extradata_size);
        out_video_stream->codecpar->extradata_size = video_enc_ctx->extradata_size;
    } else {
        Logger::getInstance()->warn("[VideoInit] SPS/PPS missing url: {}", strUrl);
        avcodec_free_context(&video_enc_ctx);
        return false;
    }

    // 输出流参数修正
    out_video_stream->codecpar->codec_tag = 0;
    out_video_stream->time_base = av_make_q(1, 1000); // 毫秒为单位

    // 最终验证
    if (!out_video_stream->codecpar->extradata) {
        Logger::getInstance()->warn("[VideoInit] SPS/PPS still missing after copy url: {}", strUrl);
        avcodec_free_context(&video_enc_ctx);
        return false;
    }

    Logger::getInstance()->info("[VideoInit] Video encoder initialized successfully url: {}, {}x{}, {}kbps @{}fps",
        strUrl, width, height, bitrate / 1000, fps);

    return true;
}

bool StreamTranscoder::InitAudioEncoder(AVFormatContext* in_fmt_ctx,
                                       AVFormatContext* out_fmt_ctx,
                                       AVCodecContext*& audio_enc_ctx,
                                       AVStream*& out_audio_stream,
                                       SwrContext*& swr_ctx,
                                       AVFrame*& audio_frame,
                                       int audio_index,
                                       const std::string& strUrl)
{
    StreamError retType = OPERATION_OK;

    const AVCodec* audio_encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audio_encoder) {
        Logger::getInstance()->error("[{}] Cannot find AAC encoder", strUrl);
        return false;
    }

    // 新建输出流
    out_audio_stream = avformat_new_stream(out_fmt_ctx, audio_encoder);
    if (!out_audio_stream) {
        Logger::getInstance()->error("[{}] Cannot create output audio stream", strUrl);
        return false;
    }

    // 分配编码器上下文
    audio_enc_ctx = avcodec_alloc_context3(audio_encoder);
    if (!audio_enc_ctx) {
        Logger::getInstance()->error("[{}] Cannot allocate audio encoder context", strUrl);
        return false;
    }

    // 获取输入音频流参数
    AVCodecParameters* in_audio_par = in_fmt_ctx->streams[audio_index]->codecpar;

    // 设置 AAC 编码参数
    audio_enc_ctx->ch_layout = in_audio_par->ch_layout;
    audio_enc_ctx->sample_rate = in_audio_par->sample_rate;
    audio_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audio_enc_ctx->bit_rate = 128000;
    audio_enc_ctx->time_base = { 1, audio_enc_ctx->sample_rate };

    Logger::getInstance()->info("[{}] Init AAC encoder: rate={}Hz, ch={}, fmt={}, bitrate={}",
        strUrl,
        audio_enc_ctx->sample_rate,
        audio_enc_ctx->ch_layout.nb_channels,
        av_get_sample_fmt_name(audio_enc_ctx->sample_fmt),
        audio_enc_ctx->bit_rate);

    // 打开 AAC 编码器
    if (avcodec_open2(audio_enc_ctx, audio_encoder, nullptr) < 0) {
        Logger::getInstance()->error("[{}] Failed to open AAC encoder", strUrl);
        goto fail;
    }

    // ==== 初始化重采样器 ====
    {
        int ret = swr_alloc_set_opts2(
            &swr_ctx,
            &audio_enc_ctx->ch_layout, audio_enc_ctx->sample_fmt, audio_enc_ctx->sample_rate,
            &in_audio_par->ch_layout, (AVSampleFormat)in_audio_par->format, in_audio_par->sample_rate,
            0, nullptr
        );
        if (ret < 0) {
            Logger::getInstance()->error("[{}] swr_alloc_set_opts2 failed ({})", strUrl, ret);
            goto fail;
        }

        if (swr_init(swr_ctx) < 0) {
            Logger::getInstance()->error("[{}] swr_init failed", strUrl);
            goto fail;
        }

        Logger::getInstance()->info("[{}] SwrContext initialized ({}Hz -> {}Hz, {} -> {})",
            strUrl,
            in_audio_par->sample_rate,
            audio_enc_ctx->sample_rate,
            av_get_sample_fmt_name((AVSampleFormat)in_audio_par->format),
            av_get_sample_fmt_name(audio_enc_ctx->sample_fmt));
    }

    // ==== 分配音频帧 ====
    audio_frame = av_frame_alloc();
    if (!audio_frame) {
        Logger::getInstance()->error("[{}] Cannot allocate audio frame", strUrl);
        goto fail;
    }

    audio_frame->ch_layout = audio_enc_ctx->ch_layout;
    audio_frame->format = audio_enc_ctx->sample_fmt;
    audio_frame->sample_rate = audio_enc_ctx->sample_rate;
    audio_frame->nb_samples = 1024;

    if (av_frame_get_buffer(audio_frame, 0) < 0) {
        Logger::getInstance()->error("[{}] Cannot allocate audio frame buffer", strUrl);
        goto fail;
    }

    // ==== 设置输出流参数 ====
    if (avcodec_parameters_from_context(out_audio_stream->codecpar, audio_enc_ctx) < 0) {
        Logger::getInstance()->error("[{}] Cannot copy parameters to output audio stream", strUrl);
        goto fail;
    }

    out_audio_stream->codecpar->codec_tag = 0;
    out_audio_stream->time_base = av_make_q(1, 1000);

    Logger::getInstance()->info("[{}] AAC encoder initialized successfully", strUrl);
    return true;

fail:
    Logger::getInstance()->warn("[{}] InitAudioEncoder failed, cleaning up...", strUrl);

    if (audio_frame) {
        av_frame_free(&audio_frame);
        audio_frame = nullptr;
    }
    if (swr_ctx) {
        swr_free(&swr_ctx);
        swr_ctx = nullptr;
    }
    if (audio_enc_ctx) {
        avcodec_free_context(&audio_enc_ctx);
        audio_enc_ctx = nullptr;
    }
    if (out_audio_stream) {
        // 注意：stream 在 avformat_free_context(out_fmt_ctx) 时会自动释放，
        // 不要单独 free，只需置空
        out_audio_stream = nullptr;
    }

    return false;
}

void StreamTranscoder::EncodeFramePushStream(int nIndex,const std::string& strUrl)
{
    // 视频编码器
    int video_index = -1;
    int audio_index = -1;
    video_index = m_video_index;
    m_audio_index = audio_index;

    AVFormatContext* out_fmt_ctx = nullptr;
    AVStream* out_video_stream = nullptr;
    AVCodecContext* video_enc_ctx = nullptr;

    AVStream* out_audio_stream = nullptr;
    SwrContext* swr_ctx = nullptr;
    AVFrame* audio_frame = nullptr;
    AVCodecContext* audio_enc_ctx = nullptr;

    //初始化输出
    if (!InitOutput(out_fmt_ctx,in_fmt_ctx,strUrl)) 
    {
        Logger::getInstance()->error("警告: InitOutput fail! url: {}",strUrl);
        avformat_free_context(out_fmt_ctx);
        return;
    }

    // 视频编码器
    if (!InitVideoEncoder(in_fmt_ctx,out_video_stream,video_enc_ctx,strUrl)) 
    {
        Logger::getInstance()->error("警告: InitAudioEncoder fail! url: {}",strUrl);
        avformat_free_context(out_fmt_ctx);
        return;
    }

    // 音频编码器
    if (!InitAudioEncoder(in_fmt_ctx,out_fmt_ctx,audio_enc_ctx,out_audio_stream
                            ,swr_ctx,audio_frame,m_audio_index,strUrl)) 
    {
        Logger::getInstance()->error("警告: InitAudioEncoder fail! url: {}",strUrl);
        avcodec_free_context(&video_enc_ctx);
        avformat_free_context(out_fmt_ctx);
        return;
    }

    // 打开输出 IO
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
    {
        if (avio_open(&out_fmt_ctx->pb, strUrl.c_str(), AVIO_FLAG_WRITE) < 0) 
        {
            Logger::getInstance()->error("Cannot open output URL url: {}",strUrl);
            if (audio_enc_ctx) 
                avcodec_free_context(&audio_enc_ctx);
            avcodec_free_context(&video_enc_ctx);
            avformat_free_context(out_fmt_ctx);
            return;
        }
    }

    // 写 header
    if (avformat_write_header(out_fmt_ctx, nullptr) < 0) 
    {
        Logger::getInstance()->error("Error writing header url: {}",strUrl);
        if (audio_enc_ctx) 
            avcodec_free_context(&audio_enc_ctx);
        avcodec_free_context(&video_enc_ctx);
        avformat_free_context(out_fmt_ctx);
        return;
    }

    //获取解码器上下文
    AVCodecContext* in_dec_ctx = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context((AVCodecContext*)in_dec_ctx, in_fmt_ctx->streams[video_index]->codecpar);

    if (!in_dec_ctx) 
    {
        Logger::getInstance()->error("Error avcodec_parameters_to_context url: {}",strUrl);
        if (audio_enc_ctx) 
            avcodec_free_context(&audio_enc_ctx);
        avcodec_free_context(&video_enc_ctx);
        avformat_free_context(out_fmt_ctx);
        return;
    }

    SwsContext* sws_ctx = nullptr;
    sws_ctx = sws_getContext(
        in_dec_ctx->width, in_dec_ctx->height, (AVPixelFormat)in_fmt_ctx->streams[video_index]->codecpar->format,
        video_enc_ctx->width, video_enc_ctx->height, video_enc_ctx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!sws_ctx) 
    {
        Logger::getInstance()->error("Error avcodec_parameters_to_context url: {}",strUrl);
        if (audio_enc_ctx) 
            avcodec_free_context(&audio_enc_ctx);
        avcodec_free_context(&video_enc_ctx);
        avformat_free_context(out_fmt_ctx);
        avcodec_free_context(&in_dec_ctx);
        return;
    }

    int64_t video_frame_count = 0;
    int64_t audio_sample_count = 0;
    int64_t first_video_dts = m_first_video_dts;
    int64_t first_audio_dts = m_first_audio_dts;

    bool first_video_keyframe_sent = false;
    int ret = 0;

    while (m_bTransCoderState)
    {
        MediaFrame mediaFrame;
        if (!m_veFrame_queue_transfer[nIndex].pop(mediaFrame, true))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        AVFrame* frame = mediaFrame.frame;
        if (!frame)
        {
            Logger::getInstance()->warn("Empty frame received at encoder [{}]", nIndex);
            continue;
        }

        if (mediaFrame.type == AVMEDIA_TYPE_VIDEO) 
        {
            // 视频帧
            AVFrame* yuv_frame = av_frame_alloc();
            if (!yuv_frame)
            {
                Logger::getInstance()->error("Failed to allocate YUV frame (video) [{}]", nIndex);
                av_frame_free(&frame);
                continue;
            }

            yuv_frame->format = video_enc_ctx->pix_fmt;
            yuv_frame->width  = video_enc_ctx->width;
            yuv_frame->height = video_enc_ctx->height;

            if ((ret = av_frame_get_buffer(yuv_frame, 32)) < 0)
            {
                Logger::getInstance()->error("av_frame_get_buffer failed [{}]: {}", nIndex, ret);
                av_frame_free(&yuv_frame);
                av_frame_free(&frame);
                continue;
            }

            // sws 转换
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
                    yuv_frame->data, yuv_frame->linesize);

            // PTS 处理
            if (mediaFrame.pts != AV_NOPTS_VALUE && first_video_dts != AV_NOPTS_VALUE)
                yuv_frame->pts = av_rescale_q(mediaFrame.pts - first_video_dts,
                                            in_fmt_ctx->streams[video_index]->time_base,
                                            video_enc_ctx->time_base);
            else
                yuv_frame->pts = video_frame_count++;

            if (yuv_frame->pts < 0)
                yuv_frame->pts = 0;

            // 送入编码器
            if ((ret = avcodec_send_frame(video_enc_ctx, yuv_frame)) < 0)
            {
                Logger::getInstance()->error("avcodec_send_frame (video) failed [{}]: {}", nIndex, ret);
                av_frame_free(&yuv_frame);
                av_frame_free(&frame);
                continue;
            }

            AVPacket* out_pkt = av_packet_alloc();
            if (!out_pkt)
            {
                Logger::getInstance()->error("Failed to allocate AVPacket (video) [{}]", nIndex);
                av_frame_free(&yuv_frame);
                av_frame_free(&frame);
                continue;
            }

            while ((ret = avcodec_receive_packet(video_enc_ctx, out_pkt)) >= 0)
            {
                if (!first_video_keyframe_sent)
                {
                    if (!(out_pkt->flags & AV_PKT_FLAG_KEY))
                    {
                        Logger::getInstance()->info("等待关键帧...");
                        av_packet_unref(out_pkt);
                        continue;
                    }

                    first_video_keyframe_sent = true;
                    Logger::getInstance()->info("首个关键帧发送, pts={}", out_pkt->pts);
                }

                out_pkt->stream_index = out_video_stream->index;

                av_packet_rescale_ts(out_pkt, video_enc_ctx->time_base,
                                    out_video_stream->time_base);
                
                out_pkt->pts = std::max<int64_t>(0, out_pkt->pts);
                out_pkt->dts = std::max<int64_t>(0, out_pkt->dts);

                if ((ret = av_interleaved_write_frame(out_fmt_ctx, out_pkt)) < 0)
                {
                    char errbuf[128];
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    Logger::getInstance()->error("Error writing video packet [{}]: {}", nIndex, errbuf);
                }

                av_packet_unref(out_pkt);
            }

            av_packet_free(&out_pkt);
            av_frame_free(&yuv_frame);
            av_frame_free(&frame);
        }
        else if (mediaFrame.type == AVMEDIA_TYPE_AUDIO) 
        {
            // 音频帧
            AVFrame* decoded_audio = frame;

            if (swr_ctx && audio_frame) 
            {
                swr_convert_frame(swr_ctx, audio_frame, decoded_audio);

                // 处理 PTS
                if (mediaFrame.pts != AV_NOPTS_VALUE && first_audio_dts != AV_NOPTS_VALUE)
                    audio_frame->pts = av_rescale_q(mediaFrame.pts - first_audio_dts,
                                                    in_fmt_ctx->streams[audio_index]->time_base,
                                                    audio_enc_ctx->time_base);
                else
                    audio_frame->pts = audio_sample_count;

                audio_sample_count += audio_frame->nb_samples;

                if (audio_frame->pts < 0) 
                    audio_frame->pts = 0;

                // 送入编码器
                if (avcodec_send_frame(audio_enc_ctx, audio_frame) >= 0) 
                {
                    AVPacket* out_pkt = av_packet_alloc();
                    while (avcodec_receive_packet(audio_enc_ctx, out_pkt) >= 0) 
                    {
                        out_pkt->stream_index = out_audio_stream->index;

                        av_packet_rescale_ts(out_pkt, audio_enc_ctx->time_base,
                                            out_audio_stream->time_base);

                        if (out_pkt->pts < 0) out_pkt->pts = 0;
                        if (out_pkt->dts < 0) out_pkt->dts = 0;
                        
                        auto ret = av_interleaved_write_frame(out_fmt_ctx, out_pkt);
                        if (ret < 0) 
                        {
                            char errbuf[128];
                            av_strerror(ret, errbuf, sizeof(errbuf));
                            std::cerr << "Error writing audio packet: " << errbuf << std::endl;
                        }

                        av_packet_unref(out_pkt);
                    }
                    av_packet_free(&out_pkt);
                }
            }

            av_frame_free(&decoded_audio);
        }
        else 
        {
            Logger::getInstance()->warn("Unknown frame type");
        }

        //主队列里的 frame 用完后释放
        av_frame_free(&frame);
    }

    av_write_trailer(out_fmt_ctx);
    sws_freeContext(sws_ctx);

    if (audio_frame) 
        av_frame_free(&audio_frame);

    if (video_enc_ctx) 
        avcodec_free_context(&video_enc_ctx);

    if (in_dec_ctx) 
        avcodec_free_context(&in_dec_ctx);

    if (audio_enc_ctx) 
        avcodec_free_context(&audio_enc_ctx);

    if (swr_ctx) 
        swr_free(&swr_ctx);
    avformat_free_context(out_fmt_ctx);

}
