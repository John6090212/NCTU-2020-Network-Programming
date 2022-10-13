#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/thread/thread.hpp>
#include <boost/mem_fn.hpp>
#include <iostream>
#include <string>
#include <map>
#include <memory>
#include <cstdlib>
#include <vector>
#include <fstream>


using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::asio::detail::socket_ops;

#define buf_size 8192

io_service ioservice;

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

typedef struct parameter{
    string hostname;
    string port;
    string filename;
} parameter;

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

void parse(string query_string,vector<parameter> &parameter_list){
    string h;
    string p;  
    string f;
    size_t prev = 0;
    size_t current = 0;
    string temp;
    
    for(int i = 0; i <= 4; i++){
        h.clear();
        p.clear();
        f.clear();  
        if(prev == 0)
            current = query_string.find("&", prev);     
        else
        {
            current = query_string.find("&", prev+1);
        }
         
        if(current != string::npos){
            if(prev == 0)
                temp = query_string.substr(prev, current - prev);
            else{
                temp = query_string.substr(prev + 1, current - prev - 1);
            }                
            if(temp.find("=") + 1 < temp.size())
                h = temp.substr(temp.find("=") + 1,string::npos);           
        }
        prev = current;
        current = query_string.find("&", prev+1);       
        if (current != string::npos)
        {
            temp = query_string.substr(prev+1, current - prev - 1);
            if(temp.find("=") + 1 < temp.size())
                p = temp.substr(temp.find("=") + 1, string::npos);
        }
        prev = current;
        current = query_string.find("&", prev + 1);
        if (current != string::npos)
        {
            temp = query_string.substr(prev + 1, current - prev - 1);
            if (temp.find("=") + 1 < temp.size())
                f = temp.substr(temp.find("=") + 1, string::npos);
        }
        if(i == 4){
            temp = query_string.substr(prev + 1, string::npos);
            if (temp.find("=") + 1 < temp.size())
                f = temp.substr(temp.find("=") + 1, string::npos);
        }
        if(h.size() && p.size() && f.size()){
            parameter param;
            param.hostname = h;
            param.port = p;
            param.filename = f;
            parameter_list.push_back(param);
        }
        prev = current;
    }
}

void output_shell(unsigned int id, string data, shared_ptr<tcp::socket> socket_){
    //cout << "start output shell" << endl;
    //cout << "shell html socket = " << socket_->native_handle() << endl;
    boost::algorithm::replace_all(data, "&", "&amp;");
    boost::algorithm::replace_all(data, "\n", "&NewLine;");
    boost::algorithm::replace_all(data, "\r", "");
    boost::algorithm::replace_all(data, "\'", "\\\'"); 
    boost::algorithm::replace_all(data, "\"", "\\\"");
    boost::algorithm::replace_all(data, "<", "&#60;");
    boost::algorithm::replace_all(data, ">", "&#62;");   
    string script = "<script>document.getElementById('s" + to_string(id) + "').innerHTML += '" + data + "';</script>\n";
    socket_->async_write_some(buffer(script),
        [](boost::system::error_code ec, std::size_t) {
            if (!ec)
            { 
                //cout << "output finish" << endl;
            }
        });
}

void output_command(unsigned int id, string data, shared_ptr<tcp::socket> socket_){
    //cout << "start output cmd" << endl;
    //cout << "cmd html socket = " << socket_->native_handle() << endl;
    boost::algorithm::replace_all(data, "&", "&amp;");
    boost::algorithm::replace_all(data, "\n", "&NewLine;");
    boost::algorithm::replace_all(data, "\r", "");
    boost::algorithm::replace_all(data, "\'", "\\\'");
    boost::algorithm::replace_all(data, "<", "&#60;");
    boost::algorithm::replace_all(data, ">", "&#62;");   
    string script = "<script>document.getElementById('s" + to_string(id) + "').innerHTML += '<b>" + data + "</b>';</script>\n";
    socket_->async_write_some(buffer(script),
        [](boost::system::error_code ec, std::size_t) {
            if (!ec)
            { 
            }
        });
}

class np_connection
    : public std::enable_shared_from_this<np_connection>
{
public:
    np_connection(string hostname, string port, unsigned int id, shared_ptr<tcp::socket> socket)
    : resolver_(ioservice),query_(hostname, port),socket_(ioservice),html_socket_(socket),connection_id(id)
    { 
    }

    void start(string filename){
        //cout << "html socket = " << html_socket_->native_handle() << endl;
        fin.open("./test_case/" + filename);
        if(!fin){
            cerr << "fin error in session " << connection_id << endl;
        }
        resolve();
    }

private:
    void resolve(){
        auto self(shared_from_this());    
        resolver_.async_resolve(query_,
            [this,self](const boost::system::error_code &ec, tcp::resolver::iterator iter) {
                if (!ec)
                {
                    cerr << "start connect" << endl;
                    connect(iter);
                }
            }
        );
    }
    void connect(tcp::resolver::iterator iter){
        auto self(shared_from_this());
        socket_.async_connect(iter->endpoint(),
            [this, self](const boost::system::error_code &ec) {
                if(!ec){
                    cerr << "start read" << endl;
                    do_read();
                }
                else{
                    cerr << "error " << ec.value() << endl;
                }
            }
        );
    }
    void do_read(){
        auto self(shared_from_this());
        socket_.async_read_some(buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec)
                {
                    string read_data(data_.begin(),data_.begin()+length);
                    output_shell(connection_id, read_data, html_socket_);
                    if(read_data.find("% ") != string::npos){
                        do_write();
                    }
                    do_read();
                }
            }
        );
    }
    void do_write(){
        auto self(shared_from_this());
        string cmd;
        if(getline(fin, cmd)){
            cmd = cmd + "\n";
            socket_.async_write_some(buffer(cmd, cmd.size()),
                [this, self, cmd](boost::system::error_code ec, std::size_t length){
                    if (!ec){
                        output_command(connection_id, cmd, html_socket_);
                    }
                }
            );
        }
    }
    enum
    {
        max_length = 1024
    };
    array<char, max_length> data_;
    tcp::socket socket_;
    shared_ptr<tcp::socket> html_socket_;
    tcp::resolver resolver_;
    tcp::resolver::query query_;
    unsigned int connection_id;
    ifstream fin;
};

class connection
    : public std::enable_shared_from_this<connection>
{
public:
    connection(io_service &ioservice, shared_ptr<tcp::socket> socket)
        : io_service_(ioservice), socket_(move(socket))
    {
    }

    void start(){
        do_read();
    }

private:
    void do_read(){
        auto self(shared_from_this());
        socket_->async_read_some(buffer(buffer_, buf_size), 
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
                    //do_write("HTTP/1.1 200 OK\r\n");
                    /*
                    reply = "HTTP/1.1 200 OK\r\n";
                    string content = "<html><head><title>Test</title></head><body><h1>Hello_world</h1></body></html>";
                    reply.append("Content-Length: ");
                    reply.append(to_string(content.length()));
                    reply.append("\r\n");
                    reply.append("Content-Type: text/html");
                    reply.append("\r\n");
                    reply.append("\r\n");
                    reply.append(content);
                    do_write(reply);  
                    */
                    if(request_.cgi_path.find("/panel.cgi") != string::npos){
                        cout << "path is /panel.cgi" << endl;
                        //cout << socket_->native_handle() << endl;
                        load_panel();
                        cout << "do_panel_write end" << endl;
                    }
                    else if(request_.cgi_path.find("/console.cgi") != string::npos){
                        cout << "path is /console.cgi" << endl;
                        load_console();
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
                    do_write(reply,true);                  
                }
            }
        });
    }
    void do_write(string message, bool end){
        auto self(shared_from_this());
        socket_->async_write_some(buffer(message),
        [this, self, end](boost::system::error_code ec, std::size_t) {
            if (!ec)
            {
                if(end){
                    socket_->shutdown(tcp::socket::shutdown_both);
                    socket_->close();
                }    
            }
        });
    }
    void load_panel(){
        do_write("HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n",false);
        
        string html_start = R"(
            <!DOCTYPE html>
            <html lang="en">
            <head>
                <title>NP Project 3 Panel</title>
                <link
                rel="stylesheet"
                href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
                integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
                crossorigin="anonymous"
                />
                <link
                href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
                rel="stylesheet"
                />
                <link
                rel="icon"
                type="image/png"
                href="https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png"
                />
                <style>
                * {
                    font-family: 'Source Code Pro', monospace;
                }
                </style>
            </head>
            <body class="bg-secondary pt-5">
        )";
        do_write(html_start,false);
        string html_form_start = R"(
            <form action="console.cgi" method="GET">
            <table class="table mx-auto bg-light" style="width: inherit">
                <thead class="thead-dark">
                <tr>
                    <th scope="col">#</th>
                    <th scope="col">Host</th>
                    <th scope="col">Port</th>
                    <th scope="col">Input File</th>
                </tr>
                </thead>
                <tbody>
        )";
        do_write(html_form_start,false);
        for(int i = 0; i < 5; i++){
            string html_form_middle = R"(
                <tr>
                    <th scope="row" class="align-middle">Session )" 
                + to_string(i + 1)
                + R"( </th>
                    <td>
                    <div class="input-group">
                        <select name="h)"
                + to_string(i)
                + R"(" class="custom-select">
                        <option></option>
                        <option value="nplinux1.cs.nctu.edu.tw">nplinux1</option>
                        <option value="nplinux2.cs.nctu.edu.tw">nplinux2</option>
                        <option value="nplinux3.cs.nctu.edu.tw">nplinux3</option>
                        <option value="nplinux4.cs.nctu.edu.tw">nplinux4</option>
                        <option value="nplinux5.cs.nctu.edu.tw">nplinux5</option>
                        <option value="nplinux6.cs.nctu.edu.tw">nplinux6</option>
                        <option value="nplinux7.cs.nctu.edu.tw">nplinux7</option>
                        <option value="nplinux8.cs.nctu.edu.tw">nplinux8</option>
                        <option value="nplinux9.cs.nctu.edu.tw">nplinux9</option>
                        <option value="nplinux10.cs.nctu.edu.tw">nplinux10</option>
                        <option value="nplinux11.cs.nctu.edu.tw">nplinux11</option>
                        <option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                        </select>
                        <div class="input-group-append">
                        <span class="input-group-text">.cs.nctu.edu.tw</span>
                        </div>
                    </div>
                    </td>
                    <td>
                )";
            string input = R"(<input name="p)" 
                + to_string(i) 
                + R"(" type="text" class="form-control" size="5" />
                    </td>
                    <td>
                    <select name="f)"
                + to_string(i)
                + R"(" class="custom-select">
                    <option></option>
                    <option value="t1.txt">t1.txt</option>
                    <option value="t2.txt">t2.txt</option>
                    <option value="t3.txt">t3.txt</option>
                    <option value="t4.txt">t4.txt</option>
                    <option value="t5.txt">t5.txt</option>
                    <option value="t6.txt">t6.txt</option>
                    <option value="t7.txt">t7.txt</option>
                    <option value="t8.txt">t8.txt</option>
                    <option value="t9.txt">t9.txt</option>
                    <option value="t10.txt">t10.txt</option>
                    </select>
                    </td>
                </tr>
                )";
            html_form_middle.append(input);
            do_write(html_form_middle,false);
        }
        string html_form_end = R"(            
                    <tr>
                        <td colspan="3"></td>
                        <td>
                        <button type="submit" class="btn btn-info btn-block">Run</button>
                        </td>
                    </tr>
                    </tbody>
                </table>
                </form>
            </body>
            </html>
        )";
        do_write(html_form_end,true);
        
    }

    void load_console(){
        //cout << "socket = " << socket_->native_handle() << endl;
        //do_write("HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n<html><head><title>Bad Request</title></head><body><h1>400 Bad Request</h1></body></html>",false);
        do_write("HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n",false);
        parse(request_.query,parameter_list);
        //debug parsing
        /*
        for(int i = 0; i < parameter_list.size(); i++){
            cout << "h" << i << " = " << parameter_list[i].hostname << endl;
            cout << "p" << i << " = " << parameter_list[i].port << endl;
            cout << "f" << i << " = " << parameter_list[i].filename << endl;
        }*/
        generate_initial_html();
        //cout << "socket after generate html = " << socket_->native_handle() << endl; 
        for(int i = 0; i < parameter_list.size(); i++){
            cerr << "create connection " << i << endl;
            make_shared<np_connection>(parameter_list[i].hostname, parameter_list[i].port, i, socket_)->start(parameter_list[i].filename);
        }
    }

    void generate_initial_html(){
        cout << "generate_initial_html" << endl;
        //cout << "socket = " << socket_->native_handle() << endl;
    string html = R"(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <title>NP Project 3 Sample Console</title>
    <link
      rel="stylesheet"
      href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
      integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
      crossorigin="anonymous"
    />
    <link
      href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
      rel="stylesheet"
    />
    <link
      rel="icon"
      type="image/png"
      href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"
    />
    <style>
      * {
        font-family: 'Source Code Pro', monospace;
        font-size: 1rem !important;
      }
      body {
        background-color: #212529;
      }
      pre {
        color: #cccccc;
      }
      b {
        color: #01b468;
      }
    </style>
  </head>
  <body>
    <table class="table table-dark table-bordered">
      <thead>
        <tr>)";

    for(int i = 0 ; i < parameter_list.size(); i++){
        html += ("<th scope=\"col\">" + parameter_list[i].hostname + ":" + parameter_list[i].port + "</th>");
    }

    html += R"(
        </tr>
      </thead>
      <tbody>
        <tr>
        )";

    for(int i = 0; i < parameter_list.size(); i++){
        html += ("<td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>");
    }

    html += R"(
        </tr>
      </tbody>
    </table>
  </body>
</html>
    )";
    auto self(shared_from_this());
    socket_->async_write_some(buffer(html),
        [this,self](boost::system::error_code ec, std::size_t) {
            if (!ec)
            { 
            }
        });
}

    char buffer_[buf_size];
    string reply;
    shared_ptr<tcp::socket> socket_;
    request_parser request_parser_;
    request request_;
    io_service& io_service_;
    vector<parameter> parameter_list;
};

class server{
public:
    server(io_service &ioservice, short port)
    : io_service_(ioservice), tcp_acceptor(ioservice, tcp::endpoint(tcp::v4(), port))
    {
        tcp_acceptor.listen();
        do_accept();
    }
private:
    void do_accept(){
        auto tcp_socket = make_shared<tcp::socket>(ioservice);
        tcp_acceptor.async_accept(*tcp_socket,
            [this, tcp_socket](boost::system::error_code ec) {
                if (!ec)
                {
                    boost::thread callcgi = boost::thread(mem_fn(&connection::start), std::make_shared<connection>(io_service_,move(tcp_socket)));
                    callcgi.detach();
                }

                do_accept();
            });
    }
    tcp::acceptor tcp_acceptor;
    io_service& io_service_;
};

int main(int argc, char *argv[])
{
    server s(ioservice,atoi(argv[1]));
    ioservice.run();
}