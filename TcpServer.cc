#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

#include <functional>
#include <strings.h>

//定义成静态的，否则会和TcpConnection.cc中名字冲突
static EventLoop* CheckLoopNotNull(EventLoop* loop)
{
    if(loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

// 构造函数，用户需要传入一个EventLoop，即mainLoop，还有服务器的ip和监听的端口
TcpServer::TcpServer(EventLoop* loop,
                const InetAddress &listenAddr,
                const std::string& nameArg,
                Option option)
                :loop_(CheckLoopNotNull(loop))//不接受用户给loop传空指针
                ,ipPort_(listenAddr.toIpPort())
                ,name_(nameArg)
                ,acceptor_(new Acceptor(loop_,listenAddr,option = kReusePort))
                ,threadPool_(new EventLoopThreadPool(loop_,name_))
                ,connectionCallback_()
                ,messageCallback_()
                ,nextConnId_(1)
                ,started_(0)
{
    // 有新用户连接时，会调用Acceptor::handleRead，然后handleRead调用TcpServer::newConnection，
    // 使用两个占位符，因为TcpServer::newConnection方法需要新用户的connfd以及新用户的ip port
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection,this,std::placeholders::_1,std::placeholders::_2));
}
TcpServer::~TcpServer()
{
    for (auto &item : connections_)
    {
        //这个局部的shared_ptr智能指针对象，出右括号，可以自动释放new出来的TcpConnection资源
        TcpConnectionPtr conn(item.second);
        item.second.reset();

        //销毁连接
        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDestroyed, conn)
        );
    }
}

//设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

//开始服务器监听
void TcpServer::start()
{
    if(started_++ == 0)//防止一个TcpServer对象被start多次，只有第一次调用start才进入if
    {
        threadPool_->start(threadInitCallback_);//启动底层的loop线程池
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get())); //启动listen监听新用户的连接
    }
}

// 根据获取到connfd来封装TcpConnection对象
// 一条新连接到来，Acceptor根据轮询算法选择一个subloop并唤醒，把和客户端通信的connfd封装成Channel分发给subloop
// TcpServer要把newConnection设置给Acceptor，让Acceptor对象去调用，工作在mainLoop
// connfd是用于和客户端通信的fd，peerAddr封装了客户端的ip port
void TcpServer::newConnection(int sockfd,const InetAddress &peerAddr)
{
    //轮询算法，选择一个subLoop来管理channel
    EventLoop* ioLoop = threadPool_->getNextLoop();
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;
    //newConnection名称
    std::string connName = name_ + buf;

    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
        name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());
    
    //用通信的sockfd来获取其绑定的本机的ip地址和port
    sockaddr_in local;
    ::bzero(&local, sizeof local);
    socklen_t addrlen = sizeof local;
    // getsockname通过connfd拿到本地的sockaddr地址信息写入local
    if(::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }
    InetAddress localAddr(local);

    //根据连接成功的sockfd,创建TcpConnection连接对象
    // 将connnfd封装成TcpConnection，TcpConnection有一个Channel的成员变量，这里就相当于把一个TcpConnection对象放入了一个subloop
    TcpConnectionPtr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));

    connections_[connName] = conn;
    // 下面的回调都是用户设置给TcpServer的，然后TcpServer => TcpConnection => Channel，Channel会把自己封装的fd和events注册到Poller，发生事件时Poller调用Channel的handleEvent方法处理
    // 就比如这个messageCallback_，用户把on_message（messageCallback_）传给TcpServer，TcpServer会调用TcpConnection::setMessageCallback，那么TcpConnection的成员messageCallback_就保存了on_message
    // TcpConnection会把handleRead设置到Channel的readCallBack_，而handleRead就包括了TcpConnection::messageCallback_（on_message）
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    //设置如何关闭连接的回调
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)
    );
    //直接调用TcpConnection::connectEstablished
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

//连接已断开，从connectionMap里移除
void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn)
    );
}

//从事件循环中删除连接
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s \n", name_.c_str(), conn->name().c_str());
    connections_.erase(conn->name());
    EventLoop* ioLoop = conn->getLoop();
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn)
    );
}
