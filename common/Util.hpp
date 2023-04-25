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
    class TimerUtil;
    class FdUtil;
    struct ClientInfo
    {
        struct sockaddr_in _address;
        int _sockfd;
        TimerUtil *_timer;
    };
    typedef void (*callback_t)(ClientInfo *);

    struct TimerUtil
    {
        time_t _expire;
        callback_t _cb_func;
        ClientInfo *_user_info;
        TimerUtil *_pre;
        TimerUtil *_next;

        TimerUtil() : _cb_func(nullptr), _user_info(nullptr), _pre(nullptr), _next(nullptr)
        {
        }
    };

    class SortTimerList
    {
    private:
        TimerUtil *_head;
        TimerUtil *tail;

    private:
        void add_timer(TimerUtil *timer, TimerUtil *list_head)
        {
        }

    public:
        SortTimerList(){};
        ~SortTimerList(){};

        void add_timer(TimerUtil *timer);
        void adjust_timer(TimerUtil *timer);
        void del_timer(TimerUtil *timer);
        void tick();
    };

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
        //
        static void epoll_event_add(int epfd, int fd, bool one_shot, TRIG_MODE trig_mode)
        {
        }
        static void epoll_event_del(int epfd, int fd)
        {
        }
        static void epoll_event_mod(int epfd, int fd, int evt, TRIG_MODE trig_mode)
        {
        }
    };
}