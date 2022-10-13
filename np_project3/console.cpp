#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <memory>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::asio::detail::socket_ops;

io_service ioservice;

typedef struct parameter{
    string hostname;
    string port;
    string filename;
} parameter;
vector<parameter> parameter_list;

void parse(string query_string){
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

void generate_initial_html(){
    cout << R"(
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
        <tr>)" << endl;

    for(int i = 0 ; i < parameter_list.size(); i++){
        cout << "<th scope=\"col\">" << parameter_list[i].hostname << ":" << parameter_list[i].port << "</th>" << endl;
    }

    cout << R"(
        </tr>
      </thead>
      <tbody>
        <tr>
        )" << endl;

    for(int i = 0; i < parameter_list.size(); i++){
        cout << "<td><pre id=\"s" << i << "\" class=\"mb-0\"></pre></td>" << endl;
    }

    cout << R"(
        </tr>
      </tbody>
    </table>
  </body>
</html>
    )" << endl;
}

void output_shell(unsigned int id, string data){
    boost::algorithm::replace_all(data, "&", "&amp;");
    boost::algorithm::replace_all(data, "\n", "&NewLine;");
    boost::algorithm::replace_all(data, "\r", "");
    boost::algorithm::replace_all(data, "\'", "\\\'"); 
    boost::algorithm::replace_all(data, "\"", "\\\"");
    boost::algorithm::replace_all(data, "<", "&#60;");
    boost::algorithm::replace_all(data, ">", "&#62;");   
    cout << "<script>document.getElementById('s" << id << "').innerHTML += '" << data << "';</script>" << flush;
}

void output_command(unsigned int id, string data){
    boost::algorithm::replace_all(data, "&", "&amp;");
    boost::algorithm::replace_all(data, "\n", "&NewLine;");
    boost::algorithm::replace_all(data, "\r", "");
    boost::algorithm::replace_all(data, "\'", "\\\'");
    boost::algorithm::replace_all(data, "<", "&#60;");
    boost::algorithm::replace_all(data, ">", "&#62;");   
    cout << "<script>document.getElementById('s" << id << "').innerHTML += '<b>" << data << "</b>';</script>" << flush;
}

class np_connection
    : public std::enable_shared_from_this<np_connection>
{
public:
    np_connection(string hostname, string port, unsigned int id)
    : resolver_(ioservice),query_(hostname, port),socket_(ioservice),connection_id(id)
    {  
    }

    void start(string filename){
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
                    output_shell(connection_id, read_data);
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
                        output_command(connection_id, cmd);
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
    tcp::resolver resolver_;
    tcp::resolver::query query_;
    unsigned int connection_id;
    ifstream fin;
};

int main(){
    cout << "Content-type: text/html\r\n\r\n";
    string query_string;
    if (getenv("QUERY_STRING") != NULL)
    {
        query_string = string(getenv("QUERY_STRING"));
    }
    parse(query_string);
    //debug parse
    /*
    for(int i = 0; i < parameter_list.size(); i++){
        cout << "h" << i << " = " << parameter_list[i].hostname << endl;
        cout << "p" << i << " = " << parameter_list[i].port << endl;
        cout << "f" << i << " = " << parameter_list[i].filename << endl;
    }
    */
    //print initial html
    generate_initial_html();
    
    for(int i = 0; i < parameter_list.size(); i++){
        cerr << "create connection " << i << endl;
        make_shared<np_connection>(parameter_list[i].hostname, parameter_list[i].port, i)->start(parameter_list[i].filename);
    }
    ioservice.run();
}
