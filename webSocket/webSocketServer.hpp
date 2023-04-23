#ifndef __WEBSOCKETSERVER_HPP__
#define __WEBSOCKETSERVER_HPP__
#include <iostream>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <map>
#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unistd.h>

/*
该类实现以下功能：
作为一个websocket服务器，支持高并发处理多个客户端连接，实现与多个客户端的交互
WebSocketServer 构造函数接受一个字符串参数，这个参数用来指定websocket url路径，只处理此路径的消息
start 成员函数接受一个参数，指定websocket监听端口，同时开启服务，此函数会阻塞。
stop 成员函数关闭websocket服务，释放资源
send 成员函数支持向指定连接发送消息
set_path 函数可以改变websocket 处理的url路径
boardcast 函数向服务器所有连接发送消息

m_message_queues为每个连接维护一个队列，配合条件变量m_cv实现消息队列


m_connections 为每个连接维护一个int参数，可以修改这个参数为其他类型如结构体，用来给每个连接传递特定的参数，此处这个参数代表链接是否正常

*/
typedef websocketpp::server<websocketpp::config::asio> server;
using namespace boost::asio;

struct CompareConnectionHdl
{
    bool operator()(const websocketpp::connection_hdl& lhs,
                    const websocketpp::connection_hdl& rhs) const
    {
        return lhs.lock().get() < rhs.lock().get();
    }
};

class WebSocketServer
{
public:
    WebSocketServer(const std::string& path = "")
        : m_path(path), _running(0)
    {
        //debug log switch
        m_server.set_access_channels(websocketpp::log::alevel::none);
        m_server.set_error_channels(websocketpp::log::elevel::none);
        m_server.set_open_handler(std::bind(   &WebSocketServer::on_open, this, std::placeholders::_1));
        m_server.set_message_handler(std::bind(&WebSocketServer::on_message, this, std::placeholders::_1, std::placeholders::_2));
        m_server.set_close_handler(std::bind(   &WebSocketServer::on_close, this, std::placeholders::_1));
    }

    void start(int port)
    {
        m_server.init_asio();
        m_server.listen(ip::tcp::endpoint(ip::tcp::v4(),port));
        // start the server accept loop
        m_server.start_accept();
        // start the ASIO io_service run loop
        _running = 1;
        #if 1//how many clients are connecting to server?
        std::thread debugTh([&, this]() {
            int index = 0;
            while(1)
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                std::cout << "current link nums: " << m_connections.size() << std::endl;
                lock.unlock();
                sleep(3);
            }
        });

        debugTh.detach();
        #endif
        m_server.run();//block
    }

    void stop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        //set running status
        _running = 0;
		//notify all clients
        for(auto &conn : m_connections)
        {
            m_cv.notify_all();
        }
        //wake up all condition variable wait
        lock.unlock();
        //waiting clients's thread exit
        usleep(1000);
        //close server
        m_server.stop();
        //clear container
        m_connections.clear();
        m_message_queues.clear();
    }

    void send(websocketpp::connection_hdl hdl, const std::string& msg)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(0 == _running)
            return;

        // send msg to one client
        m_message_queues[hdl].push(msg);
        // notify all clients
        m_cv.notify_all();
    }

    void set_path(const std::string& path)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_path = path;
    }
	//send message to all clients
    bool boardcast(std::string msg)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        if(0 == _running)
            return false;

        for(auto &conn : m_connections)
        {
            {
                m_message_queues[conn.first].push(msg);
                m_cv.notify_all();
            }
        }
        return true;
    }

private:
    server m_server;
    std::map<websocketpp::connection_hdl, int, CompareConnectionHdl> m_connections;
    std::map<websocketpp::connection_hdl, std::queue<std::string>, CompareConnectionHdl> m_message_queues;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    //connection need verify m_path and (ipAddr or deviceId or systemId), use constructor or method to init them.
    std::string m_path;
    int _running;
    void on_open(websocketpp::connection_hdl hdl)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        // get url
        std::string request_path = m_server.get_con_from_hdl(hdl)->get_uri()->get_resource();
        // disable client's wrong url
        if (request_path != m_path)
        {
            m_server.send(hdl, "wrong websocket url path", websocketpp::frame::opcode::text);
            m_server.close(hdl, websocketpp::close::status::policy_violation, "Invalid request path");
            return;
        }
        //init connection status
        m_connections[hdl] = 1;
        m_message_queues[hdl] = std::queue<std::string>();
        // create a new thread for the new client
        std::thread t([this, hdl]() {
            while (true)
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                // wait a new message from the queue.if condition is false, process will be blocked and release the lock here, otherwise get the lock and continue running.
                m_cv.wait(lock, [this, hdl]() {
                        return (!m_connections[hdl] || !m_message_queues[hdl].empty() || !_running);
                });

                if(!_running)
                {
                    std::cout << "service has been stopped, thread exit!!!" << std::endl;
                    m_server.close(hdl, websocketpp::close::status::policy_violation, "server closed");
                    lock.unlock();
                    return ;
                }
                if(!m_connections[hdl])
                {
                    std::cout << "link has been closed\n";
                    // delete an element from map
                    m_connections.erase(hdl);
                    // delete an element from map
                    m_message_queues.erase(hdl);
                    return ;
                }
                // get one msg from queue
                std::string msg = m_message_queues[hdl].front();
                m_message_queues[hdl].pop();

                //release lock
                lock.unlock();

                try
                {
                    // send message to client
                    m_server.send(hdl, msg, websocketpp::frame::opcode::text);
                }
                catch (websocketpp::exception const & e)
                {
                    std::cerr << "WebSocketServer::on_open send exception: " << e.what() << std::endl;
                }
            }
            std::cout << "thread exit\n";
        });
        t.detach();
        std::cout << "create a new thread\n";
    }
    //callback, when data arrives, this function will be called, the first arg is the connection handle, the second arg is the message.
    void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        std::string payload =  msg->get_payload();
        std::cout << "recv msg: " << payload << std::endl;
        {
        //  m_connections[hdl] = payload;
        }

    }
    //callback, when a connection closed, this function will be called.
    void on_close(websocketpp::connection_hdl hdl)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_connections[hdl] = 0;
        m_cv.notify_all();
    }
};
#endif

