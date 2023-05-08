#pragma once

#include "Poller.h"
#include "Timestamp.h"
#include "Channel.h"

#include<vector>
#include<sys/epoll.h>


/*
epoll使用：
1. epoll_create创建fd
2. epoll_ctl添加想让epoll监听的fd以及针对fd所感兴趣的事件,进行add/mod/del
3. epoll_wait
*/
class EPollPoller : public Poller
{
public:
    //表示epoll_create行为
    EPollPoller(EventLoop* loop);
    ~EPollPoller() override;

    //重写基类Poller的抽象方法
    //表示epoll_wait行为
    Timestamp poll(int timeoutMs, ChannelList *activeChannels);
    //下面两个方法表示epoll_ctl的行为
     void updateChannel(Channel* channel);
     void removeChannel(Channel* channel);
private:
    static const int kInitEventListSize = 16;//初始化vector长度 

    using EventList = std::vector<epoll_event>;

    //填写活跃的连接
    void fillActiveChannels(int numEvents,ChannelList *activeChannels) const;
    //更新Channel通道
    void update(int operation,Channel *channel);

    int epollfd_;
    EventList events_;//epoll_wait的第二个参数 
};