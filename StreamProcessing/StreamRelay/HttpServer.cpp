#include "HttpServer.h"

HttpServer::HttpServer()
{
}

HttpServer::~HttpServer()
{
}

size_t HttpServer::writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append((char*)contents, total);
    return total;
}

std::string HttpServer::Get(const std::string& host, int port, const std::string& path, const std::string& queryParams) 
{
    std::ostringstream url;
    url << host << ":" << port << path;
    if (!queryParams.empty()) {
        url << "?" << queryParams;
    }
    return request(url.str(), "", false);
}

std::string HttpServer::Post(const std::string& host, int port, const std::string& path, const std::string& postData) 
{
    std::ostringstream url;
    url << host << ":" << port << path;

    CURL* curl = curl_easy_init();
    if (!curl) return "Failed to init curl";

    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // HTTPS 时忽略证书
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) 
    {
        response = std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

std::string HttpServer::request(const std::string& url, const std::string& data, bool isPost) 
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "Failed to init curl";
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    if (isPost) 
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    }

    // HTTPS 时忽略证书（生产环境要改为严格验证）
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // 写回调函数
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) 
    {
        response = std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);
    return response;
}

bool HttpServer::sendLarkTextMessage(const std::string &webhookUrl, const std::string &message) 
{
    CURL *curl;
    CURLcode res;
    bool success = false;

    // 构造 JSON 数据
    std::string jsonData = R"({"msg_type":"text","content":{"text":")" 
                           + message + R"("}})";

    std::cout << "👉 即将发送的 JSON 数据:\n" << jsonData << "\n" << std::endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        std::string responseBody;

        curl_easy_setopt(curl, CURLOPT_URL, webhookUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());

        // 设置回调获取响应
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            std::cerr << "❌ curl_easy_perform() failed: " 
                      << curl_easy_strerror(res) << std::endl;
        } else {
            std::cout << "✅ HTTP 请求成功" << std::endl;
            std::cout << "🔙 Lark 响应内容:\n" << responseBody << std::endl;

            // 简单判断是否包含 "code\":0" 作为成功
            if(responseBody.find("\"code\":0") != std::string::npos) {
                success = true;
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

    return success;
}
