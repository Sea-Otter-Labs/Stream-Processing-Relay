
#include <iostream>
#include "Logger.h"
#include "StreamManage.h"
#include "HttpServer.h"

int main(int argc, char** argv)
{
    Logger::init(Logger::Level::Debug, "./logs", 10*1024*1024, 5);
    StreamManage server;
    server.Start();
    return 0;
}