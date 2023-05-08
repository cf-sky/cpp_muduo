#pragma once

#include<functional>
#include<vector>
#include<atomic>
#include<memory>
#include<mutex>

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"
//事件循环类，主要包含了两大模块Channel和Poller(epoll的抽象)

class Channel;
class Poller;

class EventLoop
{
public:
    using Functor = std::function<void()>;//相当于定义了一个回调的类型

    EventLoop();
    ~EventLoop();

    //开启事件循环
    void loop();
    //退出事件循环
    void quit();

    Timestamp pollReturnTime()const { return pollReturnTime_; }

    //在当前loop中执行cb
    void runInLoop(Functor cb);
    //把cb放入队列中，唤醒loop所在的线程，执行cb
    void queueInLoop(Functor cb);

    //用来唤醒loop所在的线程
    void wakeup();

    //eventloop的方法=》Poller的方法
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);

    //判断eventloop对象是否在自己的线程里面
    bool isInLoopThread()const { return threadId_ == CurrentThread::tid(); }

private:
    void handleRead();//处理wake up唤醒相关的逻辑
    void doPendingFunctors();//执行回调

    using ChannelList = std::vector<Channel*>;

    //下面两个变量是和这个事件循环本身是否继续loop下去相关的控制变量
    std::atomic_bool looping_;  //是否正常事件循环，原子操作，通过CAS实现
    std::atomic_bool quit_; //标志退出loop循环
    
    const pid_t threadId_;  //记录当前loop所在的线程id，会用于和执行的线程id比较
   
    //和poller有关
    Timestamp pollReturnTime_;  //poller返回发生事件channels的时间点
    std::unique_ptr<Poller> poller_; //eventloop所管理的poller 

    int wakeupFd_;  //主要作用，当mainloop获取一个新用户的channel，通过轮询算法选择一个subloop，通过该成员唤醒subloop(用的eventfd系统调用)处理channel
    std::unique_ptr<Channel> wakeupChannel_;    //包括wakeupFd和感兴趣的事件 

    //存储eventloop下所有的channel相关的
    ChannelList activeChannels_;//EventLoop中有事件发生的Channel
    Channel *currentActiveChannel_;

    //loop上需要执行的回调操作
    std::atomic_bool callingPendingFunctors_;   //标识当前Loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_; //存储loop需要执行的所有的回调操作
    std::mutex mutex_; //互斥锁，用来保护上面vector容器的线程安全的操作


};