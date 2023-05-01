#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> //struct addr_in
#include <sys/mman.h>
#include <sys/uio.h>
#include <map>
#include <mysql.h>
#include <string.h>
#include <memory>
#include <vector>
#include <assert.h>
#include "../connecter/SqlConnectionPool.hpp"
#include "../Util.hpp"
using namespace util_ns;

#define READ_BUFFER 2048
#define WRITE_BUFFER 1024

namespace http_conn_ns
{
    class HttpConn; // 声明
    enum IO_STATE
    {
        READ = 0,
        WRITE = 1
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
        int _timer_flag;
        int _improv;

    private:
        int _sockfd;
        struct sockaddr_in _addr;
        // io缓冲区和相关参数
        // std::string _in_buffer;
        // std::string _out_buffer;
        char _in_buffer[READ_BUFFER];
        char _out_buffer[WRITE_BUFFER]; // 我们的发送缓冲区只包含响应行和响应报头，如果请求资源，我们会使用mmap方式映射客户端请求的资源
        int _read_idx;                  // int _r_buf_len;
        int _checked_idx;
        int _start_offset; // int _r_offset;
        int _write_idx;

        // http方法
        METHOD _method;
        // 用于状态机
        CHECK_STATE _check_state;

        // 以下保存请求报文中6个关键变量
        std::string _real_file;
        std::string _url_;
        std::string _version_;
        std::string _host_;
        char *_url;
        char *_version;
        char *_host;
        int _content_length;
        bool _linger;

        char *_file_map_addr; // 文件映射地址
        struct stat _file_stat;
        struct iovec _iov[2];
        int _iov_count;
        int _enable_post;

        std::string _req_content_data; // 存储正文数据

        int _bytes_have_send;
        int _bytes_not_send;

        char *_doc_root;

        std::map<std::string /*user name*/, std::string /*passwd*/> _users;
        // int _trig_mode;
        TRIG_MODE _trig_mode;
        locker_ns::Locker _lock;

        int _enable_log;

        std::string _sql_user;
        std::string _sql_passwd;
        std::string _sql_dbname;

    public:
        HttpConn(){}
        ~HttpConn(){}

    public:
        void init(int sockfd,
                  const sockaddr_in &addr,
                  char *root, TRIG_MODE trig_mode, int enable_log,
                  std::string user,
                  std::string passwd,
                  std::string sqlname){};
        void close_conn(bool _close = true){};
        void do_process(){};
        int read_once(){};
        bool write();

        struct sockaddr_in *get_address()
        {
            return &_addr;
        }
        void init_mysql(ConnectionPool *conn_pool){};

    private:
        void _init(){};
        HTTP_CODE read_process(){};
        bool write_process(HTTP_CODE ret);
        HTTP_CODE parse_request_line(char *text);
        HTTP_CODE parse_request_line(std::string);
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
    std::string ok_200_title = "OK";
    std::string error_400_title = "Bad Request";
    std::string error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
    std::string error_403_title = "Forbidden";
    std::string error_403_form = "You do not have permission to get file form this server.\n";
    std::string error_404_title = "Not Found";
    std::string error_404_form = "The requested file was not found on this server.\n";
    std::string error_500_title = "Internal Error";
    std::string error_500_form = "There was an unusual problem serving the request file.\n";

}
    