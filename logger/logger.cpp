#include "logger.h"
#include <stdio.h>

#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <iostream>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include "lock.h"
logger::logger()//构造函数私有，不允许构造，使用静态对象
{
    curLineCount = 0;
    isAsync = 0;
}


logger::~logger()//私有虚析构函数，支持派生，限制此类的对象不能是栈对象
{
    if(fp)
    {
        fclose(fp);
        fp = NULL;
    }
}
void *logger::asyncWriteLog()//日志异步写
{
    std::string singleLog;
    while(1)
    {
        mutex.lock();
        if(logQueue.empty())//是否需要在这里加锁
        {
            mutex.unlock();
            continue;
        }
        singleLog = logQueue.front();//取队首元素
        logQueue.pop();//弹出
        if(fp)
        {
            ++curLineCount;
            fputs(singleLog.c_str(), fp);
        }
        mutex.unlock();
    }
}
bool logger::init(const char *fileName, unsigned int logOutput, unsigned int logBufSize, unsigned int logLine, unsigned int queueSize)
{
    pthread_t tid;
    time_t nowTime;
    struct tm *p;
    char timeStr[64];
    char temp[128];
    FILE *file;
    char *str;
    DIR *dir;
    struct timeval tv;
    std::string logPathFileName;

    fp = NULL;
    logBuf = NULL;
    curLineCount = 0;
    maxLogBufSize = logBufSize;
    maxLogLine = logLine;
    maxQueueSize = queueSize;

    if(!fileName && logOutput != 1 && logOutput != 2)
    {
        fprintf(stderr, "no fileName and output is not stdout or stderr\n");
        return false;
    }

    logBuf = new char[maxLogBufSize];
    
    if(logOutput == 1)
    {
        isAsync = false;
        fp = stdout;
        return true;
    }
    else if(logOutput == 2)
    {
        isAsync = false;
        fp = stderr;
        return true;
    }

    if(maxQueueSize)//开启异步日志记录
    {
        isAsync = true;
        pthread_create(&tid, NULL, asyncLogThread, NULL);//异步日志处理线程，线程是类的成员函数，可以访问类成员，不需要this指针
        pthread_detach(tid);
    }
    
    memset(logBuf, 0x0, maxLogBufSize);
    memset(timeStr, 0x0, sizeof(timeStr));            
    memset(temp, 0x0, sizeof(temp));
    
    gettimeofday(&tv, NULL);
    nowTime = tv.tv_sec;
    p = localtime(&nowTime);
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d_%02d:%02d:%02d:%03d", p->tm_year + 1900, p->tm_mon + 1, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec, (int)tv.tv_usec / 1000);
    strncpy(temp, fileName, sizeof(temp));
    str = strrchr(temp, '/');
    if(str)
    {
        logName = str + 1;
        *str = '\0';
        dirName = temp;
        *str = '/';
    }
    else 
    {
        logName = temp;
    }
    dir = opendir(dirName.c_str());
    if(!dir)
    {
        memset(temp, 0x0, sizeof(temp));
        snprintf(temp, sizeof(temp), "mkdir -p %s", dirName.c_str());
        system(temp);
    }
    else 
    {
        closedir(dir);
    }
    logPathFileName = dirName + '/' + timeStr + "_" + logName;
    file = fopen(logPathFileName.c_str(), "a");
    if(!file)
    {
        fprintf(stderr, "fopen error :%s, errno = %d\n", strerror(errno), errno);
        delete []logBuf;
        return false;
    }
    fp = file;
    return true;
}
void logger::writeLog(int level, const char *fileName, const char *func, const int line, const char *format, ...)
{
    std::string logLevel;
    char timeStr[64];
    std::string logPathFileName;
    time_t nowTime;
    struct tm *p;
    FILE *file;
    struct timeval tv;
    switch(level)
    {
        case LOGGER_DEBUG:
            logLevel = "DEBUG";
            break;
        case LOGGER_INFO:
            logLevel = "INFO";
            break;
        case LOGGER_WARNING:
            logLevel = "WARNING";
            break;
        case LOGGER_ERROR:
            logLevel = "ERROR";
            break;
        default:
            logLevel = "DEBUG";
            break;
    }
    memset(timeStr, 0x0, sizeof(timeStr));
    gettimeofday(&tv, NULL);
    nowTime = tv.tv_sec;
    p = localtime(&nowTime);
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d_%02d:%02d:%02d:%03d", p->tm_year + 1900, p->tm_mon + 1, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec, (int)tv.tv_usec / 1000);
    
    mutex.lock();
    if(fp != stdout && fp != stderr && curLineCount >= maxLogLine)
    {
        curLineCount = 0;
        if(fp)
        {
            fflush(fp);
            fclose(fp);
            fp = NULL;
        }
        logPathFileName = dirName + '/' + timeStr + "_" + logName;
        file = fopen(logPathFileName.c_str(), "a");
        if(!file)
        {
            fprintf(stderr, "fopen error :%s, errno = %d\n", strerror(errno), errno);
            mutex.unlock();
            return ;
        }
        else 
        {
            fp = file;   
        }
    }
    mutex.unlock();
    
    va_list list;
    va_start(list, format);
    std::string logStr;
    int n = snprintf(logBuf, maxLogBufSize, "[%s][%s][%s][%s][%d]", logLevel.c_str(), timeStr, fileName, func, line);
    int m = vsnprintf(logBuf + n, maxLogBufSize - n, format, list);
    logBuf[m + n] = '\0';
    va_end(list);
 
    mutex.lock();
    if(isAsync && logQueue.size() < maxQueueSize)
    {
        logStr = logBuf;
        logQueue.push(logStr);
    }
    else if(fp) 
    {
        if(fp != stdout && fp != stderr)
            ++curLineCount;
        fputs(logBuf, fp);
    }
    mutex.unlock();
}