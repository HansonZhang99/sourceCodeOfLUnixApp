#ifndef __POSIXTHREADPOOL_H__
#define __POSIXTHREADPOOL_H__

#include <condition_variable> //条件变量
#include <mutex> //互斥锁
#include <vector>
#include <queue>
#include <unistd.h>
#include <iostream>
#include <exception>
/*
mind：采用posix pthread接口创建线程，也可以用c++ thread类代替。
线程池的优点是减少了创建和销毁线程带来的开销，缺点是线程池会长期占用一部分资源。
线程池维护多个线程，线程不断访问队列获取需要执行的工作，常用于http服务器为新连接提供服务。
线程池接受模板类，模板类需要实现process函数作为工作函数
*/
#define MAXTHREADS 128

template <typename T>
struct threadInfo
{
    T *self;
    int number;
};

template <typename T>
class posixThreadPool
{
    public:
        posixThreadPool(int number = 10);
        ~posixThreadPool();
        bool append(T *task);
    private:
        std::vector<pthread_t> workThread;//线程池
        std::queue<T *> workQueue;
        std::condition_variable condition;
        std::mutex mt;//mutex和条件变量绑定使用
        int stop;
    private:
        static void *worker(void *args);
        void run(int number);
};

template <typename T>
posixThreadPool<T>::posixThreadPool(int number) : stop(false)
{
    if(number <= 0 || number > MAXTHREADS)
    {
        throw std::exception();
    }
    for(int  i = 0; i < number; i++)
    {
        pthread_t tid;
        threadInfo<posixThreadPool> *info = new threadInfo<posixThreadPool>;
        info->self = this;
        info->number = i;
        int rv = pthread_create(&tid, NULL, worker, info);
        if(rv < 0)
        {
            throw std::exception();
        }
        else 
        {
            //pthread_detach(tid);
            workThread.push_back(tid);
        }
    }
}

template <typename T>
posixThreadPool<T>::~posixThreadPool()
{
    std::unique_lock<std::mutex> unique(mt);
    stop = true;
    unique.unlock();
    condition.notify_all();
    for(auto &tid : workThread)
    {
        pthread_join(tid, NULL);
        std::cout << "thread with tid " << tid << " has joined\n";
    }
}

template <typename T>
void *posixThreadPool<T>::worker(void *args)
{
    threadInfo<posixThreadPool> *p = (threadInfo<posixThreadPool> *)args;
    int number = p->number;
    if(p->self)
    {
        p->self->run(number);
    }
    std::cout << "thread " << number << " with tid " << pthread_self() << " exit\n";
    usleep(100000);
    if(p)
    {
        delete p;
    }
    return NULL;
}

template <typename T>
void posixThreadPool<T>::run(int number)
{
    while(!stop)
    {
        std::unique_lock<std::mutex> unique(mt);
        while(this->workQueue.empty())
        {
            if(stop)
            {
                unique.unlock();
                return ;
            }
            this->condition.wait(unique);
        }
        std::cout << "thread " << number << " is running\n";
        T *task = this->workQueue.front();
        this->workQueue.pop();
        if(task)
        {
            task->process();
        }

    }
}

template <typename T>
bool posixThreadPool<T>::append(T *task)
{
    if(task)
    {
        std::unique_lock<std::mutex> unique(mt);
        workQueue.push(task);
        unique.unlock();
        condition.notify_one();
    }
}
#endif