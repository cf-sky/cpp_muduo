#pragma once

#include <memory>
#include <functional>

class Buffer;
class TcpConnection;
class Timestamp;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;

using MessageCallback = std::function<void(const TcpConnectionPtr&,
                                            Buffer*,
                                            Timestamp)>;

/**
 * 水位线不能越过，越过了就会出危险，水位就得控制在水位线以下才安全
 * 所在在这里我们发送数据的时候，对端接收的慢，但发送方发的很快，
 * 数据就会丢失，就有可能出错。所以接收方和发送方两边的速率要趋于接近，
 * 这样数据发送出错的概率是非常小的，所以这里有个高水位控制，
 * 到达水位线了就会暂停发送
 */
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&, size_t)>;