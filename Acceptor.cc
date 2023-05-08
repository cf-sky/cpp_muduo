#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

#include <sys/types.h>         
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>

//创建listenfd
static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if(sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create err:%d \n", __FILE__, __FUNCTION__, __LINE__,errno);
    }
    return sockfd;
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    :loop_(loop)
    ,acceptSocket_(createNonblocking()) // Socket的构造函数需要一个int参数，createNonblocking()返回值就是int
    ,acceptChannel_(loop,acceptSocket_.fd()) // 第一个参数就是Channel所属的EventLoop
    ,listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr);// bind
    /**当我们TcpServer调用start方法时，就会启动Acceptor.listen()方法
     * 有新用户连接时，要执行一个回调，这个方法会将和用户连接的fd打包成Channel，wakeup subloop,
     * 然后交给subloop,下面就是注册包装了listenfd的Channel发生读事件后，需要执行的回调函数，
     * Acceptor只管理封装了listenfd的Channel，只需要注册读事件的回调
    **/
   //在listenfd还没有发生事件的时候，我们就给他预先注册一个事件回调，当真正listenfd有客户端连接的话，
   //底层反应堆会帮我们调用这个回调，即event对应的事件处理器
    acceptChannel_.setReadCallBack(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}

void Acceptor::listen()
{
    listenning_ = true;  //开始监听
    acceptSocket_.listen(); // 调用底层的::listen
    //将acceptChannel_注册到poller里面，poller才能帮忙监听是否有事件发生，
    //如果有事件发生，channel就会调用事先被注册的readcallback
    //就会执行到handleRead函数，这个函数就会拿到新用户连接的fd以及客户端的ip+port
    //直接执行回调
    acceptChannel_.enableReading();
}

//listenfd有事件发生了，就是有新用户连接了
void Acceptor::handleRead()
{
    InetAddress peerAddr; //客户端地址
    int connfd = acceptSocket_.accept(&peerAddr);
    if(connfd >= 0)
    {
        if(newConnectionCallback_) // 轮询找到subloop，唤醒并分发当前新客户端connfd的Channel
        {
            newConnectionCallback_(connfd,peerAddr);
        }
        else
        {
            ::close(connfd);// 如果没有设置新用户连接的回调操作，就关闭连接
        }
    }
    else //accept出错
    {
        LOG_ERROR("%s:%s:%d accept err:%d \n", __FILE__, __FUNCTION__, __LINE__,errno);
        if(errno == EMFILE)//当前进程没有可用的fd再分配
        {
            /**出现这种错误
             * 1.调整当前文件描述符的上限
             * 2.单台服务器已经不足以支撑现有的流量，需要做集群或者分布式部署
            **/
            LOG_ERROR("%s:%s:%d sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}