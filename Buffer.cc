#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

//从fd上读取数据，底层的Poller工作在LT模式，存放到writerIndex_，返回实际读取的数据大小 
//底层的buffer缓冲区是有大小的，但是从fd上读数据的时候，却不知道tcp数据最终的大小
//Buffer缓冲区是有大小的（占用堆区内存），但是我们无法知道fd上的流式数据有多少，
//如果我们将缓冲区开的非常大，大到肯定能容纳所有读取的数据，这就太浪费空间了，
//muduo库中使用readv方法，根据读取的数据多少开动态开辟缓冲区
ssize_t Buffer::readFd(int fd, int* saveErrno)
{
    char extrabuf[65536] = {0}; // 64K栈空间，会随着函数栈帧回退，内存自动回收
    
    struct iovec vec[2];
    
    const size_t writable = writableBytes(); // 这是Buffer底层缓冲区剩余的可写空间大小

    /**
     * 当我们用readv从fd上读数据，会先填充vec[0]的缓冲区
     * vec[0]填充满以后会自动把数据填充在extrabuf里面
     * 最后extrabuf里面如果有内容的话，就把extrabuf里面的内容添加在缓冲区里面
     * 这样的结果就是缓冲区刚好存放所有需要写入的内容，内存空间利用率高
    */
    vec[0].iov_base = begin() + writerIndex_; // 第一块缓冲区
    vec[0].iov_len = writable; // iov_base缓冲区可写的大小

    vec[1].iov_base = extrabuf; // 第二块缓冲区
    vec[1].iov_len = sizeof extrabuf;

    // 如果Buffer有65536字节的空闲空间，就不使用栈上的缓冲区
    //如果不够65536字节，就使用栈上的缓冲区，即readv一次最多读取65536字节数据
    const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1; 
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if(n < 0)
    {
        *saveErrno = errno;
    }
    else if(n <= writable)
    {
         // 读取的数据n小于Buffer底层的可写空间，readv会直接把数据存放在begin() + writerIndex_
         writerIndex_ += n;
    }
    else
    {
        //extrabuf里面也写入了数据
        writerIndex_ = buffer_.size();
        // 从extrabuff里读取 n - writable 字节的数据存入Buffer底层的缓冲区
        append(extrabuf, n - writable); 
    }
    return n;
}

ssize_t Buffer::writeFd(int fd,  int* saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if(n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}