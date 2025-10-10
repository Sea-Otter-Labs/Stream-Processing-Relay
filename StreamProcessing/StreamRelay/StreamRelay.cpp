#include "StreamRelay.h"
#include "Logger.h"
#include <regex>
#include "HttpServer.h"
#include <mysql/mysql.h>

namespace fs = std::filesystem;

// Logger::getInstance()->debug("This is debug message");
// Logger::getInstance()->info("This is info message");
// Logger::getInstance()->warn("This is warning");
// Logger::getInstance()->error("This is error");

bool clearHlsfile(const std::string& stream_name,
                  const std::string& path = "/hls/live",
                  int expire_seconds = 0) // 过期时间，秒
{
    try
    {
        if (!fs::exists(path))
        {
            Logger::getInstance()->error("Directory does not exist:{}", path);
            return false;
        }

        // 匹配规则：stream.m3u8 或 stream-xxx.ts
        std::regex pattern("^" + stream_name + R"((\.m3u8|-\d+\.ts)$)");

        auto now = std::chrono::system_clock::now();
        bool removed_any = false;

        for (auto& entry : fs::directory_iterator(path))
        {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();
            if (!std::regex_match(filename, pattern)) continue;

            bool should_remove = true;

            if (expire_seconds > 0)
            {
                // 文件最后修改时间
                auto ftime = fs::last_write_time(entry.path());
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - decltype(ftime)::clock::now()
                    + std::chrono::system_clock::now());

                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - sctp).count();
                if (age < expire_seconds)
                {
                    should_remove = false;
                }
            }

            if (should_remove)
            {
                std::error_code ec;
                fs::remove(entry.path(), ec);
                if (ec)
                {
                    Logger::getInstance()->error("Failed to remove:{} {}", entry.path().string(), ec.message());
                }
                else
                {
                    removed_any = true;
                    //Logger::getInstance()->info("Removed expired HLS file:{}", entry.path().string());
                }
            }
        }

        if (!removed_any)
        {
            //Logger::getInstance()->warn("No expired files found for stream:{}", stream_name);
        }
        else
        {
            Logger::getInstance()->info("Expired HLS files cleared for stream:{}", stream_name);
        }

        return true;
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->error("[EXCEPTION]:{} stream:{}", e.what(), stream_name);
        return false;
    }
}

StreamRelay::StreamRelay()
{

}

StreamRelay::~StreamRelay()
{

}

void StreamRelay::InitStreamConfig()
{

}

void StreamRelay::InitStream()
{

}

void StreamRelay::addSqlDbData(const StreamDbDataInfo &stStreamPushData,const StreamDbDataInfo &oldStStreamPushData)
{
    auto streamInfo = m_streamInfo;

    // 1. 初始化 MySQL
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) 
    {
        Logger::getInstance()->error("mysql_init failed");
        return;
    }

    // 2. 连接数据库
    if (!mysql_real_connect(conn, SQL_HOST, SQL_USER, SQL_PASSWD, SQL_DBNAME, SQL_PORT, nullptr, 0)) 
    {
        Logger::getInstance()->error("Connection failed: {}", mysql_error(conn));
        mysql_close(conn);
        return;
    }

    // 3. 获取当前 UTC 时间戳（秒级）
    std::time_t utcTime = std::time(nullptr);       // 获取 UTC 时间戳（秒级）
    std::string utcTimeStr = std::to_string(utcTime); // 转成字符串

    // 4. 对字符串进行转义，防止 SQL 注入
    char escapedTargetMatching[1024] = {0};
    char escapedUrl[1024] ,old_escapedUrl[1024] = {0};

    mysql_real_escape_string(conn, escapedTargetMatching, streamInfo.target_matching.c_str(), streamInfo.target_matching.length());
    mysql_real_escape_string(conn, escapedUrl, stStreamPushData.url.c_str(), stStreamPushData.url.length());
    mysql_real_escape_string(conn, old_escapedUrl, oldStStreamPushData.url.c_str(), oldStStreamPushData.url.length());

    // 5. 拼接 INSERT SQL
    std::ostringstream oss;
    oss << "INSERT INTO live_stream_sources_push_log "
        << "(target_matching_id, target_matching, url_id, url, old_url_id, old_url, upload_time) VALUES ("
        << streamInfo.target_matching_id << ", "
        << "'" << escapedTargetMatching << "', "
        << stStreamPushData.id << ", "
        << "'" << escapedUrl << "', "
        << "'" << oldStStreamPushData.id << "', "
        << "'" << old_escapedUrl << "', "
        << utcTimeStr
        << ")";

    std::string sql = oss.str();

    // 6. 执行 SQL
    if (mysql_query(conn, sql.c_str())) 
    {
        Logger::getInstance()->error("MySQL insert failed: {}", mysql_error(conn));
    } 

    // 7. 关闭连接
    mysql_close(conn);
}

//若非主源在切换过程中发现当前源在列表中丢失 或者优先级别发送改变 则表示列表发生变更则直接切到主源重复上述逻辑
bool StreamRelay::Start()
{
    if (m_threadRelay.joinable()) return false; // 避免重复启动

    m_PushState = true;

    m_threadRelay = std::thread([this]{
        const int nRetry = 3; //单个流重试次数
        const int retryInterval = 10; // 全部失败后等待时间秒
        const int nMaxRetryTimeSeconds = 300; // 秒
        StreamDbDataInfo oldSources;

        while(m_PushState)
        {
            bool played = false;
            auto streamInfo = m_streamInfo;

            // 按主源优先
            std::vector<StreamDbDataInfo> sources = streamInfo.veStrDbDataInfo;
            //先找主源
            for (auto& src : sources)
            {
                if (src.is_backup == 0 && m_PushState)
                {
                    addSqlDbData(src,oldSources);
                    played = tryPlaySource(src, nRetry);
                    oldSources = src;
                    break;
                }
            }
            //如果主源都失败，再尝试备用源按优先级

            if (!played)
            {
                // 按 priority 排序
                std::sort(sources.begin(), sources.end(),
                            [](const StreamDbDataInfo& a, const StreamDbDataInfo& b){ return a.flow_score > b.flow_score; });

                for (auto& src : sources)
                {
                    if (m_PushState)
                    {
                        addSqlDbData(src,oldSources);
                        auto nowTimeStart = std::chrono::steady_clock::now();
                        played = tryPlaySource(src, nRetry);
                        auto nowTimeFinish = std::chrono::steady_clock::now();
                        oldSources = src;
                        
                        //重置操作 稳定超过十分钟后中断切换成主流
                        auto timeSeconds = std::chrono::duration_cast<std::chrono::seconds>(nowTimeFinish - nowTimeStart).count();
                        if (timeSeconds > nMaxRetryTimeSeconds)
                        {
                            Logger::getInstance()->info("源: {} 播放：{}秒后中断,切换主源播放", 
                                    src.url, timeSeconds);
                            played = true;
                            break;
                        }
                    }
                }
            }

            if (!played && m_PushState)
            {
                OnFailCallBack(streamInfo.target_matching);

                Logger::getInstance()->error("节目 [{}] 所有源播放失败，等待 {} 秒后重试", 
                                             streamInfo.target_matching, retryInterval);
                std::this_thread::sleep_for(std::chrono::seconds(retryInterval));
            }
        }
    });

    return true;
}

void StreamRelay::OnFailCallBack(std::string url)
{
    if(m_failCallback)
    {
        m_failCallback(url);
    }
}

void StreamRelay::OnStatusCallBack(StreamDbDataInfo stStreamPushData)
{
    auto streamInfo = m_streamInfo;

    for (int i =0; i < streamInfo.veStrDbDataInfo.size(); i++)
    {
        if(streamInfo.veStrDbDataInfo[i].id == m_stPushStreamInfoNow.id)
        {
            //避免重复写数据库
            if(m_stPushStreamInfoNow.play_state == 1)
            {
                return;
            }
            streamInfo.veStrDbDataInfo[i].play_state = 1;
        }
        else
        {
            streamInfo.veStrDbDataInfo[i].play_state = 0;
        }
    }

    if(m_statusCallback)
    {
        m_statusCallback(streamInfo);
    }
}

bool StreamRelay::tryPlaySource(StreamDbDataInfo& src, int nRetry)
{
    m_stPushStreamInfoNow = src;
    auto playStartTime = std::chrono::steady_clock::now();
    int MAX_RETRY_INTERVAL = 60; // 秒
    StreamError enRet = OPERATION_OK;

    for (int i = 0; i < nRetry && m_PushState; i++)
    {
        auto nowTime = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(nowTime - playStartTime).count() > MAX_RETRY_INTERVAL 
            && (enRet != OPEN_INPUT_FAILED && enRet != STREAM_INFO_FAILED && enRet != FIND_VIDEO_STREAM_FAILED))
        {
            i = 0;  // 重置索引
            playStartTime = nowTime; // 重新计时
        }

        Logger::getInstance()->info("尝试启动节目：{} 源: {} (is_backup={}, priority={}), 第 {} 次", 
                                    m_streamInfo.target_matching, src.url, src.is_backup, src.priority, i+1);
        
        //清除过期文件
        clearHlsfile(m_streamInfo.stream_name_format);

        //启动ffmpeg 命令行 监管命令行 
        if(true)
        {
            std::string strDecode =
                " -c:v libx264 -preset veryfast -tune zerolatency "
                "-b:v 1500k -maxrate 1500k -bufsize 3000k "
                "-vf scale=-2:720 "
                "-r 25 -g 50 -keyint_min 50  "
                "-c:a aac";

            //std::string strDecode = " -c:v libx264 -preset veryfast -tune zerolatency -g 100 -keyint_min 100 -c:a aac";

            std::string strCopy = " -c:v copy -c:a aac -ar 44100 -ac 2 -b:a 128k";
            std::string strLog = "  > ./logs/" + m_streamInfo.stream_name_format + ".log 2>&1";

            int id = m_streamInfo.target_matching_id;
            auto it2 = idToProgramName.find(id);
            if (it2 == idToProgramName.end()) 
            {
                //不在列表中的节目不转码
                strDecode = strCopy;
            }

            std::string ffmpegCmd = "ffmpeg -loglevel debug -re -rw_timeout 15000000  -i \"" + src.url + "\" "
                                + strDecode + " -f flv " + m_streamInfo.target_matching_format + strLog;
            
            if(m_stPushStreamInfoNow.play_state == 0)
            {
                OnStatusCallBack(m_stPushStreamInfoNow);
            }    

            // 构建 ffmpeg 参数列表
            std::vector<std::string> args = {
                "ffmpeg",
                "-loglevel", "debug",
                "-re",
                "-rw_timeout", "15000000",
                "-i", src.url,
            };

            // 拆分 strDecode 参数并加入 args
            std::istringstream iss(strDecode);
            for (std::string s; iss >> s;) args.push_back(s);

            // 输出格式
            args.push_back("-f");
            args.push_back("flv");
            args.push_back(m_streamInfo.target_matching_format);

            // fork 启动 ffmpeg
            pid_t pid = fork();
            if (pid == 0) {
                // 子进程

                // 日志重定向
                std::string logFile = "./logs/" + m_streamInfo.stream_name_format + ".log";
                int fd = open(logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd >= 0) {
                    dup2(fd, STDOUT_FILENO);
                    dup2(fd, STDERR_FILENO);
                    close(fd);
                }

                // 构建 exec 参数
                std::vector<char*> argv;
                for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
                argv.push_back(nullptr);

                execvp("ffmpeg", argv.data());
                _exit(127); // exec 失败
            } 
            else if (pid > 0) 
            {
                // 父进程
                int status = 0;

                while (m_PushState) 
                {
                    pid_t w = waitpid(pid, &status, WNOHANG);
                    if (w == pid) {
                        // FFmpeg 已退出
                        if (WIFEXITED(status)) 
                        {
                            Logger::getInstance()->warn("节目 [{}], 源 [{}] FFmpeg 退出, exit code={}",
                                                        m_streamInfo.target_matching, src.url, WEXITSTATUS(status));
                        } 
                        else if (WIFSIGNALED(status)) 
                        {
                            Logger::getInstance()->warn("节目 [{}], 源 [{}] FFmpeg 被信号 {} 杀死",
                                                        m_streamInfo.target_matching, src.url, WTERMSIG(status));
                        }
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                // 如果主动停止
                if (!m_PushState) 
                {
                    kill(pid, SIGKILL);      // 杀整个进程组
                    waitpid(pid, &status, 0);  // 回收子进程，避免僵尸
                    Logger::getInstance()->info("节目 [{}] FFmpeg 被主动停止", m_streamInfo.target_matching);
                }
            } 
            else 
            {
                Logger::getInstance()->error("fork 失败，无法启动 FFmpeg");
                return false;
            }
            
        }
        else
        {
            if(false)
            {
                enRet = relay_stream(src.url, m_streamInfo.target_matching_format);
                
                Logger::getInstance()->warn("节目 [{}],源 [{}] 启动失败，第 {} 次重试,enRet:{}"
                    ,m_streamInfo.target_matching, src.url, i+1,(int)enRet);
            }
            else
            {
                pid_t pid = fork();
                if (pid == 0) 
                {
                    // std::string strPath = "./logs/" + m_streamInfo.stream_name_format;
                    // Logger::init(Logger::Level::Debug, strPath, 10*1024*1024, 5);
                    // 子进程执行 relay_stream
                    StreamError ret = relay_stream(src.url, m_streamInfo.target_matching_format);
                    // 正常退出时，返回码传给父进程
                    _exit((int)ret);
                } 
                else if (pid > 0) 
                {
                    // 父进程
                    int status = 0;
                    while (m_PushState) 
                    {
                        pid_t w = waitpid(pid, &status, WNOHANG);
                        if (w == pid) 
                        {
                            //  退出
                            if (WIFEXITED(status)) 
                            {
                                enRet = (StreamError)WEXITSTATUS(status);
                                Logger::getInstance()->warn("节目 [{}], 源 [{}] 退出, exit code={}", 
                                                            m_streamInfo.target_matching, src.url, (int)enRet);
                            } 
                            else if (WIFSIGNALED(status)) 
                            {
                                Logger::getInstance()->warn("节目 [{}], 源 [{}] 被信号 {} 杀死", 
                                                            m_streamInfo.target_matching, src.url, WTERMSIG(status));
                            }
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }

                    // 如果主动停止，需要杀掉 
                    if (!m_PushState) 
                    {
                        kill(pid, SIGKILL);
                        Logger::getInstance()->info("节目 [{}] 被主动停止", m_streamInfo.target_matching);
                        break;
                    }
                } 
                else 
                {
                    Logger::getInstance()->error("fork failed!");
                }
            }
        }

        if (!m_PushState) 
            return false;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    return false;
}

void StreamRelay::Restart()
{
    Stop();
    Start();
}

void StreamRelay::Stop()
{
    if (m_PushState)
    {
        m_PushState = false;
    }

    if (m_threadRelay.joinable()) 
    {
        m_threadRelay.join();
    }
}

void StreamRelay::Update(const OutPutStreamInfo& newInfo) 
{
    // 替换源地址
    m_streamInfo = newInfo;
    // Stop();
    // Start();
}

bool StreamRelay::open_input(AVFormatContext*& in_fmt_ctx, const std::string& input_url, int timeout_ms)
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

    TimeoutData timeout_data;
    timeout_data.timeout_ms = timeout_ms;
    timeout_data.start_time = std::chrono::steady_clock::now();
    AVIOInterruptCB int_cb = {interrupt_cb, &timeout_data};
    in_fmt_ctx = avformat_alloc_context();
    in_fmt_ctx->interrupt_callback = int_cb;

    // 打开输入
    if (avformat_open_input(&in_fmt_ctx, input_url.c_str(), nullptr, &options) < 0) 
    {
        std::cerr << "Cannot open input URL: " << input_url << std::endl;
        av_dict_free(&options);
        return false;
    }

    av_dict_free(&options);

    // 读取流信息
    if (avformat_find_stream_info(in_fmt_ctx, nullptr) < 0) 
    {
        std::cerr << "Failed to get stream info: " << input_url << std::endl;
        avformat_close_input(&in_fmt_ctx);
        return false;
    }

    std::cout << "Input opened OK: " << input_url << std::endl;
    return true;
}

bool StreamRelay::reset_output(AVFormatContext*& out_fmt_ctx,AVFormatContext* in_fmt_ctx,const std::string& output_url) 
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
        std::cerr << "Cannot create output context" << std::endl;
        return false;
    }

    // 给输出加上对应的流（拷贝输入的流参数）
    for (unsigned int i = 0; i < in_fmt_ctx->nb_streams; i++) 
    {
        AVStream* in_stream = in_fmt_ctx->streams[i];
        AVStream* out_stream = avformat_new_stream(out_fmt_ctx, nullptr);
        if (!out_stream) {
            std::cerr << "Failed to allocate output stream" << std::endl;
            return false;
        }
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            std::cerr << "Failed to copy codec parameters" << std::endl;
            return false;
        }
        out_stream->codecpar->codec_tag = 0;
    }

    // 打开输出 IO
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
    {
        if (avio_open(&out_fmt_ctx->pb, output_url.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Cannot open output URL: " << output_url << std::endl;
            return false;
        }
    }

    // 写 header
    if (avformat_write_header(out_fmt_ctx, nullptr) < 0) 
    {
        std::cerr << "Error writing header" << std::endl;
        return false;
    }

    std::cout << "Output reset OK: " << output_url << std::endl;
    return true;
}

StreamError StreamRelay::relay_stream(const std::string& input_url, const std::string& output_url) 
{
    auto ff_err2str = [](int errnum) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(errnum, errbuf, sizeof(errbuf));
        return std::string(errbuf);
    };

    AVFormatContext* in_fmt_ctx = nullptr;
    AVFormatContext* out_fmt_ctx = nullptr;
    AVPacket pkt;

    Logger::getInstance()->info("start open input:{}", input_url);
    StreamError enRetType = OPERATION_OK;
    int ret = 0;
    int video_index = -1;
    int audio_index = -1;
    std::map<int, int> stream_mapping;
    bool has_asc = true;

    bool bUpdateState = true;
    int64_t last_dts = AV_NOPTS_VALUE;
    
    AVDictionary* options = nullptr;
    av_dict_set(&options, "reconnect", "1", 0); // 允许自动重连
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "10", 0); // 最大重连延迟10秒
    av_dict_set(&options, "timeout", "10000000", 0); // 5秒超时

    // 1. 打开输入流
    if (avformat_open_input(&in_fmt_ctx, input_url.c_str(), nullptr, &options) < 0) 
    {
        Logger::getInstance()->error("Cannot open input:{}", input_url);
        enRetType = OPEN_INPUT_FAILED;
        goto end;
    }

    if (avformat_find_stream_info(in_fmt_ctx, nullptr) < 0) 
    {
        Logger::getInstance()->error("Cannot find input stream info:{}", input_url);
        enRetType = STREAM_INFO_FAILED;
        goto end;
    }
    
    in_fmt_ctx->flags |= AVFMT_FLAG_GENPTS;

    // ====== 初始化输出流 ======
    for (unsigned int i = 0; i < in_fmt_ctx->nb_streams; i++) {
        AVStream* in_stream = in_fmt_ctx->streams[i];

        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (video_index != -1) continue; // 已经选过视频，跳过
            video_index = i;
        } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audio_index != -1) continue; // 已经选过音频，跳过
            audio_index = i;
        } else {
            stream_mapping[i] = -1;
            continue; // 其他流（字幕/数据流），不要
        }
        //stream_mapping[i] = out_stream->index;
    }

    if (audio_index == -1 && video_index == -1)
    {
        Logger::getInstance()->error("audio_index video_index erro:{}", input_url);
        enRetType = FIND_VIDEO_STREAM_FAILED;
        goto end;
    }

    // 2. 分配输出上下文
    if (avformat_alloc_output_context2(&out_fmt_ctx, nullptr, "flv", output_url.c_str()) < 0) 
    {
        Logger::getInstance()->error("Cannot alloc output ctx:{}", input_url);
        enRetType = ALLOC_VIDEO_CTX_FAILED;
        goto end;
    }

    // 3. 为输出流复制输入流的参数
    for (unsigned int i = 0; i < in_fmt_ctx->nb_streams; i++) 
    {
        if (i != video_index && i != audio_index) 
        {
            Logger::getInstance()->error("get out error nb_streams:{} input_url:{}", i ,input_url );
            continue; // 跳过未选择的流
        }

        AVStream* in_stream = in_fmt_ctx->streams[i];
        AVStream* out_stream = avformat_new_stream(out_fmt_ctx, nullptr);
        if (!out_stream) 
        {
            Logger::getInstance()->error("Failed to create output stream:{}", input_url);
            enRetType = COPY_VIDEO_PARAMS_FAILED;
            goto end;
        }

        // 复制编解码器参数（不解码）
        ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (ret < 0) 
        {
            Logger::getInstance()->error("avcodec_parameters_copy failed (err={}, reason={}), input_url={}", 
                                        ret, ff_err2str(ret), input_url);
            enRetType = COPY_VIDEO_PARAMS_FAILED;
            goto end;
        }

        out_stream->codecpar->codec_tag = 0;
    }

    // 4. 打开输出 URL
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) 
    {
        ret = avio_open(&out_fmt_ctx->pb, output_url.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) 
        {
            Logger::getInstance()->error("avio_open failed (err={}, reason={}), output_url={}, input_url={}", 
                                        ret, ff_err2str(ret), output_url, input_url);
            enRetType = OPEN_URL_FAILED;
            goto end;
        }
    }

    // 5. 写文件头
    ret = avformat_write_header(out_fmt_ctx, nullptr);
    if (ret < 0) 
    {
        Logger::getInstance()->error("avformat_write_header failed (err={}, reason={}), output_url={}, input_url={}", 
                                    ret, ff_err2str(ret), output_url, input_url);
        enRetType = READ_HEADER_FAILED;
        goto end;
    }
    
    // 6. 循环读取数据包并写入输出
    while (m_PushState) 
    {
        int ret = av_read_frame(in_fmt_ctx, &pkt);

        if (ret < 0) 
        {
            av_packet_unref(&pkt);
            if (ret == AVERROR_EOF) 
            {
                Logger::getInstance()->info("[Relay] End of stream reached");
                break;
            } 
            else if (ret == AVERROR(EAGAIN)) 
            {
                Logger::getInstance()->warn("[Relay] Temporary read error ({}), retrying...", ret);
                av_usleep(10000);
                continue;
            } 
            else 
            {
                char errbuf[256]; 
                av_strerror(ret, errbuf, sizeof(errbuf));
                Logger::getInstance()->error("[Relay] Read frame failed: {}", errbuf);
                break;
            }
        }

        //线程处理防止阻塞
        if (bUpdateState)
        {
            bUpdateState = false;
            if(m_stPushStreamInfoNow.play_state == 0)
            {
                std::thread([this]() {
                    OnStatusCallBack(m_stPushStreamInfoNow);
                }).detach(); // detach 后独立线程运行，不阻塞主循环
            }    
        }

                
        // 只处理第一个视频流和第一个音频流
        if (pkt.stream_index != video_index && pkt.stream_index != audio_index) {
            av_packet_unref(&pkt);
            continue;
        }

        int out_index = stream_mapping[pkt.stream_index];
        if (out_index < 0) {
            av_packet_unref(&pkt);
            continue;  // 跳过无关流
        }

        // 重新计算时间戳
        AVStream* in_stream  = in_fmt_ctx->streams[pkt.stream_index];
        AVStream* out_stream = out_fmt_ctx->streams[pkt.stream_index];

        //Logger::getInstance()->debug(" url: {},pkt. pkt.pts:{}, pkt.dts{}", input_url,pkt.pts,pkt.dts);
        // ==== Step 1. 基础坏包检测 ====
        if (pkt.size <= 0 || pkt.data == nullptr) {
            Logger::getInstance()->warn("url:{}, drop empty packet", input_url);
            av_packet_unref(&pkt);
            continue;
        }


        // DTS 递增修复
        if (pkt.dts != AV_NOPTS_VALUE) 
        {
            if (last_dts != AV_NOPTS_VALUE && pkt.dts <= last_dts) 
            {
                pkt.dts = last_dts + 1;
                //av_packet_unref(&pkt);
                //continue; // 丢掉这个包
            }
            last_dts = pkt.dts;
        }

        // PTS 不能小于 DTS
        if (pkt.pts != AV_NOPTS_VALUE && pkt.dts != AV_NOPTS_VALUE) 
        {
            if (pkt.pts < pkt.dts) 
            {
                pkt.pts = pkt.dts;
                //Logger::getInstance()->warn("url: {}, PTS adjusted to DTS={}", input_url, pkt.dts);
            }
        }

        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        ret = av_interleaved_write_frame(out_fmt_ctx, &pkt);
        if (ret < 0) 
        {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            Logger::getInstance()->error("[Relay] Failed to write frame: {}, input_url:{}", errbuf, input_url);
            if (ret == AVERROR(EPIPE) || ret == AVERROR_EOF) 
            {
                av_packet_unref(&pkt);
                break;
            }
        }

        av_packet_unref(&pkt);
    }

    Logger::getInstance()->info("end out frame input_url:{}", input_url);

    // 7. 写文件尾并清理资源
    av_write_trailer(out_fmt_ctx);

end:
    stream_mapping.clear();
    // 3. 释放输出上下文
    if (out_fmt_ctx) {
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE) && out_fmt_ctx->pb) {
            avio_closep(&out_fmt_ctx->pb); // 推荐用 avio_closep 自动置空
        }
        avformat_free_context(out_fmt_ctx);
        out_fmt_ctx = nullptr;
    }

    // 4. 释放输入上下文
    if (in_fmt_ctx) {
        avformat_close_input(&in_fmt_ctx); // 内部会 free 并置空
    }

    // 5. 释放字典
    if (options) {
        av_dict_free(&options);
        options = nullptr;
    }

    return enRetType;
}

bool StreamRelay::push_stream(const std::string& input_url, const std::string& output_url) 
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
        std::cerr << "Cannot open input: " << input_url << std::endl;
        av_dict_free(&options);
        return false;
    }
    
    av_dict_free(&options);

    if (avformat_find_stream_info(in_fmt_ctx, nullptr) < 0) 
    {
        std::cerr << "Cannot find stream info" << std::endl;
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
        std::cerr << "No video stream found" << std::endl;
        avformat_close_input(&in_fmt_ctx);
        return false;
    }

    // 输出
    AVFormatContext* out_fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&out_fmt_ctx, nullptr, "flv", output_url.c_str()) < 0) 
    {
        std::cerr << "Cannot create output context" << std::endl;
        avformat_close_input(&in_fmt_ctx);
        return false;
    }

    // 视频解码器
    const AVCodec* dec = avcodec_find_decoder(in_fmt_ctx->streams[video_index]->codecpar->codec_id);
    if (!dec) 
    {
        std::cerr << "Cannot find video decoder" << std::endl;
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) 
    {
        std::cerr << "Cannot allocate video decoder context" << std::endl;
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    if (avcodec_parameters_to_context(dec_ctx, in_fmt_ctx->streams[video_index]->codecpar) < 0) 
    {
        std::cerr << "Cannot copy parameters to video decoder context" << std::endl;
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    if (avcodec_open2(dec_ctx, dec, nullptr) < 0) 
    {
        std::cerr << "Cannot open video decoder" << std::endl;
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    // 视频编码器
    const AVCodec* video_encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!video_encoder) 
    {
        std::cerr << "Cannot find H264 encoder" << std::endl;
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    AVStream* out_video_stream = avformat_new_stream(out_fmt_ctx, video_encoder);
    if (!out_video_stream) 
    {
        std::cerr << "Cannot create output video stream" << std::endl;
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }
    
    AVCodecContext* video_enc_ctx = avcodec_alloc_context3(video_encoder);
    if (!video_enc_ctx) {
        std::cerr << "Cannot allocate video encoder context" << std::endl;
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
        std::cerr << "Cannot open video encoder" << std::endl;
        avcodec_free_context(&video_enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt_ctx);
        avformat_free_context(out_fmt_ctx);
        return false;
    }

    if (avcodec_parameters_from_context(out_video_stream->codecpar, video_enc_ctx) < 0) 
    {
        std::cerr << "Cannot copy parameters to output video stream" << std::endl;
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
        } else 
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

void StreamRelay::OperationStream(std::string  strInputStream, std::string strOutputStream)
{
    AVFormatContext *fmt_ctx = nullptr;
    int video_stream_index ,audio_stream_index= -1;
    std::string strAudioInfo,strVideoInfo;

    std::cout << "[INFO] 开始拉流: " << strInputStream  << std::endl;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "stimeout", "5000000", 0);    // 5秒超时
    av_dict_set(&options, "buffer_size", "1024000", 0); // 增大缓冲

    TimeoutData timeout_data;
    timeout_data.timeout_ms = 10000;
    timeout_data.start_time = std::chrono::steady_clock::now();
    AVIOInterruptCB int_cb = {interrupt_cb, &timeout_data};

    fmt_ctx = avformat_alloc_context();
    fmt_ctx->interrupt_callback = int_cb;

   // 打开输入流
    int ret = avformat_open_input(&fmt_ctx, strInputStream.c_str(), nullptr, &options);
    if (ret < 0) 
    {
        return;
    }

    // 获取流信息
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) 
    {
        avformat_close_input(&fmt_ctx);
        return;
    }

    //av_dump_format(fmt_ctx, 0, strInputStream.c_str(), 0);

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
        avformat_close_input(&fmt_ctx);
        return;
    }

    if (audio_stream_index == -1) 
    {
        avformat_close_input(&fmt_ctx);
        return;
    }

    // ---------------- 视频解码器 ----------------
    AVCodecParameters* video_codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
    const AVCodec* video_codec = avcodec_find_decoder(video_codecpar->codec_id);
    if (!video_codec) 
    {
        std::cerr << "[ERROR] 未找到视频解码器, codec_id = " << video_codecpar->codec_id << std::endl;
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVCodecContext* video_codec_ctx = avcodec_alloc_context3(video_codec);
    if (!video_codec_ctx) 
    {
        avformat_close_input(&fmt_ctx);
        return;
    }

    ret = avcodec_parameters_to_context(video_codec_ctx, video_codecpar);
    if (ret < 0) 
    {
        avcodec_free_context(&video_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    ret = avcodec_open2(video_codec_ctx, video_codec, nullptr);
    if (ret < 0) 
    {
        avcodec_free_context(&video_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    // ---------------- 音频解码器 ----------------
    AVCodecParameters* audio_codecpar = fmt_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec* audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
    if (!audio_codec) 
    {
        avcodec_free_context(&video_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVCodecContext* audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    if (!audio_codec_ctx) 
    {
        avcodec_free_context(&video_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    ret = avcodec_parameters_to_context(audio_codec_ctx, audio_codecpar);
    if (ret < 0) 
    {
        avcodec_free_context(&video_codec_ctx);
        avcodec_free_context(&audio_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    ret = avcodec_open2(audio_codec_ctx, audio_codec, nullptr);
    if (ret < 0) 
    {
        avcodec_free_context(&video_codec_ctx);
        avcodec_free_context(&audio_codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    // 准备读取帧
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    while (av_read_frame(fmt_ctx, pkt) >= 0) 
    {
        if (pkt->stream_index == video_stream_index) 
        {
            // 视频解码
            ret = avcodec_send_packet(video_codec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "视频发送包失败: " << ret << std::endl;
                av_packet_unref(pkt);
                continue;
            }

            while (avcodec_receive_frame(video_codec_ctx, frame) == 0) 
            {
                std::cout << "[VIDEO] Frame #" 
                        << " 分辨率: " << frame->width << "x" << frame->height
                        << " 格式: " << av_get_pix_fmt_name((AVPixelFormat)frame->format)
                        << std::endl;

                // 这里可以进行后续视频处理或转码
                av_frame_unref(frame);
            }
        } 
        else if (pkt->stream_index == audio_stream_index) 
        {
            // 音频解码
            ret = avcodec_send_packet(audio_codec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "音频发送包失败: " << ret << std::endl;
                av_packet_unref(pkt);
                continue;
            }

            while (avcodec_receive_frame(audio_codec_ctx, frame) == 0) 
            {
                std::cout << "[AUDIO] Frame #" 
                        << " 样本数: " 
                        << frame->nb_samples
                        << " 格式: " << frame->format
                        << std::endl;

                // 这里可以进行后续音频处理或转码
                av_frame_unref(frame);
            }
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
