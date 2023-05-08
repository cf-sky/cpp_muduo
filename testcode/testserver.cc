#include <mymuduo/TcpServer.h>
#include <mymuduo/Logger.h>

#include <string>
#include <functional>

class EchoServer
{
public:
    EchoServer(EventLoop *loop,
            const InetAddress& addr,
            const std::string& name)
            :server_(loop, addr, name)
            ,loop_(loop)
    {
        //注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1)
        );
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
        );
        //设置合适的loop线程数量
        server_.setThreadNum(3);
    }
    void start()
    {
        server_.start();
    }
private:
    //连接建立或者断开回调
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
        }
    }

    //可读事件回调
    void onMessage(const TcpConnectionPtr& conn, Buffer *buf, Timestamp time)
    {
        std::string msg = buf->retrieveAllAsString();
        conn->send(msg);
        conn->shutdown();//关闭写端，底层就会相应EPOLLHUP事件，会执行closeCallback_
    }
    EventLoop* loop_;
    TcpServer server_;
};

int main()
{
    EventLoop loop;
    InetAddress addr(8000);
    /**
     * 当定义EchoServer的时候，里面会创建TcpServer对象，相当于要创建Acceptor对象，
     * Acceptor相当于要创建non-blocking的listenfd，然后create,bind
     */
    EchoServer server(&loop, addr, "EchoServer-01");
    /**
     * 进入listen状态，创建loopthread，把listenfd打包成acceptChannel，然后把它注册在mainloop上 
     * mainloop主要接收新用户连接，有新用户连接了，用轮询算法选择一个subloop进行connection的分发
     */
    server.start(); 
    loop.loop();//启动mainloop的底层poller
    return 0;
}