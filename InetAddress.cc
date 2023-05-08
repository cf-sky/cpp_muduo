#include "InetAddress.h"

#include<strings.h>
#include<string.h>

InetAddress::InetAddress(uint16_t port,std::string ip)
{
    bzero(&addr_,sizeof addr_); //清零
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port); //把本地字节序转成网络字节序，两个不同的端要通信的时候，系统都有可能不一样，我是小端你是大端，网路字节都是大端，我们需要都转成网络字节序，通过网络传送到对端后，再将网络字节序转成本地字节序，这样互相传输的数据都能识别了
    addr_.sin_addr.s_addr = inet_addr(ip.c_str());


}
std::string InetAddress::toIp()const
{
    char buf[64] = {0};
    ::inet_ntop(AF_INET,&addr_.sin_addr,buf,sizeof buf);//读出整数的表示网络字节序转成本地字节序
    return buf;
}
std::string InetAddress::toIpPort()const
{
    char buf[64] = {0};
    ::inet_ntop(AF_INET,&addr_.sin_addr,buf,sizeof buf);
    size_t end = strlen(buf);
    uint16_t port = ntohs(addr_.sin_port);
    sprintf(buf+end,":%u",port);
    return buf;
}
uint16_t InetAddress::toPort()const
{
    return ntohs(addr_.sin_port);
}

#include<iostream>
int main()
{
    InetAddress addr(8080);
    std::cout<<addr.toIpPort()<<std::endl;
    
}