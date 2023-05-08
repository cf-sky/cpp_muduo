#pragma once

#include "noncopyable.h"

#include<functional>
#include<thread>
#include<memory>
#include<string>
#include<atomic>

class Thread : noncopyable
{
public:
    using ThreadFunc = std::function<void()>;//线程函数的函数类型  绑定器和函数对象，就可以传参 

    explicit Thread(ThreadFunc,const std::string &name = std::string());
    ~Thread();

    void start();
    void join();

    bool started() const { return started_; }
    pid_t tid() const { return tid_;}
    const std::string& name() const { return name_; }

    static int numCreated() { return numCreated_; }
private:
    void setDefaultName();

    bool started_;//启动当前线程 
    bool joined_;//当前线程等待其他线程完了再继续运行 
    std::shared_ptr<std::thread> thread_;//自己来掌控线程对象产生的时机 
    pid_t tid_;
    ThreadFunc func_;//存储线程函数 
    std::string name_;//线程名字，调试的时候打印 
    static std::atomic_int numCreated_;//创建的线程数 

};