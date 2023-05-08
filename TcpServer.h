#pragma once

/**
 * 用户使用muduo库编写服务器程序
 */
#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

#include <functional>
#include <memory>
#include <atomic>
#include <unordered_map>

//对外的服务器编程使用的类
class TcpServer : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    // 预置两个是否重用port的选项
    enum Option
    {
        kNoReusePort,
        kReusePort,
    };

    TcpServer(EventLoop* loop,
                const InetAddress &listenAddr,
                const std::string& nameArg,
                Option option = kNoReusePort);
    ~TcpServer();

    void setThreadInitcallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; }
    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    //设置底层subloop的个数
    void setThreadNum(int numThreads);

    //开始服务器监听
    void start();
private:
    void newConnection(int sockfd,const InetAddress &peerAddr);
    void removeConnection(const TcpConnectionPtr &conn);
    void removeConnectionInLoop(const TcpConnectionPtr &conn);

    //存储连接的名字和对应的连接
    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop* loop_;//baseloop 用户自己定义的loop
    const std::string ipPort_;// 保存服务器的ip port
    const std::string name_;// 保存服务器的name
    std::unique_ptr<Acceptor> acceptor_;// 运行在mainloop的Acceptor，用于监听listenfd，等待新用户连接
    std::shared_ptr<EventLoopThreadPool> threadPool_;//事件循环线程池 one loop per thread

    ConnectionCallback connectionCallback_;//有新连接时的回调
    MessageCallback messageCallback_;//有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_;//消息发送完成后的回调

    ThreadInitCallback threadInitCallback_;//loop线程初始化的回调
    std::atomic_int started_;

    int nextConnId_;
    ConnectionMap connections_;//保存所有的连接
};