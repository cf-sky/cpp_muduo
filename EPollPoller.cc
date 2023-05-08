#include "EPollPoller.h"
#include "Logger.h"

#include<errno.h>
#include <unistd.h>
#include<strings.h>

//channel的成员变量index_表示在Poller/EpollPoller中的状态
const int kNew = -1; // 一个Channel还没有添加到epoll或者从epoll和poller中的map删除，channel的成员index_ = -1
const int kAdded = 1; //一个Channel已经添加到Poller和Poller中的map
const int kDeleted = 2; // 一个Channel已经从epoll的红黑树中删除，但还存在与Poller中的map

EPollPoller::EPollPoller(EventLoop* loop)
    :Poller(loop)
    ,epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    ,events_(kInitEventListSize) //vector<epoll_event>，默认长度16
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }
}
EPollPoller::~EPollPoller()
{
    ::close(epollfd_);
}
//eventloop会创建一个channellist，并把创建好的channellist的地址传给poll
//poll通过epoll_wait监听到哪些fd发生了事件，把真真正正发生事件的channel通过形参发送到eventloop提供的实参里面
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    //实际上应该用LOG_DEBUG输出日志更合理,可以设置开启或者不开启 
    LOG_INFO("func=%s fd total count:%ld\n",__FUNCTION__,channels_.size());
    //对poll的执行效率有所影响
    int numEvents = ::epoll_wait(epollfd_,&*events_.begin(),static_cast<int>(events_.size()),timeoutMs);
    //events_.begin()返回首元素的迭代器（数组），也就是首元素的地址，是面向对象的，要解引用，就是首元素的值，然后取地址 
    //就是vector底层数组的起始地址   static_cast类型安全的转换   timeoutMs超时时间 
    int saveErrno = errno;//全局的变量errno，库里的，poll可能在多个线程eventloop被调用 ，所以有局部变量存起来 
    Timestamp now(Timestamp::now());//获取当前时间

    if(numEvents > 0) //表示有已经发生相应事件的个数 
    {
        LOG_INFO("%d events happened \n",numEvents);
        fillActiveChannels(numEvents,activeChannels);
        if(numEvents == events_.size())
        {
            events_.resize(events_.size() * 2); //所有的监听的event都发生事件了，得扩容了 
        }
    }
    else if(numEvents == 0) //epoll_wait这一轮监听没有事件发生，timeout超时了 
    {
        LOG_DEBUG("%d timeout! \n",__FUNCTION__);
    }
    else 
    {
        if(saveErrno != EINTR) //不等于外部的中断 ，是由其他错误类型引起的 
        {
            errno = saveErrno; //适配 ，把errno重置成当前loop之前发生的错误的值 
            LOG_ERROR("EPollPoller::poll() err!");
        }
    }
    return now;
}
//下面两个方法表示epoll_ctl的行为
//channel update remove => Eventloop updateChannel removeChannel => Poller updateChannel removeChannel
/**
 *            EventLoop  =>   poller.poll
 *     ChannelList      Poller
 *                     ChannelMap  <fd, channel*>   epollfd
 * EventLoop里面有一个ChannelList，就是所有的Channel都是在EventLoop里面管理的，
 * Channel创建以后，向Poller里面注册的和未注册过的全部放在EventLoop里面的ChannelList，
 * 如果某些Channel向Poller里面注册过了，那么这些Channel会写到Poller里面的成员变量ChannelMap里面，
 * poll方法还是通过EventLoop来调用Poller.poll
 * poll函数的作用是通过epoll_wait监听到哪些fd发生事件，也就是哪些channel发生事件，
 * 把真正发生事件的channel通过形参填到EventLoop提供的实参里面
*/
void EPollPoller::updateChannel(Channel* channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s => fd=%d events=%d index=%d\n",__FUNCTION__,channel->fd(),channel->events(),index);
    if(index == kNew || index == kDeleted) //未添加或者已删除
    {
        if(index == kNew) //未添加，键值对写入map中 
        {
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD,channel); //相当于调用epoll_ctl，添加1个channel到epoll中 
    }
    else //channel已经在poller上注册过了
    {
        int fd = channel->fd();
        if(channels_[fd]->isNoneEvent()) //已经对任何事件不感兴趣，不需要poller帮忙监听了 
        {
            channel->set_index(kDeleted);
            update(EPOLL_CTL_DEL,channel);
        }
        else
        {
            update(EPOLL_CTL_MOD,channel); //包含了fd的事件，感兴趣 
        }
    }
}
//从Poller中删除Channel
void EPollPoller::removeChannel(Channel* channel)
{
    int fd = channel->fd();
    channels_.erase(fd); //从map中删掉 

    LOG_INFO("func=%s => fd=%d\n",__FUNCTION__,channel->fd());

    int index = channel->index();
    if(index == kAdded) //如果已注册过 
    {
        update(EPOLL_CTL_DEL,channel); //通过epoll_ctl 删掉 
    }
    channel->set_index(kNew);//设置成未添加的状态 
}
void EPollPoller::fillActiveChannels(int numEvents,ChannelList *activeChannels) const
{
    for(int i = 0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_events(events_[i].events);
        activeChannels->push_back(channel);//EventLoop就拿到了它的poller给它返回的所有发生事件的channel列表了
        //至于EventLoop拿到这些channel干什么事情，我们看 EventLoop的代码 
    }
}
//更新Channel通道,就是epoll_ctl 的 add/mod/del 操作 
void EPollPoller::update(int operation,Channel *channel)
{
    epoll_event event;
    bzero(&event,sizeof event);
    int fd = channel->fd();
    event.events = channel->events(); //返回的就是fd所感兴趣的事件 
    event.data.fd = fd;
    event.data.ptr = channel;//绑定的参数
    
    if(::epoll_ctl(epollfd_,operation,fd,&event) < 0) //把fd相关事件更改 
    {
        if(operation == EPOLL_CTL_DEL) //没有删掉
        {
            LOG_ERROR("epoll_ctl delete error:%d\n",errno);
        }
        else //添加或者更改错误，这个会自动exit 
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n",errno);
        }
    }
}
