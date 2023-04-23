#ifndef __WEBSOCKETCLIENT_HPP__
#define __WEBSOCKETCLIENT_HPP__
#include <iostream>
#include <string>
#include <thread>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/common/thread.hpp>
#include <boost/asio.hpp>
using client = websocketpp::client<websocketpp::config::asio_client>;

class WebSocketClient {
    public:
        WebSocketClient(const std::string& uri) : uri_(uri), isConnected_(false)
        {
            // 初始化客户端
            client_.init_asio();
            //内部打印开关关闭方法
            client_.set_access_channels(websocketpp::log::alevel::none);
            client_.set_error_channels(websocketpp::log::elevel::none);
            // 注册事件处理程序
            client_.set_message_handler(std::bind(&WebSocketClient::on_message, this, std::placeholders::_1, std::placeholders::_2));
            client_.set_open_handler(std::bind(&WebSocketClient::on_open, this, std::placeholders::_1));
           	client_.set_close_handler(std::bind(&WebSocketClient::on_close, this, std::placeholders::_1));

        }
        ~WebSocketClient()
        {
            disconnect();
        }

        void connect()
        {
            if (!isConnected_)
            {
                websocketpp::lib::error_code errorCode;
                std::cout << "uri = " << uri_ << std::endl;
                // 尝试连接到指定的URI
                client::connection_ptr connection = client_.get_connection(uri_, errorCode);

                if (errorCode)
                {
                    std::cout << "connect to" << uri_ << "failure: " << errorCode.message() << std::endl;
                    return;
                }

                // 开始连接
                client_.connect(connection);
                // 启动网络线程async
                //clientThread_ = websocketpp::lib::make_shared<websocketpp::lib::thread>(&websocketpp::client<websocketpp::config::asio_client>::run, &client_);
				std::thread th([&]() {
						client_.run();//block,async
						});
				th.detach();
                // 等待连接完成
				std::unique_lock<std::mutex> lock(mutex_);
                while (!isConnected_)
                {
                    cv_.wait(lock);
                }
            }
        }

        void disconnect()
        {
            if (isConnected_) 
			{
                // 关闭连接
                client_.close(connectionHandle_, websocketpp::close::status::normal, "closing");
                // 等待连接关闭
                std::unique_lock<std::mutex> lock(mutex_);
                while (isConnected_) 
				{
                    cv_.wait(lock);
                }
        	}
		}

        bool send(const std::string& message)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            int status = isConnected_;
            lock.unlock();
            if (status)
            {
                // 发送消息
                client_.send(connectionHandle_, message, websocketpp::frame::opcode::text);
                return true;
            }
            else
            {
                return false;
            }
        }

        bool receive(std::string& message)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            int status = isConnected_;
            lock.unlock();
            if (status)
            {
                // 等待接收消息
                std::unique_lock<std::mutex> lock(mutex_);
                while (receivedMessages_.empty())
                {
                    cv_.wait(lock);
                }

                // 取出消息队列中的第一个消息
                message = receivedMessages_.front();
                receivedMessages_.pop();
                return true;
            }
            else
            {
                return false;//连接未建立成功
            }
        }

    private:

        std::string uri_;
        client client_;
        websocketpp::connection_hdl connectionHandle_;
        bool isConnected_;
        std::mutex mutex_;
        std::condition_variable cv_;
        // 存储接收到的消息
        std::queue<std::string> receivedMessages_;

        // 事件处理程序
        void on_message(websocketpp::connection_hdl connectionHandle, client::message_ptr message)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            receivedMessages_.push(message->get_payload());
            cv_.notify_all();
        }
        void on_open(websocketpp::connection_hdl connectionHandle)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            connectionHandle_ = connectionHandle;
            isConnected_ = true;
            cv_.notify_all();
        }

        void on_close(websocketpp::connection_hdl connectionHandle)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            isConnected_ = false;
            cv_.notify_all();
        }
};
#endif
