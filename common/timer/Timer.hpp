#pragma once

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "../http/Http.hpp"
using namespace http_conn_ns;

namespace timer_ns
{
    class Timer;
    struct ClientInfo
    {
        struct sockaddr_in _address;
        int _sockfd;
        Timer *_timer;
    };
    typedef void (*callback_t)(ClientInfo *);

    // 定时器类
    struct Timer
    {
        time_t _expire_time;    // 超时时间
        callback_t _cb_func;    // 回调函数
        ClientInfo *_user_info; // 回指http连接资源信息
        Timer *_pre;
        Timer *_next;

        Timer() : _cb_func(nullptr), _user_info(nullptr), _pre(nullptr), _next(nullptr)
        {
        }
    };

    class SortTimerList
    {
    private:
        Timer *_head;
        Timer *_tail;

    private:
        void add_timer(Timer *timer, Timer *list_head){};

    public:
        SortTimerList() : _head(nullptr), _tail(nullptr)
        {
        }
        ~SortTimerList()
        {
            Timer *tmp = _head;
            while (tmp)
            {
                _head = tmp->_next;
                delete tmp;
                tmp = _head;
            }
        }

        void add_timer(Timer *timer);    // 像定时器表中新加入一个定时器
        void adjust_timer(Timer *timer); // 调整定时器，任务发生变化时，调整定时器在链表中的位置
        void del_timer(Timer *timer);
        void tick();
    };

    class Utils
    {
    public:
        static int *_user_pipefd;
        static int _epfd;
        int _timeslot;
        SortTimerList _timer_list;

    public:
        Utils() {}
        ~Utils() {}

        void init(int timeslot){};
        static void sig_handler(int sig){};
        //sig:需要捕捉的信号  handler:自定义捕捉函数
        void set_sig(int sig, void (*handler)(int), bool restart = true){};

        // 定时处理任务，重新定时以不断触发SIGALRM信号
        void timer_handler(){};
        void show_error(int conn_fd, const char *info){};
    };
} 


