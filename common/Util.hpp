#pragma once

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <memory>

namespace util_ns
{
    class FdUtil;
    enum TRIG_MODE
    {
        LT = 0,
        ET
    };
    class FdUtil
    {
    public:
        static void set_non_block(int sock_fd)
        {
            int flags = fcntl(sock_fd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            fcntl(sock_fd, F_SETFL, flags);
        }

        // 将描述符添加到内核，关注读事件
        static void epoll_event_add(int epfd, int fd, bool one_shot, TRIG_MODE trig_mode)
        {
            struct epoll_event event;
            event.data.fd = fd;

            if (trig_mode == ET)
            {
                event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            }
            else
            {
                event.events = EPOLLIN | EPOLLRDHUP;
            }

            if (one_shot)
            {
                event.events |= EPOLLONESHOT;
            }

            epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
            set_non_block(fd);
        }

        // 从内核时间表删除描述符
        static void epoll_event_del(int epfd, int fd)
        {
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, 0);
            close(fd);
        }
        static void epoll_event_mod(int epfd, int fd, int evt, TRIG_MODE trig_mode)
        {
            epoll_event event;
            event.data.fd = fd;

            if (1 == trig_mode)
            {
                event.events = evt | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
            }
            else
            {
                event.events = evt | EPOLLONESHOT | EPOLLRDHUP;
            }

            epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
        }
    };
}