#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include<functional>
#include<memory>

class EventLoop;

/*
理清楚EventLoop、channel和Poller之间的关系，他们在reactor模型上对应Demultiplex多路事件分发器
channel理解为通道，封装了sockfd和其感兴趣的event,如EPOLLIN、EPOLLOUT事件
还绑定了poller返回的具体事件
*/
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;    //事件回调
    using ReadEventCallback = std::function<void(Timestamp)>;   //只读事件回调
    
    Channel(EventLoop *loop,int fd);
    ~Channel();

    //fd得到poller通知以后，处理事件的
    //调用相应的回调方法来处理事件 
    /*构造函数中的EventLoop只是前置声明就可以了，因为这里只用了类型定义的指针，不管是什么类型指针都是四个字节，不影响编译；
    但是TimeStamp这里是定义的变量，需要确定变量的大小，所以需要包含它的头文件
    */
   //fd得到poller通知以后，调用相应的回调方法处理事件
    void handleEvent(Timestamp receiveTime);

    //设置回调函数对象
    //因为cb和这个成员变量都属于左值，有内存和变量名字，因为function是一个对象，对象我们假设它是很大的，占用很多资源，在这里面调用的就是函数对象的赋值操作，
    //所以要把cb转成一个右值，把cb的资源转给成员变量，因为出了这个函数，cb形参的局部对象就不需要了，
    void setReadCallBack(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallBack(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallBack(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallBack(EventCallback cb) { errorCallback_ = std::move(cb); }

    //防止当channel被手动remove掉，channel还在执行回调操作
    void tie(const std::shared_ptr<void>&);

    int fd() const { return fd_; }
    int events() const { return events_; } //fd所感兴趣的事件
    void set_events(int revt) { revents_ = revt; } //poller监听事件，设置了channel的fd相应事件 

    //设置fd相应的事件状态，要让fd对这个事件感兴趣 
    //update就是调用epoll_ctrl，通知poller把fd感兴趣的事件添加到fd上
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~ kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() {events_ = kNoneEvent; update(); }
    

    //返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const {return events_ & kReadEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    //one loop per thread
    EventLoop* ownerLoop() { return loop_; }//当前channel属于哪个eventloop 
    void remove();//删除channel

private:

     void update();//更新，内部对象调用 
     void handleEventWithGuard(Timestamp receiveTime);//受保护的处理事件

    //表示当前fd和其状态，是没有对任何事件感兴趣，还是对读或者写感兴趣 
    static const int kNoneEvent;    //都不感兴趣
    static const int kReadEvent;    //读事件
    static const int kWriteEvent;   //写事件

    EventLoop *loop_;   //事件循环
    const int fd_;  //fd,poller监听的对象
    int events_;    //注册fd感兴趣的事件
    int revents_;   //poller返回的具体发生的事件
    int index_;

    /*
    防止手动调用removeChannel，Channel被手动remove以后我们还在使用Channel，
    所以做了跨线程的对象的生存状态的监听，使用的时候可以把弱智能指针提升成强智能指针，
    提升成功访问，提升失败说明已经被释放。
    */
    std::weak_ptr<void> tie_;   //绑定自己
    bool tied_;

    //因为channel通道里面能够获知fd最终发生的具体的事件revents，所以它负责调用具体事件的回调操作
    //这些回调是用户设定的，通过接口传给channel来负责调用 ，channel才知道fd上是什么事件 
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};