/*
    WebServer类负责维护
        1.一个http连接表，包括所有的http连接对象
        2.一个http连接用户信息表，其中包含每个http连接对应的(Timer)定时器、文件描述符(fd)和struct sockaddr_in结构体
        3.所有的
*/
#pragma

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>

#include "http/Http.hpp"
#include "connecter/SqlConnectionPool.hpp"
#include "threadpool/ThreadPool.hpp"
#include "timer/Timer.hpp"

using namespace http_conn_ns;
using namespace thread_pool_ns;
using namespace timer_ns;
// using namespace sql_conn_ns;

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer;

class WebServer
{
public:
    int _port;
    std::string _root;
    int _log_write;
    int _enable_log;
    int _actor_model;

    int _pipefd[2]; // 匿名管道，用于传递定时器相关
    int _epfd;
    struct epoll_event _events[MAX_EVENT_NUMBER]; // 用于保存epoll返回的就绪队列

    int _listenfd;
    int _opt_linger;
    int _trig_mode;
    TRIG_MODE _listen_trig_mode;
    TRIG_MODE _conn_trig_mode;
    // int _listen_trig_mode;
    // int _conn_trig_mode;

    HttpConn *_http_conns; //

    // 数据库
    ConnectionPool *_sql_conn_pool; // 管理数据库连接链表
    std::string _user;
    std::string _passwd;
    std::string _dbname;
    int _sql_num;

    // 线程池
    ThreadPool<HttpConn> *_thread_pool;
    int _thread_num;

    // 定时器
    timer_ns::ClientInfo *_user_timer; // 管理定时器链表
    Utils _utils;

public:
    WebServer() {}
    ~WebServer() {}

    void init(int port, std::string user, std::string passwd, std::string dbname,
              int log_write, int opt_linger, int trig_mode, int sql_num,
              int thread_num, int enable_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void event_listen();
    void event_loop();
    void http_create_with_timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(Timer *timer);
    void deal_timer(Timer *timer, int sockfd);
    bool dealclinetdata();
    bool accepter();
    bool dealwithsignal(bool &timeout, bool &stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);
};

WebServer::WebServer()
{
    _http_conns = new HttpConn[MAX_FD];

    char server_path[200];
    getcwd(server_path, 200);

    _root = server_path;
    _root += "/root";

    _user_timer = new timer_ns::ClientInfo[MAX_FD];
}

WebServer::~WebServer()
{
    close(_epfd);
    close(_listenfd);
    close(_pipefd[1]);
    close(_pipefd[0]);
    delete[] _http_conns;
    delete[] _user_timer;
    delete _thread_pool;
}

void WebServer::init(int port, std::string user, std::string passwd, std::string dbname,
                     int log_write, int opt_linger, int trig_mode, int sql_num,
                     int thread_num, int enable_log, int actor_model)
{
    _port = port;
    _user = user;
    _passwd = passwd;
    _dbname = dbname;
    _sql_num = sql_num;
    _thread_num = thread_num;
    _log_write = log_write;
    _opt_linger = opt_linger;
    _trig_mode = trig_mode;
    _enable_log = enable_log;
    _actor_model = actor_model;
}

void WebServer::trig_mode()
{
    // LT + LT
    if (0 == _trig_mode)
    {
        _listen_trig_mode = LT;
        _conn_trig_mode = LT;
    }
    // LT + ET
    else if (1 == _trig_mode)
    {
        _listen_trig_mode = LT;
        _conn_trig_mode = ET;
    }
    // ET + LT
    else if (2 == _trig_mode)
    {
        _listen_trig_mode = ET;
        _conn_trig_mode = LT;
    }
    // ET + ET
    else if (3 == _trig_mode)
    {
        _listen_trig_mode = ET;
        _conn_trig_mode = ET;
    }
}

void WebServer::log_write()
{
    // TODO
}

void WebServer::sql_pool()
{
    // 获取连接池对象并初始化,循环建立指定数量的mysql连接
    _sql_conn_pool = ConnectionPool::get_instance();
    _sql_conn_pool->init("127.0.0.1", _user, _passwd, _dbname, 3306, _sql_num, _enable_log);

    // 将所有的username,passwd读进内存,建立user -- passwd map映射
    _http_conns->init_mysql(_sql_conn_pool);
    // _http_conns[0].init_mysql(_sql_conn_pool);这样写是否更容易理解一些？或者将_users设置为静态成员？ TODO
}

void WebServer::thread_pool()
{
    /*
        我们创建线程池对象，对象内部维护一个任务队列，该队列资源被所有线程共享。
        线程池初始化指定数量的线程，线程中运行基于任务队列的消费者函数。等待任务到来。
    */
    _thread_pool = new ThreadPool<HttpConn>(_actor_model, _sql_conn_pool, _thread_num);
}

/*
    事件循环，我们做了以下事情
        1.老三样，创建、绑定、监听端口
        2.创建用于通信的管道
        3.创建epoll_event事件表
        4.自定义捕捉SIGPIPE(管道破裂)/SIGALRM(用于定时器)/SIGTERM(进程终止)信号函数
        5.初始化工具类，其内部维护了用于通信的pipe管道，定时器链表，epool_fd，等待时间间隔timeslot
*/
void WebServer::event_listen()
{
    _listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(_listenfd >= 0);

    // 设置套接字关闭之前，若缓冲区中仍有数据，是否需要等待数据发送完成
    if (_opt_linger == 0 || _opt_linger == 1)
    {
        struct linger tmp;
        tmp.l_onoff = _opt_linger; // 0 无需等待数据发送完成，立即关闭. 1 在关闭前等待l_linger秒
        tmp.l_linger = 1;          // 当l_onoff设置为0时，无意义
        setsockopt(_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(_port);

    // 设置允许端口处于TIME_WAIT状态时，可立即重新绑定套接字
    int flag = 1;
    setsockopt(_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    int ret = 0;
    ret = bind(_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret == 0);
    ret = listen(_listenfd, 5);
    assert(ret == 0);

    _utils.init(TIMESLOT);

    // epoll
    struct epoll_event events[MAX_EVENT_NUMBER];
    _epfd = epoll_create(5);
    assert(_epfd >= 0);

    // 监听套接字加入内核
    FdUtil::epoll_event_add(_epfd, _listenfd, false, _listen_trig_mode);
    HttpConn::_epfd = _epfd;

    // 类似于匿名管道，pipe[1]为写端，pipe[0]为读端
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, _pipefd);
    assert(ret == 0);
    FdUtil::set_non_block(_pipefd[1]);
    FdUtil::epoll_event_add(_epfd, _pipefd[0], false, LT);

    _utils.set_sig(SIGPIPE, SIG_IGN);
    _utils.set_sig(SIGALRM, _utils.sig_handler, false);
    _utils.set_sig(SIGTERM, _utils.sig_handler, false); // 进程终止信号，不同于kill信号，该信号可捕捉，可以自定义捕捉函数使进程退出前执行清理释放资源等工作

    alarm(TIMESLOT);

    // Utils类静态成员变量赋值
    Utils::_epfd = _epfd;
    Utils::_user_pipefd = _pipefd;
}

/*
    1.初始化一个Http连接对象
    2.维护该对象的连接信息（fd sockaddr_in timer），并初始化定时器
    3.将该定时器加入定时器链表中管理
*/
void WebServer::http_create_with_timer(int connfd, struct sockaddr_in client_address)
{
    _http_conns[connfd].init(connfd, client_address, const_cast<char *>(_root.c_str()), _conn_trig_mode, _enable_log, _user, _passwd, _dbname);

    _user_timer[connfd]._address = client_address;
    _user_timer[connfd]._sockfd = connfd;

    // 创建定时器，设置回调函数和超时时间，绑定用户数据
    Timer *timer = new Timer();
    timer->_user_info = &_user_timer[connfd]; // 回指ClientInfo
    timer->_cb_func = cb_func;
    time_t cur_time = time(nullptr);
    timer->_expire_time = cur_time + 3 * TIMESLOT;

    _user_timer[connfd]._timer = timer;
    _utils._timer_list.add_timer(timer);
}

// 更新定时器的超时时间，并调整其在定时器链表中的位置
void WebServer::adjust_timer(Timer *timer)
{
    time_t cur_time = time(nullptr);
    timer->_expire_time = cur_time + 3 * TIMESLOT;
    _utils._timer_list.adjust_timer(timer);

    printf("adjust timer once");
}

void WebServer::deal_timer(Timer *timer, int sockfd)
{
    timer->_cb_func(&_user_timer[sockfd]);
    if (timer)
    {
        _utils._timer_list.del_timer(timer);

        // TODO
        printf("close fd %d , %d", _user_timer[sockfd]._sockfd, sockfd);
    }
}

bool WebServer::accepter()
{
    struct sockaddr_in client_address;
    socklen_t addr_length = sizeof(client_address);

    // LT
    if (_listen_trig_mode == LT)
    {
        int connfd = accept(_listenfd, (struct sockaddr *)&client_address, &addr_length);

        if (connfd < 0)
        {
            printf("accept error:errno is:%d", errno);
        }
        if (HttpConn::_user_count >= MAX_FD)
        {
            _utils.show_error(connfd, "Internal server busy");
            printf("Internal server busy");
            return false;
        }
        http_create_with_timer(connfd, client_address);
    }
    // ET
    else
    {
        while (true)
        {
            int connfd = accept(_listenfd, (struct sockaddr *)&client_address, &addr_length);
            if (connfd < 0)
            {
                printf("accept error:errno is:%d", errno);
                break;
            }
            if (HttpConn::_user_count >= MAX_FD)
            {
                _utils.show_error(connfd, "Internal server busy");
                printf("Internal server busy");
                break;
            }

            http_create_with_timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];

    ret = recv(_pipefd[0], signals, sizeof(signals), 0);

    if (ret > 0)
    {
        // 我们只关注收到的定时器信号和进程关闭信号
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
        return true;
    }
    return false;
}

void WebServer::dealwithread(int sockfd)
{
    Timer *timer = _user_timer[sockfd]._timer;

    // Reactor
    if (_actor_model == 1)
    {
        if (timer)
        {
            // 涉及到IO，我们需要更新定时器
            adjust_timer(timer);
        }
        // 我们把涉及到读请求的http对象加入线程池的请求队列
        _thread_pool->append(&_http_conns[sockfd], 0);

        while (true)
        {
            if (_http_conns[sockfd]._improv == 1)
            {
                if (_http_conns[sockfd]._timer_flag == 1)
                {
                    deal_timer(timer, sockfd);
                    _http_conns[sockfd]._timer_flag = 0;
                }

                _http_conns[sockfd]._improv = 0;
                break;
            }
        }
    }
    else
    {
        // Proactor
        if (_http_conns[sockfd].read_once() == 1)
        {
        }

        // if (_http_conns[sockfd].write())
        // {
        //     printf("send data to the client(%s)", inet_ntoa(_http_conns[sockfd].get_address()->sin_addr));

        //     if (timer)
        //     {
        //         adjust_timer(timer);
        //     }
        // }
        // else
        // {
        //     deal_timer(timer, sockfd);
        // }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    Timer *timer = _user_timer[sockfd]._timer;
    // Reactor
    if (_actor_model == 1)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        _thread_pool->append(&_http_conns[sockfd], 1);

        while (true)
        {
            if (_http_conns[sockfd]._improv)
            {
                if (_http_conns[sockfd]._timer_flag == 1)
                {
                    deal_timer(timer, sockfd);
                    _http_conns[sockfd]._timer_flag = 0;
                }
                _http_conns[sockfd]._improv = 0;
                break;
            }
        }
    }
    else
    {
        // Proactor
    }
}
void WebServer::event_loop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int evt_nums = epoll_wait(_epfd, _events, MAX_EVENT_NUMBER, -1);
        if (evt_nums < 0 && errno != EINTR)
        {
            printf("epoll failure");
            break;
        }

        for (int i = 0; i < evt_nums; ++i)
        {
            int sockfd = _events[i].data.fd;
            uint32_t evt = _events[i].events;

            if (sockfd == _listenfd)
            {
                // 新连接到来是以读就绪方式通知
                bool ret = accepter();
                if (ret == false)
                {
                    continue;
                }
            }
            else if (evt & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭连接，移除对应的定时器
                //  Timer *timer = _user_timer[sockfd]._timer;
                deal_timer(_user_timer[sockfd]._timer, sockfd);
            }
            else if ((sockfd == _pipefd[0]) && (evt & EPOLLIN))
            {
                // 管道读端读事件就绪，管道内为写端写入的信号数据，我们以此实现信号的异步处理
                bool ret = dealwithsignal(timeout, stop_server);
                if (ret == false)
                {
                    printf("dealclientdata failure");
                }
            }
            else if (evt & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (evt & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }

        if (timeout)
        {
            _utils.timer_handler();
            printf("timer tick");
            timeout = false;
        }
    }
}
