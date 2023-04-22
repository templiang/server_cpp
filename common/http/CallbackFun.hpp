#pragma once 
#include <string>

/*
    return:
        1 读取完成
       -1 读取出错
        0 对端关闭链接
*/
static int recver_from_core(int sock_fd, std::string &in_buffer)
{
    while (1)
    {
        char buffer[ONCE_SIZE];
        int ret = recv(sock_fd, buffer, ONCE_SIZE - 1, 0);
        // recv return:When a stream socket peer has performed an orderly shutdown, the return value will be 0
        if (ret > 0)
        {
            // 读取成功
            buffer[ret] = '\0';
            in_buffer += buffer;
        }
        else if (ret < 0)
        {
            // EINTR  The receive was interrupted by delivery of a signal before any data were available
            if (errno == EINTR)
            {
                // 1. IO被信号中断
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 2. 底层数据已读完,读取完成
                return 1; // success
            }

            // 3. 读取出错
            return -1;
        }
        else
        {
            // ret == 0 对端关闭链接
            return 0;
        }
    }
}