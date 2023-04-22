#pragma once

#include <map>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> //struct addr_in
#include <mysql.h>
#include <string.h>
#include <memory>
#include "../connecter/SqlConnectionPool.hpp"
#include "../Util.hpp"
using namespace util_ns;

#define READ_BUFFER 2048
#define WRITE_BUFFER 1024

namespace http_conn_ns
{
    class HttpConn; // 声明
    // 初始化类内static变量
    int HttpConn::_epfd = -1;
    int HttpConn::_user_count = 0;

    enum IO_STATE
    {
        READ = 0,
        WRITE
    };
    // enum TRIG_MODE{
    //     LT = 0,
    //     ET
    // };
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUTDELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    // 用于状态机
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    class HttpConn
    {
    public:
        static int _epfd; // 所有Http连接对象共用一个epoll
        static int _user_count;
        MYSQL *_mysql;
        IO_STATE _io_state;

    private:
        int _sockfd;
        struct sockaddr_in _addr;
        // io缓冲区和相关参数
        // std::string _in_buffer;
        // std::string _out_buffer;
        char _in_buffer[READ_BUFFER];
        char _out_buffer[WRITE_BUFFER];
        int _read_idx; // int _r_buf_len;
        int _checked_idx;
        int _start_offset; // int _r_offset;
        int _write_idx;

        // http方法
        METHOD _method;
        // 用于状态机
        CHECK_STATE _check_state;

        // 以下保存请求报文中6个关键变量
        std::string _real_file;
        char *_url;
        char *_version;
        char *_host;
        int _content_len;
        bool _linger;

        char *_file_path; // 需读取的文件在服务器中的路径？
        struct stat _file_stat;
        struct iovec _iv[2];
        int _iv_count;
        int _enable_post;

        std::string _req_headr; // 存储请求头

        int _bytes_have_send;
        int _bytes_not_send;

        char *_doc_root;

        std::map<std::string /*user name*/, std::string /*passwd*/> _users;
        // int _trig_mode;
        TRIG_MODE _trig_mode;

        int _enable_log;

        std::string _sql_user;
        std::string _sql_passwd;
        std::string _sql_dbname;

    public:
        HttpConn(){};
        ~HttpConn(){};

    public:
        void init(int sockfd,
                  const sockaddr_in &addr,
                  char *root, TRIG_MODE trig_mode, int enable_log,
                  std::string user,
                  std::string passwd,
                  std::string sqlname){};
        void close_conn(bool _close = true){};
        void precess(){};
        int read_once(){};
        bool write();

        struct sockaddr_in *get_address()
        {
            return &_addr;
        }
        void init_mysql(ConnectionPool *conn_pool){};
        int _timer_flag;
        int _improv;

    private:
        void _init(){};
        HTTP_CODE process_read;
        bool process_write(HTTP_CODE ret);
        HTTP_CODE parse_request_line(char *text);
        HTTP_CODE parse_headers(char *text);
        HTTP_CODE parse_content(char *text);
        HTTP_CODE do_respone();

        // 根据响应报文格式，生成对应8个部分
        bool add_response(const char *format, ...);
        bool add_content(const char *content);
        bool add_status_line(int status, const char *title);
        bool add_headers(int content_length);
        bool add_content_type();
        bool add_content_length(int content_length);
        bool add_linger();
        bool add_blank_line();

        char *get_line(){};

        // 从状态机读取一行
        LINE_STATUS parse_line();

        void unmap();
    };

    // 定义http响应的一些状态信息
    const char *ok_200_title = "OK";
    const char *error_400_title = "Bad Request";
    const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
    const char *error_403_title = "Forbidden";
    const char *error_403_form = "You do not have permission to get file form this server.\n";
    const char *error_404_title = "Not Found";
    const char *error_404_form = "The requested file was not found on this server.\n";
    const char *error_500_title = "Internal Error";
    const char *error_500_form = "There was an unusual problem serving the request file.\n";

    // 初始化一个建立的http连接
    void HttpConn::_init()
    {
        _mysql = nullptr;
        _bytes_have_send = 0;
        _bytes_not_send = 0;
        _check_state = CHECK_STATE_REQUESTLINE; // 默认设置为分析http请求行的状态

        _method = GET; // 默认请求方法设置为GET
        _url = nullptr;
        _version = nullptr;
        _content_len = 0;
        _host = nullptr;

        _linger = false;

        _start_offset = 0; // 偏移量
        _checked_idx = 0;
        _read_idx = 0;
        _write_idx = 0;

        _enable_post = 0; // 默认禁用post
        _io_state = READ;
        _timer_flag = 0;
        _improv = 0;

        memset(_in_buffer, '\0', READ_BUFFER);
        memset(_out_buffer, '\0', WRITE_BUFFER);
        // memset(_real_file, '\0', FILENAME_LEN);
        //_real_file.clear();
    }

    // 初始化连接，外部调用初始化socket
    void HttpConn::init(int sockfd,
                        const sockaddr_in &addr,
                        char *root, TRIG_MODE trig_mode, int enable_log,
                        std::string user,
                        std::string passwd,
                        std::string sqlname)
    {
        _sockfd = sockfd;
        _addr = addr;

        //_trig_mode = trig_mode;
        FdUtil::epoll_event_add(_epfd, sockfd, true, _trig_mode);
        ++_user_count;

        _doc_root = root;
        _trig_mode = trig_mode;
        _enable_log = enable_log;

        _sql_user = user;
        _sql_passwd = passwd;
        _sql_dbname = sqlname;

        _init();
    }

    // 初始化数据库连接,并将所有的username,passwd读进内存
    // TODO redis代替
    void HttpConn::init_mysql(ConnectionPool *conn_pool)
    {
        // 遵循RAII原则
        MYSQL *mysql = nullptr;
        ConnectionPoolRAII(conn_pool, &mysql);
        if (mysql == nullptr)
        {
            // TODO
        }

        // 在user表中检获取所有的username，passwd数据
        if (mysql_query(mysql, "SELECT username,passwd from user"))
        {
            // TODO
            std::cout << "SELECT error:" << mysql_error(mysql) << std::endl;
        }

        // 获取查询结果
        MYSQL_RES *result = mysql_store_result(mysql);

        // 获取结果集中的列数和字段信息
        int num_fields = mysql_num_fields(result);
        // MYSQL_FIELD *fields = mysql_fetch_fields(result);

        // 遍历查询结果
        while (MYSQL_ROW row = mysql_fetch_row(result))
        {
            // 获取当前行的用户名和密码
            std::string username(row[0]);
            std::string passwd(row[1]);

            // 将用户名和密码存储到map中
            _users[username] = passwd;
        }
    }

    // 关闭一个http连接。即取消关注相应fd的事件,并关闭相应fd文件
    void HttpConn::close_conn(bool close)
    {
        if (close && (_sockfd != -1))
        {
            FdUtil::epoll_event_del(_epfd, this->_sockfd);
            _sockfd = -1;

            // 此处应加锁 TODO
            --_user_count;
        }
    }

    char *HttpConn::get_line()
    {
        return _in_buffer + _start_offset;
    }

    // 状态机用其解析一行内容.HTTP 协议中的换行符是"\r\n"
    LINE_STATUS HttpConn::parse_line()
    {
        char c;
        for (; _checked_idx < _read_idx; ++_checked_idx)
        {
            c = _in_buffer[_checked_idx];
            if (c == '\r')
            {
                if ((_checked_idx + 1) == _read_idx)
                {
                    // 非完整的http请求
                    return LINE_OPEN;
                }
                else if (_in_buffer[_checked_idx + 1] == '\n')
                {
                    // 将"\r\n"替换为"\0\0",并将_checked_idx更新为下行首元素下标
                    _in_buffer[_checked_idx++] = '\0';
                    _in_buffer[_checked_idx++] = '\0';
                    return LINE_OK;
                }
                return LINE_BAD;
            }
            else if (c == '\n')
            {
                return LINE_BAD;
            }
        }
        return LINE_OPEN;
    }

    /*
    return:
        1 读取完成
       -1 读取出错
        0 对端关闭链接
    */
    int HttpConn::read_once()
    {
        if (_read_idx >= READ_BUFFER)
        {
            // 缓冲区已满
            return -1;
        }

        // LT
        if (_trig_mode == LT)
        {
            //_in_buffer + _read_idx为缓冲区未使用空间的首地址，相应的READ_BUFFER - _read_idx为缓冲区的剩余空间大小
            int ret = recv(_sockfd, _in_buffer + _read_idx, READ_BUFFER - _read_idx, 0);
            if (ret > 0)
            {
                // 读取成功，更新缓冲区已使用大小
                _read_idx += ret;
                return 1;
            }
            else if (ret < 0)
            {
                // 读取失败
                return -1;
            }
            else
            {
                // ret == 0 对端关闭链接
                return 0;
            }
        }
        // ET模式
        else
        {
            while (true)
            {
                int ret = recv(_sockfd, _in_buffer + _read_idx, READ_BUFFER - _read_idx, 0);
                if (ret > 0)
                {
                    _read_idx += ret;
                }
                else if (ret < 0)
                {
                    if (errno == EINTR)
                    {
                        // 1. IO被信号中断
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // 2. 底层数据已读完,读取完成
                        break;
                    }
                    // 3. 读取出错
                    return -1;
                }
                else if (ret == 0)
                {
                    // ret == 0 对端关闭链接
                    return 0;
                }
            }
            
            // 至此，读取完毕
            return 1;
        }
    }

}