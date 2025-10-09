#include "StreamManage.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include "Logger.h"
#include "StreamTranscoder.h"

namespace fs = std::filesystem;
static std::mutex g_mutex;

// Logger::getInstance()->debug("This is debug message");
// Logger::getInstance()->info("This is info message");
// Logger::getInstance()->warn("This is warning");
// Logger::getInstance()->error("This is error");

StreamManage::StreamManage()
{
}

StreamManage::~StreamManage()
{
}

std::string to_ascii(const std::string& input) {
    std::string output;
    for (unsigned char c : input) {
        if (c < 128) {
            output.push_back(c);
        }
        // 否则跳过（相当于 unidecode 简化版）
    }
    return output;
}

bool clearHlsDir(const std::string& path = "/hls/") 
{
    try {
        if (!fs::exists(path)) {
            std::cerr << "[WARN] Directory does not exist: " << path << std::endl;
            return false;
        }

        for (auto& entry : fs::directory_iterator(path)) {
            std::error_code ec;
            fs::remove_all(entry.path(), ec);  // 递归删除（子目录也清掉）
            if (ec) {
                std::cerr << "[ERROR] Failed to remove: " 
                          << entry.path() << " - " << ec.message() << std::endl;
                return false;
            }
        }

        std::cout << "[INFO] Directory cleared: " << path << std::endl;
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] " << e.what() << std::endl;
        return false;
    }
}

bool clearHlsAllfile(const std::string& stream_name = "",
                  const std::string& path = "/hls/live",
                  int expire_seconds = 90) // 过期时间，秒，0 表示立即删除
{
    try
    {
        if (!fs::exists(path))
        {
            Logger::getInstance()->warn("HLS directory not found: {}", path);
            return false;
        }

        auto now = std::chrono::system_clock::now();
        bool removed_any = false;

        // 如果指定了 stream_name，构建专用正则；否则匹配所有 .m3u8/.ts
        std::regex pattern(
            stream_name.empty()
                ? R"(^.+(\.m3u8|-\d+\.ts)$)"
                : "^" + stream_name + R"((\.m3u8|-\d+\.ts)$)"
        );

        for (auto& entry : fs::directory_iterator(path))
        {
            if (!entry.is_regular_file()) continue;

            const auto& file = entry.path();
            std::string filename = file.filename().string();

            if (!std::regex_match(filename, pattern)) continue;

            bool should_remove = true;

            if (expire_seconds > 0)
            {
                // 文件最后修改时间 → 转换到 system_clock
                auto ftime = fs::last_write_time(file);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - decltype(ftime)::clock::now() + now);

                auto age = std::chrono::duration_cast<std::chrono::seconds>(now - sctp).count();
                if (age < expire_seconds)
                {
                    should_remove = false;
                }
            }

            if (should_remove)
            {
                std::error_code ec;
                fs::remove(file, ec);
                if (ec)
                {
                    Logger::getInstance()->error("Failed to remove {}: {}", file.string(), ec.message());
                }
                else
                {
                    removed_any = true;
                }
            }
        }

        if (removed_any)
        {
            if (stream_name.empty())
                Logger::getInstance()->info("Expired HLS files cleared (all streams)");
            else
                Logger::getInstance()->info("Expired HLS files cleared for stream: {}", stream_name);
        }

        return true;
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->error("Exception in clearHlsfile ({}): {}", 
                                     stream_name.empty() ? "ALL" : stream_name, e.what());
        return false;
    }
}

void StreamManage::Start()
{
    std::map<std::string, std::shared_ptr<StreamRelay>> activePrograms; 

    //clearHlsDir();

    while (false)
    {
        /*
            管理模块-》推流模块
            1.查询数据库或者配置文件决定拉流地址 编码格式 推流地址等
            2.使用多进程模块 对拉流推流模块进行管理

            拉流-》解码-》编码-》推流
            1.拉一路流解码一次
            2.需要多个格式，编码多次
            3.推流地址不一样 推多路流
        */

        auto veStreamData = GetSqlDbData(); // 返回 std::vector<OutPutStreamInfo>
        Logger::getInstance()->info("Connected to GetSqlDbData. size:{}",veStreamData.size());

        if (!veStreamData.empty())
        {
            // 遍历数据库中的节目
            
            for (int i = 0; i < veStreamData.size() ;i++)
            {
                auto& program = veStreamData[i];
                auto key = program.target_matching;

                if (activePrograms.find(key) == activePrograms.end())
                {
                    // 新增节目
                    auto relay = std::make_shared<StreamRelay>(program);
                    
                    relay->setStatusCallback([this](const OutPutStreamInfo& info)
                    {
                        //回调更新数据库
                        WriteSqlDbData(info);
                    });

                    relay->setFailCallback([this](const std::string& url)
                    {
                        //回调更新
                        std::lock_guard<std::mutex> lock(g_mutex);
                        m_mapStreamCallbackNum[url]++;

                        std::ofstream countFile("./logs/stream_error_count.log", std::ios::trunc);
                        if (countFile.is_open())
                        {
                            for (const auto &kv : m_mapStreamCallbackNum)
                            {
                                countFile << kv.first << " : " << kv.second << std::endl;
                            }
                            countFile.close();
                        }
                    });

                    if (relay->Start()) 
                    {
                        activePrograms[key] = relay;
                        Logger::getInstance()->info("启动新节目:{}",key);
                    }
                }
                else
                {
                    // 节目已存在，检查源是否有变化
                    activePrograms[key]->Update(program);
                }
            
            }

            // 清理数据库已删除的节目
            for (auto it = activePrograms.begin(); it != activePrograms.end(); )
            {
                auto found = std::find_if(
                    veStreamData.begin(), veStreamData.end(),
                    [&](const OutPutStreamInfo& p) {
                        return p.target_matching == it->first;
                    }
                );

                if (found == veStreamData.end()) 
                {
                    it->second->Stop();
                    Logger::getInstance()->info("节目移除:{}",it->first);
                    it = activePrograms.erase(it);
                } 
                else 
                {
                    ++it;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(120));
    }


    while (true)
    {
        //清理过期文件
        clearHlsAllfile();
        //读取配置

        auto veStreamData = GetSqlDbData(); // 返回 std::vector<OutPutStreamInfo>
        Logger::getInstance()->info("Connected to GetSqlDbData. size:{}",veStreamData.size());

        if (!veStreamData.empty())
        {
            // 遍历数据库中的节目
            
            for (int i = 0; i < veStreamData.size() ;i++)
            {
                auto& program = veStreamData[i];
                auto key = program.target_matching;

                if (activePrograms.find(key) == activePrograms.end())
                {
                    // 新增节目
                    auto relay = std::make_shared<StreamRelay>(program);
                    relay->setStatusCallback([this](const OutPutStreamInfo& info)
                    {
                        //回调更新数据库
                        WriteSqlDbData(info);
                    });

                    relay->setFailCallback([this](const std::string& url)
                    {
                        //回调更新
                        std::lock_guard<std::mutex> lock(g_mutex);
                        m_mapStreamCallbackNum[url]++;

                        std::ofstream countFile("stream_error_count.log", std::ios::trunc);
                        if (countFile.is_open())
                        {
                            for (const auto &kv : m_mapStreamCallbackNum)
                            {
                                countFile << kv.first << " : " << kv.second << std::endl;
                            }
                            countFile.close();
                        }
                    });

                    if (relay->Start()) 
                    {
                        activePrograms[key] = relay;
                        Logger::getInstance()->info("启动新节目:{}",key);
                    }
                }
                else
                {
                    // 节目已存在，检查源是否有变化
                    activePrograms[key]->Update(program);
                }
            
            }

            // 清理数据库已删除的节目
            for (auto it = activePrograms.begin(); it != activePrograms.end(); )
            {
                auto found = std::find_if(
                    veStreamData.begin(), veStreamData.end(),
                    [&](const OutPutStreamInfo& p) {
                        return p.target_matching == it->first;
                    }
                );

                if (found == veStreamData.end()) 
                {
                    it->second->Stop();
                    Logger::getInstance()->info("节目移除:{}",it->first);
                    it = activePrograms.erase(it);
                } 
                else 
                {
                    ++it;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(120));
    }

}

std::vector<OutPutStreamInfo> StreamManage::GetSqlDbData()
{
    auto stStreamTask = GetStreamTask("task.json");
    std::vector<OutPutStreamInfo> resultList;

    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        Logger::getInstance()->error("mysql_init failed");
        return resultList;
    }

    if (!mysql_real_connect(conn, SQL_HOST, SQL_USER, SQL_PASSWD, SQL_DBNAME, SQL_PORT, nullptr, 0)) 
    {
        Logger::getInstance()->error("Connection failed:{}", mysql_error(conn));
        mysql_close(conn);
        return resultList;
    }

    Logger::getInstance()->debug("Connected to database.");

    // 查询所需字段
    const char* query = "SELECT id, url, is_backup, priority, flow_score, resolution_type, play_state, target_matching_id, target_matching, stream_name_format "
        "FROM live_stream_sources "
        "WHERE is_del = 0 " 
        "AND target_matching_id >= 237 "
        "AND flow_score >= 60 " //质量
        "AND resolution_type < 17 ";//分辨率

    if (mysql_query(conn, query)) 
    {
        Logger::getInstance()->error("Query failed:{}", mysql_error(conn));
        mysql_close(conn);
        return resultList;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) 
    {
        Logger::getInstance()->error("mysql_store_result failed:{}", mysql_error(conn));
        mysql_close(conn);
        return resultList;
    }
    
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) 
    {
        StreamDbDataInfo dbInfo;
        dbInfo.id         = row[0] ? row[0] : "";
        dbInfo.url        = row[1] ? row[1] : "";
        dbInfo.is_backup  = row[2] ? atoi(row[2]) : 0;
        dbInfo.priority   = row[3] ? atoi(row[3]) : 0;
        dbInfo.flow_score   = row[4] ? atoi(row[4]) : 0;
        dbInfo.resolution_type   = row[5] ? atoi(row[5]) : 0;
        dbInfo.play_state = row[6] ? atoi(row[6]) : 0;
        int target_id     = row[7] ? atoi(row[7]) : -1;
        std::string target_name = row[8] ? row[8] : "";
        std::string stream_name_format = row[9] ? row[9] : "";

        // 查找是否已有该 target_matching_id 的 OutPutStreamInfo
        auto it = std::find_if(resultList.begin(), resultList.end(),
            [target_id](const OutPutStreamInfo& info) 
            {
                return info.target_matching_id == target_id;
            });

        if (it == resultList.end()) 
        {
            // 新节目
            OutPutStreamInfo outInfo;
            outInfo.target_matching_id     = target_id;
            outInfo.target_matching        = target_name;
            outInfo.stream_name_format     = stream_name_format + std::string(HD720_FORMAT);
            //outInfo.target_matching_format = "rtmp://127.0.0.1/live/" + stream_name_format + "_HD";
            outInfo.target_matching_format = "rtmp://127.0.0.1/live/" + outInfo.stream_name_format;
            
            outInfo.veStrDbDataInfo.push_back(dbInfo);
            outInfo.stStreamTask = stStreamTask; //当前情况任务同步
            resultList.push_back(outInfo);
        } 
        else 
        {
            // 已存在该节目，添加源
            it->veStrDbDataInfo.push_back(dbInfo);
        }
    }

    mysql_free_result(res);
    mysql_close(conn);

    return resultList;
}

StreamTask StreamManage::GetStreamTask(std::string strPath)
{
    StreamTask stStreamTask;

    try 
    {
        std::ifstream ifs(strPath);
        if (!ifs.is_open()) 
        {
            std::cerr << "[ERROR] Failed to open file: " << strPath << std::endl;
            return stStreamTask;
        }

        json j;
        ifs >> j; // 直接读取 JSON
        stStreamTask = j.get<StreamTask>();

    } 
    catch (const std::exception& e) 
    {
        std::cerr << "[ERROR] Failed to parse JSON file: " << e.what() << std::endl;
    }

    return stStreamTask;
}

void StreamManage::WriteSqlDbData(const OutPutStreamInfo &stOutputStreamInfo)
{
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) 
    {
        Logger::getInstance()->error("mysql_init failed");
        mysql_close(conn);
        return;
    }

    if (!mysql_real_connect(conn, SQL_HOST, SQL_USER, SQL_PASSWD, SQL_DBNAME, SQL_PORT, nullptr, 0)) 
    {
        Logger::getInstance()->error("Connection failed:{}", mysql_error(conn));
        mysql_close(conn);
        return ;
    }

    for (const auto& dbInfo : stOutputStreamInfo.veStrDbDataInfo)
    {
        // 拼接 SQL
        std::ostringstream oss;
        oss << "UPDATE live_stream_sources "
            << "SET play_state=" << dbInfo.play_state
            << " WHERE id='" << dbInfo.id << "';";

        std::string sql = oss.str();

        if (mysql_query(conn, sql.c_str()))
        {
            Logger::getInstance()->error("Update failed for id={},error:{}", dbInfo.id, mysql_error(conn));
        }
        // else
        // {
        //     Logger::getInstance()->info("Update success for id={}, dbInfo.play_state={}, target_matching_id={}",
        //          dbInfo.id, dbInfo.play_state,stOutputStreamInfo.target_matching_id);
        // }

    }

    mysql_close(conn);
}
