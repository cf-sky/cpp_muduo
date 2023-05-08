#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"

#include <sys/epoll.h>

const int Channel::kNoneEvent = 0; 
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI; //epoll的表示 
const int Channel::kWriteEvent = EPOLLOUT;   //epoll的表示 

//EventLoop底层: ChannelList  Poller 每个channel属于1个loop 
Channel::Channel(EventLoop *loop,int fd)
    :loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false)
{}

//析构函数
//保证没有正在处理的回调操作
//保证channel要么是从来没使用过，要么是被remove掉后才能析构
//判断当前的loop是否在它所在的线程去析构的channel
Channel::~Channel()
{}

//channel的tie方法什么时候调用过？一个TcpConnection新连接创建的时候 TcpConnection => Channel
void Channel::tie(const std::shared_ptr<void> &obj){
    tie_ = obj;
    tied_ = true;
}

/*
当改变channel所表示fd的events事件后，update负责在poller里面更改fd相应的事件epoll_ctl
 EventLoop => ChannelList   Poller
*/
void Channel::update(){
    //通过channel所属的EventLoop，调用poller的相应方法，注册fd的events事件
    loop_->updateChannel(this);
}

//在channel所属的EventLoop中， 把当前的channel删除掉
void Channel::remove(){
    loop_->removeChannel(this);
}

//fd得到poller通知以后，调用相应的回调方法处理事件
void Channel::handleEvent(Timestamp receiveTime){
    if(tied_){ //绑定过 
        std::shared_ptr<void> guard = tie_.lock(); //弱智能指针-》强智能指针 
        if(guard){ //是否存活 
            handleEventWithGuard(receiveTime);
        }
    }
    else{
        handleEventWithGuard(receiveTime);
    }
}

//根据poller通知的channel发生的具体事件， 由channel负责调用具体的回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime){
    LOG_INFO("channel handleEvent revent:%d\n",revents_);

    if((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)){
        if(closeCallback_){
            closeCallback_();
        }
    }

    if(revents_ & EPOLLERR){
        if(errorCallback_){
            errorCallback_();
        }
    }

    if(revents_ & (EPOLLIN|EPOLLPRI)){
        if(readCallback_){
            readCallback_(receiveTime);
        }
    }
    
    if(revents_ & EPOLLOUT){
        if(writeCallback_){
            writeCallback_();
        }
    }
}

