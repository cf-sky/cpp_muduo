#pragma once

#include <vector>
#include <string>
#include <algorithm>

//网络库底层的缓冲区类型定义
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;//数据包长度
    static const size_t kInitialSize = 1024; //缓冲区大小

    explicit Buffer(size_t initialSize = kInitialSize)
        :buffer_(kCheapPrepend + initialSize)
        ,readerIndex_(kCheapPrepend)
        ,writerIndex_(kCheapPrepend)
        {}
    
    //因为底层的内存资源是通过vector直接管理的，所以buffer_也不需要自己去析构资源，当前对象析构的时候成员对象析构，vector在析构的时候会自动释放外面堆内存管理的资源


    //可读数据长度
    size_t readableBytes() const
    {
        return writerIndex_ - readerIndex_;
    }

    //可写空间大小
    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;
    }
    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    //返回缓冲区中可读数据的起始地址
    const char* peek() const
    {
        return begin() + readerIndex_; 
    }

    /**
     *在底层相关connection有数据到来的时候，muduo库会注册回调onMessage 
     *把数据放到一个buffer里面，我们一般都会调用Buffer的retrieveAllAsString
     *把数据从buffer转成c++的string类型
     */
    void retrieve(size_t len)
    {
        // len就是应用程序从Buffer缓冲区读取的数据长度
        // 必须要保证len <= readableBytes()
        if(len < readableBytes())
        {
            // 这里就是可读数据没有读完
            //应用只读取了可读缓冲区数据的一部分，就是len,还剩下readableIndex_ += len - writerIndex_
            readerIndex_ += len;

        }
        else
        {
            // len == readableBytes()
            // 可读数据读完了，readerIndex_和writerIndex_都要复位
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    //把onMessage函数上报的Buffer数据，转成string类型的数据返回
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());//应用可读取数据的长度
    }
    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);
        retrieve(len);//上面一句把缓冲区中可读的数据，已经读取出来，这里肯定要对缓冲区进行复位操作
        return result;
    }

    void ensureWritableBytes(size_t len)
    {
        if(writableBytes() < len)
        {
            makeSpace(len);//扩容函数
        }
    }

    //不管是从fd上读数据写到缓冲区inputBuffer_，还是发数据要写入outputBuffer_，我们都要往writeable区间内添加数据
    void append(const char* data, size_t len)
    {
        // 确保可写空间不小于len
        ensureWritableBytes(len);
        // 把[data,data+len]内存上的数据，添加到writable缓冲区当中
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;

    }
    char* beginWrite()
    {
        return begin() + writerIndex_;
    }
    const char* beginWrite() const
    {
        return begin() + writerIndex_;
    }
    //从fd上读取数据，存放到writerIndex_，返回实际读取的数据大小 
    ssize_t readFd(int fd, int* saveErrno);
    ssize_t writeFd(int fd, int* saveErrno);
private:
    //返回buffer底层数组首元素的地址，也就是数组的起始地址
    char* begin()
    {
        return &(*buffer_.begin());
    }
    const char* begin() const
    {
        return &(*buffer_.begin());
    }
    /**
     *  
     */
    void makeSpace(size_t len)
    {
        //如果需要写入缓冲区数据的长度要大于Buffer对象底层vector空闲的长度了，就需要扩容，其中len表示需要写入数据的长度
        if(writableBytes() + prependableBytes() < len + kCheapPrepend)
        {
            // 直接在writerIndex_后面再扩大len的空间
            buffer_.resize(len + writerIndex_);
        }
        else // 如果是空闲空间足够存放len字节的数据，就把未读取的数据统一往前移，移到kCheapPrepend的位置
        {
            size_t readable = readableBytes();// 这表示剩余需要读取的数据
             // 把[readerIndex_, writerIndex_]整体搬到从kCheapPrepend开始的位置
            std::copy(begin() + readerIndex_,
                        begin() + writerIndex_,
                        begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;// writerIndex_指向待读取数据的末尾
        }
    }
    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
}; 