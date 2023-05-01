#include "Http.hpp"
using namespace http_conn_ns;

// 初始化类内static变量
int HttpConn::_epfd = -1;
int HttpConn::_user_count = 0;

void HttpConn::do_process()
{
    HTTP_CODE read_ret = read_process();
    if (read_ret == NO_REQUEST)
    {
        // 请求不完整,继续监听读事件
        FdUtil::epoll_event_mod(_epfd, _sockfd, EPOLLIN, _trig_mode);
        return;
    }

    if (!write_process(read_ret))
    {
        close_conn();
    }
    // 现在，我们已经将数据写入到了自定义的输出缓冲区，我们需要关注写事件，以待合适的时机将数据发送到内核
    FdUtil::epoll_event_mod(_epfd, _sockfd, EPOLLOUT, _trig_mode);
}

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
    _content_length = 0;
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
        assert(0);
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

// 解析http请求行,提取请求行中的url.请求行内容之间以'\t'或' '作为分隔符,例如GET /index.html HTTP/1.1\r\n
HTTP_CODE HttpConn::parse_request_line(std::string line)
{
    std::vector<std::string> result;

    size_t pos = 0;
    while (pos != std::string::npos)
    {
        size_t next_pos = line.find_first_of(" \t", pos);
        std::string token = line.substr(pos, next_pos - pos);

        // 如果不是最后一个单词，则将pos移到下一个单词的起始位置
        if (next_pos != std::string::npos)
        {
            // pos = line.find_first_not_of(" \t", next_pos);
            pos = next_pos + 1;
        }
        result.push_back(token);
    }

    if (result.size() != 3)
    {
        return BAD_REQUEST;
    }
    // 提取请求方法
    if (strcasecmp(result[0].c_str(), "GET") == 0)
    {
        _method = GET;
    }
    else if (strcasecmp(result[0].c_str(), "POST") == 0)
    {
        _method = POST;
        _enable_post = 1;
    }
    else
    {
        return BAD_REQUEST;
    }

    // 提取版本号
    if (strcasecmp(result[2].c_str(), "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    _version_ = move(result[2]);

    // 提取url
    if (strncasecmp(result[1].c_str(), "http://", 7) == 0)
    {
        result[1].erase(0, 7);
    }
    else if (strncasecmp(result[1].c_str(), "https://", 8) == 0)
    {
        result[1].erase(0, 8);
    }

    size_t pos = result[1].find_first_of('/');

    if (pos == std::string::npos)
    {
        // 没找到
        return BAD_REQUEST;
    }
    _url_ = result[1].substr(pos, std::string::npos);

    // 当url等于'/'时
    if (_url_ == "/")
    {
        _url_ += "judge,html";
    }

    // 至此，请求行解析完毕,状态切换至解析请求头
    _check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// HTTP_CODE HttpConn::parse_request_line(char *line)
// {
//     char *ret = nullptr;
//     char *start = nullptr;
//     char *end = nullptr;

//     ret = strpbrk(line, " \t");
//     if (ret == nullptr)
//     {
//         return BAD_REQUEST;
//     }
//     // 此时,ret指向method后的空格或'\t'
//     *ret == '\0';
//     start = ++ret;

//     char *method = line;
//     if (strcasecmp(method, "GET") == 0)
//     {
//         _method == GET;
//     }
//     else if (strcasecmp(method, "POST") == 0)
//     {
//         _method == POST;
//         _enable_post == 1;
//     }
//     else
//     {
//         return BAD_REQUEST;
//     }

//     ret = strpbrk(start, " \t");
//     if (ret == nullptr)
//     {
//         return BAD_REQUEST;
//     }
//     // 此时,ret指向url后的空格或'\t'
//     *ret = '\0';
//     end = ret++;
//     _url_ = std::string(start, end);
//     // todo
// }

// 解析请求头
HTTP_CODE HttpConn::parse_headers(char *line)
{
    if (line[0] == '\0')
    {
        // 当该行为'\0'时。如果请求头中有Content-length字段已解析，本次请求中含有正文部分，状态切换为解析正文。否则证明本次请求解析完毕
        if (_content_length != 0)
        {
            _check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(line, "Connection:", 11) == 0)
    {
        line += 11;
        line += strspn(line, " \t");
        if (strcasecmp(line, "keep-alive") == 0)
        {
            _linger = true;
        }
    }
    else if (strncasecmp(line, "Content-length:", 15) == 0)
    {
        line += 15;
        line += strspn(line, " \t");
        // _content_length = atol(line);
        _content_length = atoi(line);
    }
    else if (strncasecmp(line, "Host:", 5) == 0)
    {
        line += 5;
        line += strspn(line, " \t");
        _host_ = line;
    }
    else
    {
        // todo
        printf("oop!unknow header: %s", line);
    }
    return NO_REQUEST;
}
// 判断http请求是否完整
HTTP_CODE HttpConn::parse_content(char *line)
{
    if (_read_idx >= (_content_length + _checked_idx))
    {
        line[_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        _req_content_data = line;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 生成响应
HTTP_CODE HttpConn::do_respone()
{
    _real_file = _doc_root;
    int len = _real_file.size();
    printf("url:%s\n", _url_);

    size_t pos = _url_.find_last_of('/');
    if ((pos != std::string::npos))
        return BAD_REQUEST;
    char flag = _url_[pos + 1];

    // 处理POST请求
    //"/2":POST登录  "/3":POST注册
    if (_enable_post == 1 && (flag == '2' || flag == '3'))
    {
        // char flag = _url_[1];
        _real_file += std::string("/" + _url_.substr(2, std::string::npos));
        // 提取用户名和密码 user=liang&passwd=123456
        std::string user_name;
        std::string passwd;

        size_t pos = _req_content_data.find('&');
        user_name = _req_content_data.substr(5, pos - 5);

        pos = _req_content_data.find_last_of('=');
        passwd = _req_content_data.substr(pos + 1, std::string::npos);

        if (flag == '3')
        {
            // 注册。注册前检测用户名是否已注册
            if (_users.count(user_name) == 0)
            {
                std::string sql_insert("INSERT INTO user(username, passwd) VALUES(");
                sql_insert += "'" + user_name + "',";
                sql_insert += "'" + passwd + "')";
                _lock.lock();
                int ret = mysql_query(_mysql, sql_insert.c_str());
                _users.insert({user_name, passwd});
                _lock.lock();

                if (ret == 0)
                {
                    // 注册成功,跳转到登录页面
                    _url_ = "/log.html";
                }
                else
                {
                    // 注册失败
                    _url_ = "/registerError.html";
                }
            }
            else
            {
                _url_ = "/registerError.html";
            }
        }
        else if (flag == '2')
        {
            // 登录
            const auto &iter = _users.find(user_name);
            if (iter != _users.end() && iter->second == passwd)
            {
                _url_ = "/welcome.html";
            }
            else
            {
                _url_ = "/logError.html";
            }
        }
    }

    // 在POST后,flag是否需要重新赋值？
    switch (flag)
    {
    case '0':
        _real_file += "/register.html";
        break;
    case '1':
        _real_file += "/log.html";
        break;
    case '5':
        _real_file += "/picture.html";
        break;
    case '6':
        _real_file += "/video.html";
        break;
    case '7':
        _real_file += "/fans.html";
        break;
    default:
        // 此时为请求图片
        _real_file += _url_;
        break;
    }

    if (stat(_real_file.c_str(), &_file_stat) != 0)
    {
        return NO_RESOURCE;
    }

    if ((_file_stat.st_mode & S_IROTH) == 0)
    {
        // 没有读取该文件的权限
        return FORBIDDEN_REQUEST;
    }

    if (S_ISDIR(_file_stat.st_mode))
    {
        // 该文件为目录文件
        return BAD_REQUEST;
    }

    int fd = open(_real_file.c_str(), O_RDONLY);
    // mmap将文件的内容读取到一段内核分配的连续地址空间（避免了read时内核与用户空间的频繁切换与拷贝），该空间被映射到用户地址空间，可以被用户进程读取，
    _file_map_addr = (char *)mmap(0, _file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}

// 主状态机
HTTP_CODE HttpConn::read_process()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *line = 0;

    while ((_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        line = get_line();
        _start_offset = _checked_idx;
        printf("%s", line);
        switch (_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(line);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(line);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_respone();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(line);
            if (ret == GET_REQUEST)
                return do_respone();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
void HttpConn::unmap()
{
    if (_file_map_addr != nullptr)
    {
        munmap(_file_map_addr, _file_stat.st_size);
        _file_map_addr == nullptr;
    }
}
bool HttpConn::write()
{
    int ret = 0;

    if (_bytes_not_send == 0)
    {
        // 写事件一般一直是就绪的。所以当数据已经发送完成，我们便不再关注写事件，避免epoll一直被写事件触发
        // 同时，我们将对象内的成员重置
        FdUtil::epoll_event_mod(_epfd, _sockfd, EPOLLIN, _trig_mode);
        _init();
        return true;
    }

    while (true)
    {
        ret = writev(_sockfd, _iov, _iov_count);

        if (ret > 0)
        {
            _bytes_have_send += ret;
            _bytes_not_send -= ret;

            // 数据已发完
            if (_bytes_not_send <= 0)
            {
                unmap();
                FdUtil::epoll_event_mod(_epfd, _sockfd, EPOLLIN, _trig_mode);

                if (_linger)
                {
                    _init();
                    return true;
                }
                else
                {
                    return true;
                }
            }
            // 数据未发完，更新下次需要发送数据的地址与长度
            if (_bytes_have_send >= _iov[0].iov_len)
            {
                // iv[0]数据已发完，更新下次需要发送数据的地址与长度
                assert(_iov_count == 2);
                int offset = _bytes_have_send - _iov[0].iov_len;
                _iov[0].iov_len = 0;
                _iov[1].iov_base = _file_map_addr + offset;
                _iov[1].iov_len = _bytes_not_send;
            }
            else
            {
                _iov[0].iov_base += _bytes_have_send;
                _iov[0].iov_len -= _bytes_have_send;
            }
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 内核输出缓冲区已满，writev未完成向内核缓冲区写数据，此时我们继续关注写事件，等待机会再次尝试写入
                FdUtil::epoll_event_mod(_epfd, _sockfd, EPOLLOUT, _trig_mode);
                return true;
            }
            if (errno == EINTR)
            {
                // 写入被信号中断
                continue;
            }

            // 写入失败，不能继续写入
            unmap();
            return false;
        }
    }
}
bool HttpConn::add_response(const char *format, ...)
{
    if (_write_idx >= WRITE_BUFFER)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(_out_buffer + _write_idx, WRITE_BUFFER - 1 - _write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER - 1 - _write_idx))
    {
        va_end(arg_list);
        return false;
    }
    _write_idx += len;
    va_end(arg_list);

    printf("respone:%s", _out_buffer);
    return true;
}

bool HttpConn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool HttpConn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool HttpConn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool HttpConn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool HttpConn::add_linger()
{
    return add_response("Connection:%s\r\n", (_linger == true) ? "keep-alive" : "close");
}
bool HttpConn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool HttpConn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool HttpConn::write_process(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title.c_str());
        add_headers(error_500_form.size());
        if (!add_content(error_500_form.c_str()))
        {
            return false;
        }
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title.c_str());
        add_headers(error_404_form.size());
        if (!add_content(error_404_form.c_str()))
        {
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title.c_str());
        add_headers(error_403_form.size());
        if (!add_content(error_403_form.c_str()))
        {
            return false;
        }
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title.c_str());

        if (_file_stat.st_size != 0)
        {
            // 1.客户端请求了有效的服务器资源
            add_headers(_file_stat.st_size);

            // 挂载状态行和响应头
            _iov[0].iov_base = _out_buffer;
            _iov[0].iov_len = _write_idx;

            // 挂载有效载荷
            _iov[1].iov_base = _file_map_addr;
            _iov[1].iov_len = _file_stat.st_size;

            _iov_count = 2;
            _bytes_not_send = _write_idx + _file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
            {
                return false;
            }
            break;
        }
    }
    default:
        return false;
    }

    _iov[0].iov_base = _out_buffer;
    _iov[0].iov_len = _write_idx;
    _iov_count = 1;
    _bytes_not_send = _write_idx;
    return true;
}
