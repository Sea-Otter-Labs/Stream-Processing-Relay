#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <curl/curl.h>
#include <mysql/mysql.h>
#include <thread>
#include <mutex>
#include <atomic>
#include "StreamRelay.h"
#include "httplib.h"

// ---------------------
// JSON → Struct 适配
// ---------------------

inline void from_json(const json& j, EncoderProfile& e) {
    if (j.contains("id")) j.at("id").get_to(e.id);
    if (j.contains("codec")) j.at("codec").get_to(e.codec);
    if (j.contains("width")) j.at("width").get_to(e.width);
    if (j.contains("height")) j.at("height").get_to(e.height);
    if (j.contains("bitrate_kbps")) j.at("bitrate_kbps").get_to(e.bitrate_kbps);
    if (j.contains("fps")) j.at("fps").get_to(e.fps);
}

inline void from_json(const json& j, OutputTarget& o) {
    if (j.contains("ip")) j.at("ip").get_to(o.ip);
    if (j.contains("protocol")) j.at("protocol").get_to(o.protocol);
    if (j.contains("encoder_id")) j.at("encoder_id").get_to(o.encoder_id);
}

inline void from_json(const json& j, StreamTask& t) {
    if (j.contains("encoders")) j.at("encoders").get_to(t.encoders);
    if (j.contains("outputs")) j.at("outputs").get_to(t.outputs);
}


class StreamManage
{
public:
    StreamManage();
    ~StreamManage();
    void Start();

private:
    std::map<int, std::shared_ptr<StreamRelay>> activePrograms; 

    bool m_bDbState = false;
    int m_target_matching_id = 0;
    std::mutex m_updateMutex;
    std::condition_variable m_cvUpdate;
    bool m_isUpdating = false;      // 是否正在更新

    std::map<std::string , int> m_mapStreamCallbackNum;
    void StartHttpServer(httplib::Server &svr);
    std::vector <OutPutStreamInfo> m_veStreamDbData;
    std::vector <OutPutStreamInfo> GetSqlDbData(int target_matching_id = 0);//获取数据
    StreamTask GetStreamTask(std::string strPath);
    void WriteSqlDbData(const OutPutStreamInfo &stOutputStreamInfo);//写入数据
    
};
