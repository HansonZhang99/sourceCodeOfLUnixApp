#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <iostream>
#include <time.h>
#include <string.h>
#include <errno.h>
#include "lock.h"
#include <queue>
/*
mind:日志类对外应该只提供日志输出接口，调用接口可以将日志输出到标准输出或者写入日志文件
调用者不需要常规的：创建一个日志对象，然后调用对象方法去实现功能。
只需要调用类特定的初始化接口，就可以使用相应的日志输出接口。
初始化接口应该提供一些选项：
1.日志输出位置：文件/标准输出
2.输出到文件时：同步写入/异步写入
3.每条日志的格式：应该包含常规的[时间][文件][函数][行]:log
4.日志文件命名：日志文件命名和时间绑定
5.日志文件限制：应该限制单个日志文件大小，以及超过此大小后需要重新生成新的日志文件，并继续记录日志
6.系统崩溃时日志文件完整性：日志类不具备系统崩溃检测能力，在每次记录完后调用fsync尽可能写入文件。崩溃对异步写入影响较大
*/

#define LOGGER_DEBUG 0
#define LOGGER_INFO 1
#define LOGGER_WARNING 2
#define LOGGER_ERROR 3

#define dev_debug(level, format, ...) \
do {\
    logger::getInstance()->writeLog(level, __FILE__, __FUNCTION__, __LINE__, format, ##__VA_ARGS__);\
}while(0);

#define LOG_DEBUG(arg...) dev_debug(LOGGER_DEBUG, ##arg)
#define LOG_INFO(arg...) dev_debug(LOGGER_INFO, ##arg)
#define LOG_WARN(arg...) dev_debug(LOGGER_WARNING, ##arg)
#define LOG_ERROR(arg...) dev_debug(LOGGER_ERROR, ##arg)

class logger
{
    private:
        logger();//构造函数私有，不允许构造，使用静态对象
        virtual ~logger();//私有虚析构函数，支持派生，限制此类的对象不能是栈对象
        void *asyncWriteLog();//日志异步写
    private:
        std::string dirName;//日志文件位置
        std::string logName;//日志文件名
        int maxLogLine;
        int maxLogBufSize;//单条日志最大长度
        long long curLineCount;
        FILE *fp;
        char *logBuf;//temp，用于将单条日志输出或者放入队列
        std::queue<std::string> logQueue;//日志缓冲队列
        int maxQueueSize; //缓冲队列元素个数
        bool isAsync; //是否异步记录日志
        locker mutex;
    public:
        static logger *getInstance()//返回一个静态实例
        {
            static logger instance;
            return &instance;
        }
        static void *asyncLogThread(void *args)//异步记录工作线程
        {
            logger::getInstance()->asyncWriteLog();
        }
        bool init(const char *fileName, unsigned int logOutput = 1, unsigned int logBufSize = 8192, unsigned int logLine = 50000000, unsigned int queueSize = 0);
        void writeLog(int level, const char *fileName, const char *func, const int line, const char *format, ...);

};

#endif