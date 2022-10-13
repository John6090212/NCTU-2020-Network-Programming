#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <sys/wait.h>
#include <sys/types.h>
#include <memory>
#include <iostream>
#include <fstream>
#include <string>

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

#define buf_size 8192



class server_connection
    : public std::enable_shared_from_this<server_connection>
{
public:
    server_connection(io_service &ioservice, string hostname, string port, tcp::socket client_socket)
        : io_service_(ioservice), resolver_(ioservice), query_(hostname, port), server_socket_(ioservice), client_socket_(move(client_socket))
    {
        server_read_end = false;
        client_read_end = false;
    }

    void start()
    {
        resolve();
    }

private:
    void resolve()
    {
        auto self(shared_from_this());
        resolver_.async_resolve(query_,
            [this, self](const boost::system::error_code &ec, tcp::resolver::iterator iter) {
                if (!ec)
                {
                    //cout << "start connect" << endl;
                    connect(iter);
                }
            });
    }
    void connect(tcp::resolver::iterator iter)
    {
        auto self(shared_from_this());
        server_socket_.async_connect(iter->endpoint(),
            [this, self](const boost::system::error_code &ec) {
                if (!ec)
                {
                    //cout << "start read" << endl;
                    do_server_read();
                    do_client_read();
                }
                else
                {
                    cerr << "error " << ec.message() << endl;
                }
            });
    }
    void do_server_read()
    {
        auto self(shared_from_this());
        server_socket_.async_read_some(buffer(server_data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    //cout << "read from server: "  << length << " bytes" << endl;
                    //cout << "start write to client" << endl;
                    do_client_write(length);
                }
                else
                {
                    cout << "read server error: " << ec.message() << endl;
                    server_read_end = true;
                    server_socket_.shutdown(tcp::socket::shutdown_send, ec);
                    client_socket_.shutdown(tcp::socket::shutdown_receive, ec);
                    if(client_read_end){
                        server_socket_.close();
                        client_socket_.close();
                        exit(0);
                    }
                }
            });
    }
    void do_client_read(){
        auto self(shared_from_this());
        client_socket_.async_read_some(buffer(client_data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    //cout << "read from client: "  << length << " bytes" << endl;
                    //cout << "start write to server" << endl;
                    do_server_write(length);
                }
                else
                {
                    cout << "read client error: " << ec.message() << endl;
                    client_read_end = true;
                    client_socket_.shutdown(tcp::socket::shutdown_send, ec);
                    server_socket_.shutdown(tcp::socket::shutdown_receive, ec);
                    if(server_read_end){
                        server_socket_.close();
                        client_socket_.close();
                        exit(0);
                    }
                }
            });
    }
    void do_server_write(size_t length)
    {
        auto self(shared_from_this());
        async_write(server_socket_,buffer(client_data_, length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    //cout << "write to server: "  << length << " bytes" << endl;
                    //cout << "start read again" << endl;
                    do_client_read();
                }
                else
                {
                    cout << "write server error: " << ec.message() << endl;
                }
            });
    }
    void do_client_write(size_t length){
        auto self(shared_from_this());
        async_write(client_socket_,buffer(server_data_, length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    //cout << "write to client: "  << length << " bytes" << endl;
                    //cout << "start read again" << endl;
                    do_server_read();
                }
                else
                {
                    cout << "write client error: " << ec.message() << endl;
                }
            });
    }
    enum
    {
        max_length = 8192
    };
    array<char, max_length> client_data_;
    array<char, max_length> server_data_;
    tcp::socket server_socket_;
    tcp::socket client_socket_;
    tcp::resolver resolver_;
    tcp::resolver::query query_;
    io_service &io_service_;
    bool server_read_end;
    bool client_read_end;
};

class ftp_connection
    : public std::enable_shared_from_this<ftp_connection>
{
public:
    ftp_connection(io_service &ioservice, tcp::socket client_socket)
        : io_service_(ioservice), server_socket_(ioservice), tcp_acceptor(ioservice), endpoint(tcp::v4(), 0), client_socket_(move(client_socket))
    {
        server_read_end = false;
        client_read_end = false;
    }

    void start(bool is_success)
    {
        do_bind(is_success);
    }

private:
    void do_bind(bool is_success){
        auto self(shared_from_this());
        tcp_acceptor.open(endpoint.protocol());
        tcp_acceptor.set_option(tcp::acceptor::reuse_address(true));
        tcp_acceptor.bind(endpoint);
        tcp_acceptor.listen();
        u_short port = tcp_acceptor.local_endpoint().port();
        //cout << "bind port is " << port << endl;
        reply[0] = 0;
        reply[1] = (is_success ? 90 : 91);
        reply[2] = (u_char)(port / 256);
        reply[3] = (u_char)(port % 256);
        for(int i = 4; i < 8; i++){
            reply[i] = 0;
        }
        /*
        cout << "initial message = ";
        for(int i = 0; i < 8; i ++){
            cout << (unsigned int)reply[i] << " ";
        }
        cout << endl;
        */
        do_bind_reply();
    }
    void do_ftp_accept(){
        auto self(shared_from_this());
        tcp_acceptor.async_accept(server_socket_,
            [this, self](boost::system::error_code ec) {
            if (!ec)
            {
                /*
                cout << "second reply message = ";
                for(int i = 0; i < 8; i ++){
                    cout << (unsigned int)reply[i] << " ";
                }
                cout << endl;
                */
                async_write(client_socket_, buffer(reply, 8),
                    [this, self](boost::system::error_code ec, std::size_t length) {
                        if (!ec)
                        {
                            tcp_acceptor.close();
                            do_server_read();
                            do_client_read();
                        }
                        else{
                            cout << "do bind reply error: " << ec.message() << endl;
                        }
                    });
            }
            else{
                cout << "accept error: " << ec.message() << endl;
            }
        });
    }
    void do_bind_reply(){
        auto self(shared_from_this());
        /*
        cout << "first reply message = ";
        for(int i = 0; i < 8; i ++){
            cout << (unsigned int)reply[i] << " ";
        }
        cout << endl;
        */
        async_write(client_socket_, buffer(reply, 8),
            [ this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    if(reply[1] == 90)
                        do_ftp_accept();
                    else{
                        cout << "reject" << endl;
                        exit(0);
                    }
                }
                else{
                    cout << "do bind reply error: " << ec.message() << endl;
                }
            });
    }
    void do_server_read()
    {
        auto self(shared_from_this());
        server_socket_.async_read_some(buffer(server_data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    //cout << "read from server: "  << length << " bytes" << endl;
                    //cout << "start write to client" << endl;
                    do_client_write(length,false,"False");
                }
                else
                {
                    cout << "read server error: " << ec.message() << endl;
                    server_read_end = true;
                    server_socket_.shutdown(tcp::socket::shutdown_send, ec);
                    client_socket_.shutdown(tcp::socket::shutdown_receive, ec);
                    if(client_read_end){
                        server_socket_.close();
                        client_socket_.close();
                        exit(0);
                    }
                }
            });
    }
    void do_client_read(){
        auto self(shared_from_this());
        client_socket_.async_read_some(buffer(client_data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    //cout << "read from client: "  << length << " bytes" << endl;
                    //cout << "start write to server" << endl;
                    do_server_write(length,false,"False");
                }
                else
                {
                    cout << "read client error: " << ec.message() << endl;
                    client_read_end = true;
                    client_socket_.shutdown(tcp::socket::shutdown_send, ec);
                    server_socket_.shutdown(tcp::socket::shutdown_receive, ec);
                    if(server_read_end){
                        server_socket_.close();
                        client_socket_.close();
                        exit(0);
                    }
                }
            });
    }
    void do_server_write(size_t length, bool not_equal, string not_send_data)
    {
        auto self(shared_from_this());
        if(not_equal){
            async_write(server_socket_,buffer(not_send_data, not_send_data.size()),
                [this, self, length, not_send_data](boost::system::error_code ec, std::size_t w_length) {
                    if (!ec)
                    {
                        if(w_length != length){
                            cout << "read from client: " << length << " bytes" << endl;
                            cout << "write to server: "  << w_length << " bytes" << endl;
                            cout << "in server write: length not equal!" << endl;
                            string read_data(not_send_data.begin()+w_length, not_send_data.begin() + length);
                            do_server_write(length-w_length,true,read_data);
                        }         
                        else{
                            do_client_read();
                        }     
                    }
                    else
                    {
                        cout << "write server error: " << ec.message() << endl;
                    }
                });
        }
        else{
            async_write(server_socket_,buffer(client_data_, length),
                [this, self, length](boost::system::error_code ec, std::size_t w_length) {
                    if (!ec)
                    {   
                        if(w_length != length){
                            cout << "read from client: " << length << " bytes" << endl;
                            cout << "write to server: "  << w_length << " bytes" << endl;
                            cout << "in server write: length not equal!" << endl;
                            string read_data(client_data_.begin()+w_length, client_data_.begin() + length);
                            do_server_write(length-w_length,true,read_data);
                        }         
                        else{
                            do_client_read();
                        }     
                    }
                    else
                    {
                        cout << "write server error: " << ec.message() << endl;
                    }
                });
        }
    }
    void do_client_write(size_t length, bool not_equal, string not_send_data){
        auto self(shared_from_this());
        if(not_equal){
            async_write(client_socket_,buffer(not_send_data, length),
                [this, self, length, not_send_data](boost::system::error_code ec, std::size_t w_length) {
                    if (!ec)
                    {
                        if(w_length != length){
                            cout << "read from server: " << length << " bytes" << endl;
                            cout << "write to client: "  << w_length << " bytes" << endl;
                            cout << "in client write: length not equal!" << endl;
                            string read_data(not_send_data.begin()+w_length, not_send_data.begin() + length);
                            do_client_write(length-w_length,true,read_data);
                        }
                        else{
                            do_server_read();
                        }
                    }
                    else
                    {
                        cout << "write client error: " << ec.message() << endl;
                    }
                });
        }
        else{
            async_write(client_socket_,buffer(server_data_, length),
                [this, self, length](boost::system::error_code ec, std::size_t w_length) {
                    if (!ec)
                    {
                        
                        if(w_length != length){
                            cout << "read from server: " << length << " bytes" << endl;
                            cout << "write to client: "  << w_length << " bytes" << endl;
                            cout << "in client write: length not equal!" << endl;
                            string read_data(server_data_.begin()+w_length, server_data_.begin() + length);
                            do_client_write(length-w_length,true,read_data);
                        }
                        else{
                            do_server_read();
                        }
                    }
                    else
                    {
                        cout << "write client error: " << ec.message() << endl;
                    }
                });
        }
    }
    enum
    {
        max_length = 8192
    };
    array<char, max_length> client_data_;
    array<char, max_length> server_data_;
    tcp::socket server_socket_;
    tcp::socket client_socket_;
    io_service &io_service_;
    u_char reply[8];
    tcp::endpoint endpoint;
    tcp::acceptor tcp_acceptor;
    bool server_read_end;
    bool client_read_end;
};

class connection
    : public std::enable_shared_from_this<connection>
{
public:
    connection(io_service &ioservice, tcp::socket socket)
        : io_service_(ioservice), socket_(move(socket))
    {
    }

    void start()
    {
        do_read();
    }

private:
    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(buffer(buffer_, buf_size),
            [this, self](boost::system::error_code ec, std::size_t bytes_transferred) {
                if (!ec)
                {
                    //parse socks4 request
                    u_char VN = buffer_[0];
                    u_char CD = buffer_[1];
                    u_short DSTPORT = u_short(buffer_[2] << 8) + u_short(buffer_[3]);
                    boost::asio::ip::address_v4::bytes_type IP;
                    for(int i = 4; i <= 7; i++)
                        IP[i-4] = buffer_[i];
                    boost::asio::ip::address_v4 DSTIP(IP); 
                    if (VN != 4)
                    {
                        cerr << "VN error" << endl;
                    }
                    char *user_id = (char *)buffer_ + 8;
                    char *domain_name = "No";
                    if (buffer_[4] == 0 && buffer_[5] == 0 && buffer_[6] == 0 && buffer_[7] != 0)
                    {
                        domain_name = user_id + strlen(user_id) + 1;
                    }
                    
                    //cout << "user_id = " << user_id << endl;
                    //cout << "domain_name = " << domain_name << endl;
                    string domain(domain_name);
                    if(domain_name != "No"){
                        //cout << "domain: " << domain << endl;
                        //cout << "start resolve" << endl;
                        tcp::resolver::query query_(domain,to_string(DSTPORT));
                        tcp::resolver resolver_(io_service_);
                        tcp::resolver::iterator iter;
                        iter = resolver_.resolve(query_);
                        tcp::resolver::iterator end;
                        while(iter != end){
                            if(iter->endpoint().address().is_v4()){
                                cout << "resolve ip:" << iter->endpoint().address().to_v4().to_string() << endl;
                                boost::asio::ip::address_v4::bytes_type resolve_ip = iter->endpoint().address().to_v4().to_bytes();
                                for (int i = 4; i <= 7; i++)
                                {
                                    buffer_[i] = resolve_ip[i - 4];
                                }
                            }
                            iter++;
                        }
                    }  
                                
                    u_char reply[8] = {0};
                    reply[0] = 0;
                    reply[1] = 90;
                    //read firewall file
                    fstream fin("socks.conf");
                    if (fin)
                    {
                        string permit, code, ip, ip_part[4];
                        //deny all traffic by default
                        reply[1] = 91;
                        int count = 0;
                        while (fin >> permit >> code >> ip)
                        {
                            //cout << "rule " << count << ": " << permit << " " << code << " " << ip << endl;
                            for (int i = 0; i < 4; i++)
                            {
                                auto pos = ip.find_first_of('.');
                                if (pos != string::npos)
                                {
                                    ip_part[i] = ip.substr(0, pos);
                                    ip.erase(0, pos + 1);
                                }
                                else
                                {
                                    ip_part[i] = ip;
                                }
                            }
                            
                            //debug ip and buffer
                            /*
                            for(int i = 0; i < 4; i++){
                                if(ip_part[i] == "*")
                                    cout << "ip[" << i << "] = *" << endl;
                                else
                                    cout << "ip[" << i << "] = " << stoul(ip_part[i]) << endl;
                                cout << "buffer[" << i+4 << "] = " << (unsigned long)(buffer_[i]) << endl;
                            }
                            */
                            if ((code == "c" && CD == 1) || (code == "b" && CD == 2))
                            {
                                if ((ip_part[0] == "*" || ((u_char)stoul(ip_part[0]) == buffer_[4])) &&
                                    (ip_part[1] == "*" || ((u_char)stoul(ip_part[1]) == buffer_[5])) &&
                                    (ip_part[2] == "*" || ((u_char)stoul(ip_part[2]) == buffer_[6])) &&
                                    (ip_part[3] == "*" || ((u_char)stoul(ip_part[3]) == buffer_[7])))
                                {
                                    reply[1] = 90;
                                    break;
                                }
                            }
                            count++;
                        }
                        fin.close();
                    }
                    else
                    {
                        cout << "Firewall file doesn't exist!" << endl;
                    }

                    //print server message
                    cout << "<S_IP>: " << socket_.local_endpoint().address().to_string() << endl;
                    cout << "<S_PORT>: " << to_string(socket_.local_endpoint().port()) << endl;
                    cout << "<D_IP>: " << DSTIP.to_string() << endl;
                    cout << "<D_PORT>: " << DSTPORT << endl;
                    cout << "<Command>: " << ((CD == 1) ? "CONNECT" : "BIND") << endl;
                    cout << "<Reply>: " << ((reply[1] == 90) ? "ACCEPT" : "REJECT") << endl;

                    //generate reply
                    if (CD == 1)
                    {
                        if (string(domain_name) == "No")
                            do_reply(reply, DSTIP.to_string(), to_string(DSTPORT), (reply[1] == 90));
                        else
                            do_reply(reply, string(domain_name), to_string(DSTPORT), (reply[1] == 90));
                    }
                    else if (CD == 2)
                    {
                        if (reply[1] == 90)
                        {
                            make_shared<ftp_connection>(io_service_, move(socket_))->start(true);
                        }
                        else
                        {
                            make_shared<ftp_connection>(io_service_, move(socket_))->start(false);
                        }
                    }
                }
            });
    }
    void do_reply(u_char *message, string hostname, string port, bool accept)
    {
        auto self(shared_from_this());
        async_write(socket_, buffer(message, 8),
            [this, self, hostname, port, message, accept](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    cout << "hostname = " << hostname << endl;
                    cout << "port = " << port << endl;
                    if(accept){
                        cout << "success" << endl;
                        cout << "start server connection" << endl;
                        make_shared<server_connection>(io_service_, hostname, port, move(socket_))->start();
                    }
                    else{
                        cout << "reject" << endl;
                        exit(0);
                    }
                }
            });
    }

    unsigned char buffer_[buf_size];
    tcp::socket socket_;
    io_service &io_service_;
};

class server
{
public:
    server(io_service &ioservice, short port)
        : io_service_(ioservice), tcp_acceptor(ioservice, tcp::endpoint(tcp::v4(), port)), tcp_socket(ioservice)
    {
        tcp_acceptor.listen();
        do_accept();
    }

private:
    void do_accept()
    {
        //cout << "start do accept" << endl;
        //cout << "tcp_socket: " << tcp_socket.native_handle() << endl;
        tcp_acceptor.async_accept(tcp_socket,
            [this](boost::system::error_code ec) {
                if (!ec)
                {
                    //cout << "accept" << endl;
                    //cout << "socket: " << tcp_socket.native_handle() << endl;
                    //start fork
                    pid_t pid;
                    io_service_.notify_fork(io_service::fork_prepare);
                    while ((pid = fork()) < 0)
                    {
                        waitpid(-1, NULL, 0);
                    }
                    if (pid == 0)
                    {
                        io_service_.notify_fork(io_service::fork_child);
                        tcp_acceptor.close();
                        std::make_shared<connection>(io_service_, move(tcp_socket))->start(); 
                    }
                    else{
                        io_service_.notify_fork(io_service::fork_parent);
                        tcp_socket.close();
                        do_accept();
                    }
                }
                else
                {
                    cerr << "Accept error: " << ec.message() << std::endl;
                    do_accept();
                }        
            });
    }
    tcp::acceptor tcp_acceptor;
    tcp::socket tcp_socket;
    io_service &io_service_;
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
    server s(ioservice, atoi(argv[1]));
    ioservice.run();
}
