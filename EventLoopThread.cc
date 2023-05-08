#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,const std::string& name)
    :loop_(nullptr)
    ,exiting_(false)
    ,thread_(std::bind(&EventLoopThread::threadFunc,this),name)//绑定回调函数
    ,mutex_()
    ,cond_()
    ,callback_(cb)
{

}
EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if(loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}
EventLoop* EventLoopThread::startLoop()
{
    //启动底层的新线程，执行的是下发给底层线程的回调函数，也就是当前EventLoopThread的threadFunc
    thread_.start();
    EventLoop* loop = nullptr;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        while(loop_ == nullptr)
        {
            cond_.wait(lock); //成员变量loop_没有被新线程初始化的时候，一直wait在lock上
        }
        loop = loop_;
    }
    return loop;
}

//下面这个方法是在单独的新线程里面运行的
void EventLoopThread::threadFunc()
{
    //创建一个独立的EventLoop和上面的线程是一一对应的，one loop per thread
    EventLoop loop;

    //ThreadInitCallback就是在底层起一个新线程去绑定一个loop的时候，什么事情还没做
    //如果传递过ThreadInitCallback，在这里就会调用这个回调，可以把当前这个线程绑定的
    //loop对象传给这个回调函数，就可以针对这个loop做一些想做的事
    if(callback_)
    {
        callback_(&loop);
    }
    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;//就是运行在这个线程的loop对象
        cond_.notify_one();
    }
    //这里面执行了EventLoop的loop函数，开启了底层的poller
    //开始进入阻塞状态来监听远端用户的连接或已连接用户的读写事件
    loop.loop();
    
    //一般来说，loop是一直执行的，能执行到下面语句，说明程序要退出了，要关闭事件循环
    std::unique_lock<std::mutex> lock(mutex_);
    loop_=nullptr;
}