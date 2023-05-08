#include "EventLoopThreadPool.h"
#include "EventLoopThread.h"

#include<memory>

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, const std::string &nameArg)
    :baseLoop_(baseLoop)
    ,name_(nameArg)
    ,started_(false)
    ,numThreads_(0)
    ,next_(0)
{}

//析构的时候不需要关注vector析构的时候里面存的EventLoop指针执行的外部资源是否需要单独delete，
//因为线程里面绑定的loop都是栈上的对象,不用担心需要手动delete它，它也不会释放掉，
//因为它下边的loop.loop()一直在循环，这个函数不会出右括号的，除非这个EventLoop底层的poller不工作了
//出右括号这个对象会自动析构的
EventLoopThreadPool::~EventLoopThreadPool()
{}

//根据指定的线程数量在池里面创建numThread_个数的事件线程
void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true;
    //用户通过setThreadNum设置了线程数就会进入循环
    for(int i = 0; i < numThreads_; ++i)
    {
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i); //底层线程名字 = 线程池名字+循环下标
        EventLoopThread *t = new EventLoopThread(cb, buf);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t)); // 用unique_ptr管理堆上的EventLoopThread对象，以免我们手动释放
        loops_.push_back(t->startLoop()); //底层创建线程，绑定一个新的EventLoop,并返回该loop的地址
    }

    //整个服务端只有一个线程，运行着baseloop
    if(numThreads_ == 0 && cb)
    {
        cb(baseLoop_);
    }
}

//如果工作在多线程中，baseLoop_会默认以轮询的方式分配channel给subloop
EventLoop* EventLoopThreadPool::getNextLoop()
{
    EventLoop* loop = baseLoop_;

    if(!loops_.empty()) //通过轮询获取下一个处理事件的loop
    {
        loop = loops_[next_];
        ++next_;
        if(next_ >= loops_.size())
        {
            next_ = 0;
        }
    }
    return loop;
}

//返回事件循环池所有的EventLoop,就是loops_
std::vector<EventLoop*> EventLoopThreadPool::getAllLoops()
{
    if(loops_.empty())
    {
        return std::vector<EventLoop*>(1,baseLoop_);
    }
    return loops_;
}