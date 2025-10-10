#pragma once
#include <nlohmann/json.hpp>

using json = nlohmann::json;

#define WEB_HOOK_STREAM_FAIL "https://open.larksuite.com/open-apis/bot/v2/hook/de65a92f-2039-4ce4-bfcb-df65bdad1523"
#define SQL_HOST "database-1.cn4csgi60ope.eu-west-3.rds.amazonaws.com"
#define SQL_USER "pplive"
#define SQL_PASSWD "pplive123$"
#define SQL_DBNAME "stream_manage"

#define HD720_FORMAT "_HD"
#define FHD1080_FROMAT "_FHD"

#define SQL_PORT 3306
#define HTTP_PORT 8877

enum class VideoResolutionType {
    UNKNOWN = 0,
    QQVGA,      // 160x120
    QVGA,       // 320x240
    NHD,        // 640x360
    VGA,        // 640x480
    SD480,      // 720x480
    SD576,      // 720x576
    SVGA,       // 800x600
    FWVGA,      // 854x480
    WSVGA,      // 1024x576
    XGA,        // 1024x768
    HD720,      // 1280x720
    WXGA,       // 1280x800
    WXGA_PLUS,  // 1366x768
    HD_PLUS,    // 1600x900
    FHD1080,    // 1920x1080
    DCI2K,      // 2048x1080
    QHD1440,    // 2560x1440
    RETINA2880, // 2880x1800
    QHD_PLUS,   // 3200x1800
    UHD4K,      // 3840x2160
    DCI4K,      // 4096x2160
    UHD5K,      // 5120x2880
    UHD8K,      // 7680x4320
    DCI8K,      // 8192x4320
    CUSTOM      // 非标准分辨率
};

// ID -> 节目名 需要节目转码列表
static const std::unordered_map<int, std::string> idToProgramName = 
{
    {237, "movistar la liga"},
    {238, "dazn la liga"},
    {239, "la liga smartbank tv"},
    {241, "movistar liga de campeones"},
    {242, "movistar deportes"},
    {243, "dazn f1"},
    {261, "MOVISTAR LA LIGA 3"},
    {299, "movistart +"},
    {300, "movistart +2"},
    {301, "MOVISTAR PRIMERA FEDERACION"},
    {302, "movistar la liga FHD"},
    {303, "dazn la liga FHD"},
    {304, "la liga smartbank tv FHD"},
    {306, "movistar liga de campeones FHD"},
    {307, "movistar deportes FHD"},
    {308, "dazn f1 FHD"},
    {326, "MOVISTAR LA LIGA 3 FHD"},
    {364, "movistart + FHD"},
    {365, "movistart +2 FHD"},
    {366, "MOVISTAR PRIMERA FEDERACION FHD"}
};

// 映射：字符串 -> 枚举类型
static const std::map<std::string, VideoResolutionType> resolutionMap = 
{
    {"160x120",   VideoResolutionType::QQVGA},
    {"320x240",   VideoResolutionType::QVGA},
    {"640x360",   VideoResolutionType::NHD},
    {"640x480",   VideoResolutionType::VGA},
    {"720x480",   VideoResolutionType::SD480},
    {"720x576",   VideoResolutionType::SD576},
    {"800x600",   VideoResolutionType::SVGA},
    {"854x480",   VideoResolutionType::FWVGA},
    {"1024x576",  VideoResolutionType::WSVGA},
    {"1024x768",  VideoResolutionType::XGA},
    {"1280x720",  VideoResolutionType::HD720},
    {"1280x800",  VideoResolutionType::WXGA},
    {"1366x768",  VideoResolutionType::WXGA_PLUS},
    {"1600x900",  VideoResolutionType::HD_PLUS},
    {"1920x1080", VideoResolutionType::FHD1080},
    {"2048x1080", VideoResolutionType::DCI2K},
    {"2560x1440", VideoResolutionType::QHD1440},
    {"2880x1800", VideoResolutionType::RETINA2880},
    {"3200x1800", VideoResolutionType::QHD_PLUS},
    {"3840x2160", VideoResolutionType::UHD4K},
    {"4096x2160", VideoResolutionType::DCI4K},
    {"5120x2880", VideoResolutionType::UHD5K},
    {"7680x4320", VideoResolutionType::UHD8K},
    {"8192x4320", VideoResolutionType::DCI8K},
};

enum StreamError {
    OPERATION_OK = 0,            // 成功
    OPEN_INPUT_FAILED = 1,       // 打开输入流失败
    STREAM_INFO_FAILED,          // 获取流信息失败
    FIND_VIDEO_STREAM_FAILED,    // 查找视频流失败
    FIND_AUDIO_STREAM_FAILED,    // 查找音频流失败
    VIDEO_DECODER_NOT_FOUND,     // 未找到视频解码器
    ALLOC_VIDEO_CTX_FAILED,      // 无法分配视频解码器上下文
    COPY_VIDEO_PARAMS_FAILED,    // 视频解码器参数拷贝失败
    OPEN_VIDEO_DECODER_FAILED,   // 打开视频解码器失败
    AUDIO_DECODER_NOT_FOUND,     // 未找到音频解码器
    ALLOC_AUDIO_CTX_FAILED,      // 无法分配音频解码器上下文
    COPY_AUDIO_PARAMS_FAILED,    // 音频解码器参数拷贝失败
    OPEN_AUDIO_DECODER_FAILED,   // 打开音频解码器失败
    READ_PACKET_FAILED,          // 获取流包失败
    DECODE_FRAME_FAILED,         // 解码失败
    READ_FRAME_FAILED,           // 读帧失败
    DTS_PTS_FAILED,              // 时间戳异常
    OPEN_URL_FAILED,             // 打开输出 URL异常
    READ_HEADER_FAILED,          // 写文件头异常

};

// 编码配置
struct EncoderProfile {
    std::string id;         // profile ID
    std::string codec;      // H264/H265/AAC/...
    int width = 1920;       // 分辨率
    int height = 1080;
    int bitrate_kbps = 2000;
    int fps = 25;
};

// 输出目标（推流到流媒体）
struct OutputTarget {
    std::string ip;             // 服务器地址
    std::string encoder_id;     // 对应哪个编码器
    std::string protocol;       // 协议 rtmp rtsp
};

// 整个任务描述
struct StreamTask {
    std::vector<EncoderProfile> encoders;  // 编码配置（可能多个）
    std::vector<OutputTarget> outputs;     // 输出配置（多个推流目标）
};

struct StreamDbDataInfo
{
    std::string id = "";//主键ID
    std::string url = "";//源地址
    int is_backup; //是否为备用源：0=主源，1=备用源
    int priority; //备用优先级
    int flow_score; //源质量分数
    int resolution_type;
    int play_state; //播放状态 1播放 0未播放
};

struct OutPutStreamInfo
{
    std::vector<StreamDbDataInfo> veStrDbDataInfo;//不同源地址
    int target_matching_id;//节目ID
    std::string target_matching;//节目名
    std::string stream_name_format;//格式化节目名
    std::string target_matching_format;//格式化节目名推流地址
    StreamTask stStreamTask; //编码推流任务
};

struct TimeoutData {
    int timeout_ms; // 超时时间（毫秒）
    std::chrono::steady_clock::time_point start_time;
};

// 回调函数
static int interrupt_cb(void* ctx) {
    TimeoutData* data = (TimeoutData*)ctx;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - data->start_time).count();
    if (elapsed > data->timeout_ms) {
        // 超时，返回非零表示中断
        return 1;
    }
    return 0; // 不中断
}