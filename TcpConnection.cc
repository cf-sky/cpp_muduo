#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <errno.h>
#include <memory>
#include <sys/types.h>         
#include <sys/socket.h>
#include<strings.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <unistd.h>

static EventLoop* CheckLoopNotNull(EventLoop* loop)
{
    if(loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection Loop is null \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
                const std::string &nameArg,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr)
                :loop_(CheckLoopNotNull(loop))
                ,name_(nameArg)
                ,state_(kConnecting) //初始状态为正在连接
                ,reading_(true)
                ,socket_(new Socket(sockfd))
                ,channel_(new Channel(loop, sockfd))
                ,localAddr_(localAddr)
                ,peerAddr_(peerAddr)
                ,highWaterMark_(64*1024*1024)
{
    // 我们muduo用户把这些操作注册给TcpServer，TcpServer传递给TcpConnection，
    // TcpConnection给Channel设置回调函数，这些方法都是Poller监听到事件后，Channel需要调用的函数
    channel_->setReadCallBack(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    channel_->setWriteCallBack(
        std::bind(&TcpConnection::handleWrite, this)
    );
    channel_->setCloseCallBack(
        std::bind(&TcpConnection::handleClose, this)
    );
    channel_->setErrorCallBack(
        std::bind(&TcpConnection::handleError,this)
    );

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d \n", name_.c_str(), sockfd);
    // 调用setsockopt启动socket的保活机制
    socket_->setKeepAlive(true); 
}
TcpConnection::~TcpConnection()
{
    // 析构函数不需要做什么，只有socket_、channel_是new出来的，这俩用智能指针管理会自动释放
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d\n", name_.c_str(), socket_->fd(), (int)state_);
}

/**
 * 用户会给TcpServer注册一个onMessage方法，已建立连接的用户在发生读写事件的时候onMessage会响应，
 * 我们在onMessage方法处理完一些业务代码以后，会send给客户端返回一些东西，send最终发的时候，
 * 都是把数据序列化成json或者pb，都是转成相应的字符串，然后通过网络发送出去，
 * 所以就不需要专门初始化buf,而且如果用户值有字符串的话，buffer也没有带字符串相应的构造函数，
 * string也无法直接转成buffer,所以这里对外提供的接口string作为参数
 */


void TcpConnection::send(const std::string& buf)
{
    //state应该是已连接状态
    if (state_ == kConnected)
    {
        /**
         * 当前loop是否在对应线程里面，一般来说肯定在当前线程去执行这个方法，因为TcpConnection最终注册到某一线程里面，
         * poller也是在相应的Loop线程里去通知，可是我们有一些应用场景会把connection全部记录下来，记录下来就有可能在
         * 其他线程里面去调用connection进行send数据发送
         */
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(),buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(
                &TcpConnection::sendInLoop,
                this,
                buf.c_str(),
                buf.size()
                ));
        }
    }
}
/**
 * 发送数据，应用写的快，而内核发送数据慢，需要把待发送数据写入缓冲区，
 * 而且设置了水位回调
 */
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    ssize_t nwrote = 0; // 已发送数据
    size_t remaining = len; //未发送数据
    bool faultError = false; //记录是否产生错误

    //之前调用过该Connection的shutdown,不能再进行发送了
    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!");
        return;
    }
    
    //刚开始我们注册的感兴趣的都是socket读事件，写事件刚开始没有注册过
    //表示channel第一次开始写数据，而且缓冲区没有待发送数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 如果数据刚好发送完了 && 用户注册过发送完成的回调writeCompleteCallback_
                // 数据没有发送完时，channel_才对写事件感兴趣
                // 既然在这里数据全部发送完成，就不用再给channel设置epollout事件，handleWrite也不会再执行了，handleWrite就是有epollout事件发生时执行
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this())
                );
            }
        }
        else //nwrote < 0
        {
            nwrote = 0;
            // EWOULDBLOCK是由非阻塞没有数据正常的返回
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop \n");
                //接收到对端的socket重置，有错误发生了
                if (errno == EPIPE || errno == ECONNRESET) //SIGPIPE RESET
                {
                    faultError = true;
                }
            }
        }
    }

    /**
     * 说明当前这一次write，并没有把数据全部发送出去，剩余的数据需要保存到缓冲区当中，
     * 然后给channel注册epollout事件，poller发现tcp的发送缓冲区有空间，会通知相应的
     * sock-channel，调用writeCallback_相应的回调方法，也就是调用TcpConnection::handleWrite
     * 方法，把发送缓冲区的数据全部发送完成
     * 
     */
    if (!faultError && remaining > 0)
    {
        // 目前outputBuffer_中积攒的待发送的数据
        size_t oldlen = outputBuffer_.readableBytes();
        if (oldlen + remaining >= highWaterMark_
            && oldlen < highWaterMark_
            && highWaterMarkCallback_)
        {
            // 如果以前积攒的数据不足水位 && 以前积攒的加上本次需要写入outputBuffer_的数据大于水位 && 注册了highWaterMarkCallback_
            // 调用highWaterMarkCallback_
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldlen + remaining)
            );
        }
        // 剩余没发送完的数据写入outputBuffer_
        outputBuffer_.append((char*)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
            // 给Channel注册EPOLLOUT写事件，否则当内核的TCP发送缓冲区没有数据时，Poller不会给Channel通知，
            // Channel就不会调用writeCallback_，即TcpConnection::handleWrite
            channel_->enableWriting();
        }
    }
}

//关闭连接
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop,this));
    }
}

void TcpConnection::shutdownInLoop()
{
    // 有可能数据还没发送完就调用了shutdown，先等待数据发送完，发送完后handleWrite内会调用shutdownInLoop
    if (!channel_->isWriting())//说明outputBuffer_中的数据已经发送完成
    {
        socket_->shutdownWrite();//关闭写端
    }
}
//建立连接
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());
    //向Poller注册channel的epollin事件
    channel_->enableReading();
    //新连接建立，执行回调
    connectionCallback_(shared_from_this());
}

//销毁连接
void TcpConnection::connectDestroyed()
{
    if(state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); // 通过epoll_ctl把channel所有感兴趣的事件从poller中del掉
        connectionCallback_(shared_from_this());
    }
    channel_->remove(); //把channel从poller中删除
}

// fd上有读事件到来时，Poller会通知Channel调用相应的回调函数，即handleRead。这个函数用于读取fd上的数据存入inputBuffer_
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    // 发生了读事件，从channel_的fd中读取数据，存到inputBuffer_
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if(n > 0)
    {
        // 读取完成后，需要调用用户传入的回调操作，就是我们给TcpServer传入的on_message函数
        // 把当前TcpConnection对象给用户定义的on_message函数，是因为用户需要利用TcpConnection对象给客户端发数据，
        // inputBuffer_就是客户端发来的数据，也传入on_message函数给用户
        // shared_from_this就是获取了当前TcpConnection对象的一个shared_ptr
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if(n == 0)//客户端断开了
    {
        handleClose();
    }
    else//出错了
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

// TcpConnection::sendInLoop一次write没有发送完数据，将剩余的数据写入outputBuffer_后，然后Channel调用writeCallback_
// Channel调用的writeCallback_就是TcpConnection注册的handleWrite，handleWrite用于继续发送outputBuffer_中的数据到TCP缓冲区，
//直到outputBuffer_可读区间没有数据
void TcpConnection::handleWrite()
{
    if(channel_->isWriting())
    {
        int saveErrno = 0;
        // 往fd上写outputBuffer_可读区间的数据，写了n个字节，即发送数据
        int n = outputBuffer_.writeFd(channel_->fd(), &saveErrno);
        if(n > 0) //有数据发送成功
        {
            outputBuffer_.retrieve(n); // readerIndex_复位
            if(outputBuffer_.readableBytes() == 0)
            {
                // outputBuffer_的可读区间为0，已经发送完了，将Channel封装的events置为不可写，底层还是调用的epoll_ctl
                // Channel调用update remove ==> EventLoop updateChannel removeChannel ==> Poller updateChannel removeChannel
                channel_->disableWriting();
                if(writeCompleteCallback_)
                {
                    //唤醒loop对应的thread线程执行回调
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_,shared_from_this())
                    );
                }
                if(state_ = kDisconnected)
                {
                    // 读完数据时，如果发现已经调用了shutdown方法，state_会被置为kDisconnecting，
                    // 则会调用shutdownInLoop，在当前所属的loop里面删除当前TcpConnection对象
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else //对写事件不感兴趣,    要执行handleWrite，但是channel的fd的属性为不可写
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing \n", channel_->fd());
    }
}
//底层的poller通知channel调用它的closeCallback方法，最终就回调到TcpConnection::handleClose
void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d, state=%d \n", channel_->fd(), (int)state_);
    //只有在已连接或者正在断开的状态才能close
    setState(kDisconnected);
    //对channel所有的事件都不感兴趣了，从epoll红黑树中删除
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);//执行连接关闭的回调（用户传入的）
    closeCallback_(connPtr); // 执行连接关闭以后的回调，即TcpServer::removeConnection
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if(::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n", name_.c_str(), err);
}