#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include<vector>
#include<unordered_map>

//只用到指针类型 
class Channel;
class EventLoop;

//muduo库中多路事件分发器的核心IO复用模块
class Poller{
public:
    using ChannelList = std::vector<Channel*>;

    Poller(EventLoop *loop);
    virtual ~Poller();

    //给所有IO复用保留统一的接口
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;//相当于启动了epoll_wait 
    virtual void updateChannel(Channel* channel) = 0;//相当于启动了epoll_ctl 
    virtual void removeChannel(Channel* channel) = 0;//fd所感兴趣的事件delete掉 

    //判断当前Channel是否在Poller当中
    bool hasChannel(Channel *channel) const;

    //EventLoop可以通过该接口获取默认的IO复用的具体实现 
    static Poller* newDefaultPoller(EventLoop *loop);

protected:
    //map的key：sockfd  value：sockfd所属的channel通道类型
    using ChannelMap = std::unordered_map<int,Channel*>;
    ChannelMap channels_;
private:
    EventLoop *ownerLoop_; // 定义Poller所属的事件循环EventLoop
};