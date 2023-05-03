#include "common/WebServer.hpp"

int main(int argc, char **argv)
{
    std::string user = "root";
    std::string passwd = "liang123";
    std::string dbname = "mydb";

    WebServer server;

    server.init(8080,user,passwd,dbname,0,0,0,8,8,0,1);

    server.log_write();

    server.sql_pool();

    server.thread_pool();

    server.trig_mode();

    server.event_listen();
    
    server.event_loop();
    return 0;
}