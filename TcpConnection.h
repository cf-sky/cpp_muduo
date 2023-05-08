#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;

/**
 * TcpServer通过Acceptor和一个新用户建立连接，通过accept函数拿到connfd，
 * 然后就可以打包TcpConnection，包括设置相应的回调，再把回调设置给Channel,
 * Channel注册到Poller上，Poller接收到事件就会调用Channel的回调操作
 */

class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop *loop,
                const std::string &nameArg,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr);
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; }

    bool connected() const { return kConnected == state_; }
    bool disconnected() const { return kDisconnected == state_; }

    //发送数据
    void send(const void* message, int len);

    void setConnectionCallback(const ConnectionCallback& cb)
    {
        connectionCallback_ = cb;
    }
    void setMessageCallback(const MessageCallback& cb)
    {
        messageCallback_ = cb;
    }
    void setWriteCompleteCallback(const WriteCompleteCallback& cb)
    {
        writeCompleteCallback_ = cb;
    }
    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
    {
        highWaterMarkCallback_ = cb;
        highWaterMark_ = highWaterMark;
    }
    void setCloseCallback(const CloseCallback& cb)
    {
        closeCallback_ = cb;
    }

    void send(const std::string& buf);
    //关闭连接
    void shutdown();

    //建立连接
    void connectEstablished();
    //销毁连接
    void connectDestroyed();

private:
    //初始的时候是kConnecting，连接成功是kConnected，断开连接shutdown的时候是kDisconnecting，最终把底层的socket关闭完以后是kDisconnected
    enum StateE {kDisconnected, kConnecting, kConnected, kDisconnecting}; //表示连接的状态
    void setState(StateE state) { state_ = state; }
    
    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();


    
    void sendInLoop(const void* data, size_t len);
    
    void shutdownInLoop();


    EventLoop *loop_; //这里绝对不是baseloop,因为TcpConnection都是在subloop里面管理的
    const std::string name_;
    std::atomic_int state_; // 会在多线程环境使用
    bool reading_;

    //这里和Acceptor类似，Acceptor是在mainLoop里面，TcpConnection是在subLoop里面
    //他们都需要封装底层的socket(listenfd/connfd封装成channel)，在相应loop的poller中去监听事件
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;  // 主机地址，这是定义变量，编译阶段需要知道变量占用空间，要包含头文件
    const InetAddress peerAddr_; //客户端地址

    ConnectionCallback connectionCallback_;//有新连接时的回调
    MessageCallback messageCallback_;//有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_;//消息发送完成后的回调
    HighWaterMarkCallback highWaterMarkCallback_;// 控制双方发送、接收速度
    CloseCallback closeCallback_;
    size_t highWaterMark_;//水位线

    Buffer inputBuffer_;// 用于服务器接收数据，handleRead就是写入inputBuffer_
    Buffer outputBuffer_;// 用于服务器发送数据，应用层需要发送的数据先存到outputBuffer_

};