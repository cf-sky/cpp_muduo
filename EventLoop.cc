#include<sys/eventfd.h>
#include<unistd.h>
#include<fcntl.h>
#include<errno.h>
#include<memory.h>

#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

//防止一个线程创建多个eventloop   
//__thread：就是thread_local机制，如果不加就是全局变量，所有线程所共享，我们要一个线程就有一个eventloop
//当一个eventloop创建起来它就指向那个对象，在一个线程里再去创建一个对象，由于这个指针为空，就不创建 
__thread EventLoop *t_loopInThisThread = nullptr;

//定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;//10s

//创建wakeupfd，用notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK |EFD_CLOEXEC);
    //出错
    if(evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    :looping_(false)
    ,quit_(false)
    ,callingPendingFunctors_(false)
    ,threadId_(CurrentThread::tid())
    ,poller_(Poller::newDefaultPoller(this))
    ,wakeupFd_(createEventfd())
    ,wakeupChannel_(new Channel(this,wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this,threadId_);
    if(t_loopInThisThread)//这个线程已经有loop了，就不创建了 
    {
        LOG_FATAL("Another EventLoop %p exist in this thread %d \n",t_loopInThisThread,threadId_);
    }
    else//这个线程还没有loop，创建 
    {
        t_loopInThisThread = this;
    }

    //设置wakeup的事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallBack(std::bind(&EventLoop::handleRead,this));
    //每一个eventloop都将监听wakeupchannel的Epollin读事件了
    wakeupChannel_->enableReading();
}
EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

//开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;
    LOG_INFO("EventLoop %p start looping \n", this);

    while(!quit_)
    {
        activeChannels_.clear();
        //监听两类fd，一种是client的fd,一种是wakeup的fd
        pollReturnTime_ = poller_->poll(kPollTimeMs,&activeChannels_);
        for(Channel* channel : activeChannels_)
        {
            //Poller可以监听哪些channel发生事件了，然后上报给EventLoop,EventLoop通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        //执行当前EventLoop需要处理的回调操作
        /**
         * IO线程就是mainLoop(mainreactor)，主要做的是accept接收新用户的连接，接收新用户的连接以后，accept会返回和客户端专门通信用的fd
         * 我们肯定会用channel来打包fd，因为mainLoop只做新用户的连接，而已连接用户的channel得分发给subloop，如果说从来没有调用过muduo库
         * 的setthreadnumber，也就是说muduo库只有一个loop就是mainloop，不仅仅需要监听新用户的连接，还要负责已连接用户的读写事件，我们现
         * 在都是多核的CPU肯定会调用setthreadnumber，那么mainloop拿到和新用户通信的channel以后，就会wakeup唤醒某一个subloop，mainloop
         * 事先注册一个回调，这个回调就是一个cb，这个cb需要subloop来执行，但是此时的subloop还在阻塞，mainloop在wakeup以后(通过eventfd把
         * subloop唤醒)，要执行回调，回调都在pendingFunctors_里写的，回调就是谁唤醒你让你做事情的，做什么事情呢，mainloop要事先注册一个回调cb
         * 所以mainloop唤醒subloop以后，执行下面的方法，执行之前mainloop注册的cb
         */
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}
//退出事件循环 1. loop在自己的线程中调用quit，2.在非loop的线程中，调用loop的quit
/**
 *              mainLoop
 * 
 *              通过wakeupfd                     no ==================== 生产者-消费者的线程安全的队列
                                                mainloop生产 subloop消费  逻辑好处理 但是muduo库没有这个 是通过wakefd通信 
                                                线程间直接notify唤醒 
 * 
 *  subLoop1     subLoop2     subLoop3    
 */ 
void EventLoop::quit()
{
    quit_ = true;
    if(!isInLoopThread()) //2. 如果实在其他线程中调用的quit，比如在一个subloop中调用了mainloop的quit
    {
        wakeup();//因为不知道主线程是什么情况，需要唤醒一下 
    }
}

//在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
    if(isInLoopThread()) //在当前的loop线程中执行callback(cb)
    {
        cb();
    }
    else //在非当前loop线程中执行cb，那就需要唤醒loop所在线程执行cb
    {
        queueInLoop(cb);
    }
}
//把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
    //因为多个loop可能同时去调用另一个loop让它执行回调，所以vector涉及了并发访问，需要通过锁控制
    {
        std::unique_lock<std::mutex> lock(mutex_);
        //push_back是拷贝构造，emplace_back是直接构造
        pendingFunctors_.emplace_back(cb);
    }
    //唤醒相应的，需要执行上面回调操作的loop线程了
    //|| callingPendingFunctors_的意思是：当前loop正在执行回调，但是loop又有了新的回调，
    if(!isInLoopThread() || callingPendingFunctors_)
    {
        //唤醒loop所在线程
        wakeup();
    }
}


void EventLoop::handleRead()//就是读，写啥读啥无所谓，就是为了唤醒loop线程执行回调 
{
  uint64_t one = 1;
  ssize_t n = read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR("EventLoop::handleRead() reads %lu bytes instead of 8", n);
  }
}

//用来唤醒loop所在的线程,向wakeupfd_写一个数据,wakeupchannel就发生读书建，当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_,&one,sizeof one);
    if(n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8 \n", n);
    }
}

//eventloop的方法=》Poller的方法
void EventLoop::updateChannel(Channel* channel)
{
    poller_->updateChannel(channel);
}
void EventLoop::removeChannel(Channel* channel)
{
    poller_->removeChannel(channel);
}
bool EventLoop::hasChannel(Channel* channel)
{
    return poller_->hasChannel(channel);
}

//执行回调
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);//资源交换，把pendingFunctors_ 置为空
		//不需要pendingFunctors_了  不妨碍 mainloop向 pendingFunctors_写回调操作cb 
    }

    for(const Functor &functor : functors)
    {
        functor(); // 执行当前loop需要执行的回调操作
    }
    callingPendingFunctors_ = false;
}

