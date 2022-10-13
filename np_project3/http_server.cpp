#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/asio/write.hpp>
#include <sys/wait.h>
#include <sys/types.h>
#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::asio::detail::socket_ops;

#define buf_size 8192

typedef struct header{
    string name;
    string value;
} header;

typedef struct request{
    string method;
    string cgi_path;
    string query;
    int http_version_first;
    int http_version_second;
    vector<header> header_list;
} request;

enum phase{
    method, cgi, query_parameter, http, http_version_f, http_version_s, header_new, header_name, header_value 
};

class request_parser
{
public:
    bool parse(request& req, char* input, int size){
        int phase_now = method;
        for(int i = 0; i < size; i++){
            if(phase_now == method){
                if(is_alpha(input[i])){
                    req.method.push_back(input[i]);
                }
                else if(input[i] == ' '){
                    phase_now = cgi;
                }
                else{
                    return false;
                }
            }
            else if(phase_now == cgi){
                if(input[i] == ' '){
                    phase_now = http;
                }
                else if(input[i] == '?'){
                    phase_now = query_parameter;
                }
                else if(is_char(input[i])){
                    req.cgi_path.push_back(input[i]);
                }
                else{
                    return false;
                }
            }
            else if(phase_now == query_parameter){
                if(input[i] == ' '){
                    phase_now = http;
                }
                else if(is_char(input[i])){
                    req.query.push_back(input[i]);
                }
                else{
                    return false;
                }
            }
            else if(phase_now == http){
                if((size - i) >= 5){
                    if(input[i] == 'H' && input[i+1] == 'T' && input[i+2] == 'T' && input[i+3] == 'P' && input[i+4] == '/'){
                        req.http_version_first = 0;
                        req.http_version_second = 0;
                        phase_now = http_version_f;
                        i += 4;
                    }
                    else{
                        return false;
                    }
                }
                else{
                    return false;
                }
            }
            else if(phase_now == http_version_f){
                if(is_digit(input[i])){
                    req.http_version_first = req.http_version_first * 10 + (input[i] - '0');
                }
                else if(input[i] == '.'){
                    phase_now = http_version_s;
                }
                else{
                    return false;
                }
            }
            else if(phase_now == http_version_s){
                if(is_digit(input[i])){
                    req.http_version_second = req.http_version_second * 10 + (input[i] - '0');
                }
                else if(input[i] == '\r' && (size - i) >= 1){
                    if(input[i+1] == '\n'){
                        phase_now = header_new;
                        i += 1;
                    }
                    else{
                        return false;
                    }
                }
                else{
                    return false;
                }
            }
            else if(phase_now == header_new){
                if (input[i] == '\r' && (size - i) >= 1)
                {
                    if (input[i+1] == '\n')
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                else if(is_ctl(input[i])){
                    return false;
                }
                else{
                    req.header_list.push_back(header());
                    req.header_list.back().name.push_back(input[i]);
                    phase_now = header_name;
                }
            }
            else if(phase_now == header_name){
                if (input[i] == ':' && (size - i) >= 1)
                {
                    if (input[i+1] == ' '){
                        phase_now = header_value;
                        i += 1;
                    }
                    else{
                        return false;
                    }
                }
                else if (is_ctl(input[i]))
                {
                    return false;
                }
                else{
                    req.header_list.back().name.push_back(input[i]);
                }
            }
            else if(phase_now == header_value){
                if (input[i] == '\r' && (size - i) >= 1)
                {
                    if (input[i + 1] == '\n')
                    {
                        phase_now = header_new;
                        i += 1;
                    }
                    else
                    {
                        return false;
                    }
                }
                else if (is_ctl(input[i]))
                {
                    return false;
                }
                else{
                    req.header_list.back().value.push_back(input[i]);
                }
            }
        }
        return false;
    }
private:
    bool is_alpha(int c){
        return (c >= 65 && c <= 90) || (c >= 97 && c <= 122);
    }
    bool is_char(int c){
        return c >= 0 && c <= 127;
    }
    bool is_ctl(int c){
        return (c >=0 && c <= 31) || (c == 127);
    }
    bool is_digit(int c){
        return c >= '0' && c <= '9';
    }
};

class connection
    : public std::enable_shared_from_this<connection>
{
public:
    connection(io_service &ioservice, tcp::socket socket)
        : io_service_(ioservice), socket_(move(socket))
    {
    }

    void start(){
        do_read();
    }

private:
    void do_read(){
        auto self(shared_from_this());
        socket_.async_read_some(buffer(buffer_, buf_size), 
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
        {
            if(!ec){
                bool result = request_parser_.parse(request_, buffer_, bytes_transferred);
                if(result){
                    cout << "parse success!" << endl;
                    //debug parsing
                    /*
                    cout << "method = " << request_.method << endl;
                    cout << "cgi path = " << request_.cgi_path << endl;
                    cout << "query string = " << request_.query << endl;
                    cout << "http_version_first = " << request_.http_version_first << endl;
                    cout << "http_version_second = " << request_.http_version_second << endl;
                    for(int i = 0; i < request_.header_list.size(); i++){
                        cout << "header name = " << request_.header_list[i].name << endl;
                        cout << "header value = " << request_.header_list[i].value << endl;
                    }
                    */
                    
                    //start fork
                    io_service_.notify_fork(io_service::fork_prepare);
                    pid_t pid;
                    while ((pid = fork()) < 0)
                    {
                        waitpid(-1, NULL, 0);
                    }
                    if(pid == 0){
                        io_service_.notify_fork(io_service::fork_child);
                        do_write("HTTP/1.1 200 OK\r\n");
                        //set environment variable
                        const char *c1 = "REQUEST_METHOD";
                        const char *method = request_.method.c_str();
                        setenv(c1,method,1);
                        const char *c2 = "REQUEST_URI";
                        const char *uri = request_.cgi_path.c_str();
                        setenv(c2, uri, 1);
                        const char *c3 = "QUERY_STRING";
                        const char *query_str = request_.query.c_str();
                        setenv(c3, query_str, 1);
                        const char *c4 = "SERVER_PROTOCOL";
                        const char *s_protocol = "HTTP/1.1";
                        setenv(c4, s_protocol, 1);
                        const char *c5 = "HTTP_HOST";
                        string host;
                        for(int i = 0; i < (int)request_.header_list.size(); i++){
                            if(request_.header_list[i].name == "Host"){
                                host = request_.header_list[i].value;
                            }
                        }
                        const char *http_host = host.c_str();
                        setenv(c5, http_host, 1);
                        const char *c6 = "SERVER_ADDR";
                        const char *s_addr = socket_.local_endpoint().address().to_string().c_str();
                        setenv(c6, s_addr, 1);
                        const char *c7 = "SERVER_PORT";
                        const char *s_port = to_string(socket_.local_endpoint().port()).c_str();
                        setenv(c7, s_port, 1);
                        const char *c8 = "REMOTE_ADDR";
                        const char *r_addr = socket_.remote_endpoint().address().to_string().c_str();
                        setenv(c8, r_addr, 1);
                        const char *c9 = "REMOTE_PORT";
                        const char *r_port = to_string(socket_.remote_endpoint().port()).c_str();
                        setenv(c9, r_port, 1);

                        int socket = socket_.native_handle();
                        dup2(socket,STDIN_FILENO);
                        dup2(socket,STDOUT_FILENO);
                        socket_.close();
                        string path = "." + request_.cgi_path;
                         const char *p = path.c_str();
                        if(execl(p,p,NULL) < 0){
                            perror("Execute: ");
                        }
                        exit(0);
                    }
                    else{
                        io_service_.notify_fork(io_service::fork_parent);
                        socket_.close();
                    }
                    
                }
                else{
                    reply = "HTTP/1.0 400 Bad Request\r\n";
                    string content = "<html><head><title>Bad Request</title></head><body><h1>400 Bad Request</h1></body></html>";
                    reply.append("Content-Length: ");
                    reply.append(to_string(content.length()));
                    reply.append("\r\n");
                    reply.append("Content-Type: text/html");
                    reply.append("\r\n");
                    reply.append("\r\n");
                    reply.append(content);
                    do_write(reply);
                }
            }
        });
    }
    void do_write(string message){
        auto self(shared_from_this());
        async_write(socket_, buffer(message),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (!ec)
            {
                socket_.shutdown(tcp::socket::shutdown_both);
                socket_.close();
            }
        });
    }
    char buffer_[buf_size];
    string reply;
    tcp::socket socket_;
    request_parser request_parser_;
    request request_;
    io_service& io_service_;
};

class server{
public:
    server(io_service &ioservice, short port)
    : io_service_(ioservice), tcp_acceptor(ioservice, tcp::endpoint(tcp::v4(), port)),tcp_socket(ioservice)
    {
        tcp_acceptor.listen();
        do_accept();
    }
private:
    void do_accept(){
        tcp_acceptor.async_accept(tcp_socket,
            [this](boost::system::error_code ec) {
                if (!ec)
                {
                    std::make_shared<connection>(io_service_,move(tcp_socket))->start();
                }

                do_accept();
            });
    }
    tcp::acceptor tcp_acceptor;
    tcp::socket tcp_socket;
    io_service& io_service_;
};

void childHandler(int signo)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
    {
    }
}

int main(int argc, char *argv[])
{
    signal(SIGCHLD, childHandler);
    io_service ioservice;
    server s(ioservice,atoi(argv[1]));
    ioservice.run();
}
