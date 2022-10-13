#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <algorithm>

#include <cstdarg>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <map>

using namespace std;
//maximum server request queue length
#define QLEN 0
#define BUFSIZE 4096

u_short portbase = 0;
int msock;

int errexit(char *format, ...)
{
    va_list args;
    char buf[1024];

    va_start(args, format);
    //vfprintf(stderr, format, args);
    vsprintf(buf, format, args);
    va_end(args);
    fprintf(stderr, "ERROR: %s\n", buf);
    exit(1);
}

void childHandler(int signo)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
    {
    }
}

int passivesock(const char *service, const char *protocol, int qlen)
{
    struct servent *pse;
    struct protoent *ppe;
    struct sockaddr_in sin;

    int s, type;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;

    if (pse = getservbyname(service, protocol))
    {
        sin.sin_port = htons(ntohs((u_short)pse->s_port) + portbase);
    }
    else if ((sin.sin_port = htons((u_short)atoi(service))) == 0)
        cerr << "can't get service entry" << endl;

    if ((ppe = getprotobyname(protocol)) == 0)
        cerr << "can't get protocol entry" << endl;

    if (strcmp(protocol, "udp") == 0)
        type = SOCK_DGRAM;
    else
        type = SOCK_STREAM;

    s = socket(PF_INET, type, ppe->p_proto);
    if (s < 0)
        cerr << "can't create socket" << endl;

    /* Bind the socket */
    int enable = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        cerr << "setsockopt(SO_REUSEADDR) failed" << endl;
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
        cerr << "can't bind to port" << endl;
    if (type == SOCK_STREAM && listen(s, qlen) < 0)
        cerr << "can't listen on port" << endl;
    return s;
}

int passiveTCP(const char *service, int qlen)
{
    return passivesock(service, "tcp", qlen);
}

ssize_t readLine(int fd, void *buffer, size_t n)
{
    ssize_t numRead; /* # of bytes fetched by last read() */
    size_t totRead;  /* Total bytes read so far */
    char *buf;
    char ch;

    if (n <= 0 || buffer == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    buf = (char *)buffer; /* No pointer arithmetic on "void *" */

    totRead = 0;
    for (;;)
    {
        numRead = read(fd, &ch, 1);
        if (numRead == -1)
        {
            if (errno == EINTR) /* Interrupted --> restart read() */
                continue;
            else
                return -1; /* Some other error */
        }
        else if (numRead == 0)
        {                     /* EOF */
            if (totRead == 0) /* No bytes read; return 0 */
                return 0;
            else /* Some bytes read; add '\0' */
                break;
        }
        else
        { /* 'numRead' must be 1 if we get here */
            if (totRead < n - 1)
            { /* Discard > (n - 1) bytes */
                totRead++;
                *buf++ = ch;
            }

            if (ch == '\n')
                break;
        }
    }

    *buf = '\0';
    return totRead;
}

int echo(int fd)
{
    char buf[BUFSIZE];
    int cc;

    cc = read(fd, buf, sizeof(buf));
    if (cc < 0)
        cerr << "echo read error" << endl;
    if (cc && write(fd, buf, cc) < 0)
        cerr << "echo write: error" << endl;
    return cc;
}

//struct to deal with number pipe
typedef struct numberpipe
{
    int fd[2];
    int remainline;
    bool error;
    pid_t pid;
} numberpipe;
//struct of a single pipe
typedef struct pipefd
{
    int fd[2];
    pid_t pid;
} pipefd;

//parse cmd function
vector<vector<string>> parse(string cmd, vector<numberpipe> &nplist)
{
    vector<vector<string>> cmd_list;
    vector<string> parsed_cmd;
    istringstream iss(cmd);
    string s;
    while (iss >> s)
    {
        if (s == "yell")
        {
            parsed_cmd.push_back(s);
            iss.get();
            getline(iss, s);
            parsed_cmd.push_back(s);
            cmd_list.push_back(parsed_cmd);
            parsed_cmd.clear();
            break;
        }
        else if (s == "tell"){
            parsed_cmd.push_back(s);
            iss >> s;
            parsed_cmd.push_back(s);
            iss.get();
            getline(iss, s);
            parsed_cmd.push_back(s);
            cmd_list.push_back(parsed_cmd);
            parsed_cmd.clear();
            break;
        }
        else if (s[0] != '|' && s[0] != '!')
        {
            parsed_cmd.push_back(s);            
        }
        else if (s[0] == '|' && s.size() >= 2)
        {
            cmd_list.push_back(parsed_cmd);
            parsed_cmd.clear();

            numberpipe np;
            np.remainline = stoi(s.substr(1, s.size() - 1));
            np.error = false;
            nplist.push_back(np);
        }
        else if (s[0] == '!' && s.size() >= 2)
        {
            cmd_list.push_back(parsed_cmd);
            parsed_cmd.clear();

            numberpipe np;
            np.remainline = stoi(s.substr(1, s.size() - 1));
            np.error = true;
            nplist.push_back(np);
        }
        else
        {
            cmd_list.push_back(parsed_cmd);
            parsed_cmd.clear();
        }
    }
    if (parsed_cmd.size() != 0)
    {
        cmd_list.push_back(parsed_cmd);
    }
    return cmd_list;
}
//convert vector<string> to char**
void convert(const vector<string> &v, char **argv)
{
    for (unsigned int i = 0; i < v.size(); ++i)
    {
        argv[i] = new char[v[i].size() + 1];
        strcpy(argv[i], v[i].c_str());
    }
    argv[v.size()] = NULL;
}

string convertToString(char *a, int size)
{
    int i;
    string s = "";
    for (i = 0; i < size; i++)
    {
        if (a[i] == '\0')
        {
            s = s + a[i];
            break;
        }
        s = s + a[i];
    }
    return s;
}

typedef struct user{
    //number pipe list
    vector<numberpipe> nplist;
    //the pipe list
    vector<pipefd> pipefd_list;
    //user name
    string name;
    //user environment variable
    map<string, string> env;
    //ip and port
    string ip_port;
    //user id
    int id;
} user;

map<int, user> usermap;
map<int, int> idmap;

typedef struct user_pipe{
    int fd[2];
    bool pipe_exist;
} user_pipe;

vector< vector< user_pipe>> user_pipe_list;

int npshell(int fd, vector<numberpipe> &nplist, vector<pipefd> &pipefd_list, user &current_user)
{
    //check environment variable
    /*
    for (char **env = envp; *env != 0; env++){
        char *thisEnv = *env;
        cout << thisEnv << endl;  
    }
    */
    char buffer[15001];
    map<int, user>::iterator sock_it;
    map<int, int>::iterator id_it;
    //read command
    string cmd;
    int readbyte;
    readbyte = readLine(fd, buffer, sizeof(buffer));
    if (readbyte == 0)
    {
        return 0;
    }
    //cmd = convertToString(buffer, readbyte);
    cmd.assign(buffer, readbyte);

    //save nplist size to check whether nplist increased
    int npnum = nplist.size();
    //parse the command
    vector<vector<string>> cmd_list = parse(cmd, nplist);
    //debug parsing
    /*
    for(int i = 0; i < cmd_list.size(); i++){
        cout << "i = " << i << " cmd = ";
        for(int j = 0; j < cmd_list[i].size(); j++){
            cout << cmd_list[i][j] << " ";
        }
        cout << endl;
    }
    */

    pid_t pid;
    //whether this command contains number pipe
    bool has_np = false;
    bool has_error = false;
    if (nplist.size() != npnum)
    {
        //cout << "new number pipe!" << endl;
        has_np = true;
        has_error = nplist[nplist.size() - 1].error;
    }

    //count down all the number pipe except the new one
    if (cmd_list.size() != 0)
    {
        if (has_np)
        {
            if (nplist.size() >= 2)
            {
                for (int i = 0; i < nplist.size() - 1; i++)
                {
                    nplist[i].remainline--;
                }
            }
        }
        else
        {
            for (int i = 0; i < nplist.size(); i++)
            {
                nplist[i].remainline--;
            }
        }
    }

    //debug nplist
    /*
    for(int i = 0; i < nplist.size(); i++){
        if(!nplist[i].error)
            cout << "nplist " << i << " = |" << nplist[i].remainline << endl;
        else
            cout << "nplist " << i << " = !" << nplist[i].remainline << endl;
    }
    */
    //whether line of a number pipe counts down to 0
    bool np_in = false;
    int np_in_index = -1;
    vector<int> np_in_list;
    if (nplist.size() != 0)
    {
        for (int i = 0; i < nplist.size(); i++)
        {
            if (nplist[i].remainline == 0)
            {
                np_in = true;
                //only record first number pipe because output need to merge to first number pipe
                if (np_in_index == -1)
                    np_in_index = i;
                np_in_list.push_back(i);
            }
        }
    }

    //if command contains number pipe, check whether line is same as element in nplist
    bool np_same = false;
    int np_same_index = -1;
    if (nplist.size() >= 2)
    {
        for (int i = 0; i < nplist.size() - 1; i++)
        {
            if (nplist[i].remainline == nplist[nplist.size() - 1].remainline)
            {
                np_same = true;
                np_same_index = i;
                break;
            }
        }
    }
    //check user pipe, set the flag
    bool has_user_pipe = false;
    bool user_pipe_in = false;
    int sender = -1;
    int receiver = -1;
    if(cmd_list.size() == 1){
        if(cmd_list[0].size() >= 2){
            if (cmd_list[0][cmd_list[0].size() - 2] == ">"){
                if (cmd_list[0][cmd_list[0].size() - 3][0] == '<' && cmd_list[0][cmd_list[0].size() - 3].size() >= 2)
                {
                    user_pipe_in = true;
                    string user = cmd_list[0][cmd_list[0].size() - 3].substr(1, string::npos);
                    sender = stoi(user);
                    cout << "sender: " << sender << endl;
                }
            }
            else if (cmd_list[0][cmd_list[0].size() - 1][0] == '>' && cmd_list[0][cmd_list[0].size() - 1].size() >= 2){
                has_user_pipe = true;
                string user = cmd_list[0][cmd_list[0].size() - 1].substr(1, string::npos);
                receiver = stoi(user);
                cout << "receiver: " << receiver << endl;
                if (cmd_list[0][cmd_list[0].size() - 2][0] == '<' && cmd_list[0][cmd_list[0].size() - 2].size() >= 2){
                    user_pipe_in = true;
                    string user = cmd_list[0][cmd_list[0].size() - 2].substr(1, string::npos);
                    sender = stoi(user);
                    cout << "sender: " << sender << endl;
                }
            }
            else if (cmd_list[0][cmd_list[0].size() - 1][0] == '<' && cmd_list[0][cmd_list[0].size() - 1].size() >= 2){
                user_pipe_in = true;
                string user = cmd_list[0][cmd_list[0].size() - 1].substr(1, string::npos);
                sender = stoi(user);
                cout << "sender: " << sender << endl;
                if (cmd_list[0][cmd_list[0].size() - 2][0] == '>' && cmd_list[0][cmd_list[0].size() - 2].size() >= 2){
                    has_user_pipe = true;
                    string user = cmd_list[0][cmd_list[0].size() - 2].substr(1, string::npos);
                    receiver = stoi(user);
                    cout << "receiver: " << receiver << endl;
                }
            }
        }
    }
    else if(cmd_list.size() >= 2){
        if(cmd_list[0].size() >= 2){
            if (cmd_list[0][cmd_list[0].size() - 1][0] == '<' && cmd_list[0][cmd_list[0].size() - 1].size() >= 2){
                user_pipe_in = true;
                string user = cmd_list[0][cmd_list[0].size() - 1].substr(1, string::npos);
                sender = stoi(user);
                cout << "sender: " << sender << endl;
            }
        }
        if(cmd_list[cmd_list.size() - 1].size() >= 2){
            if (cmd_list[cmd_list.size() - 1][cmd_list[cmd_list.size() - 1].size() - 1][0] == '>' && cmd_list[cmd_list.size() - 1][cmd_list[cmd_list.size() - 1].size() - 1].size() >= 2)
            {
                has_user_pipe = true;
                string user = cmd_list[cmd_list.size() - 1][cmd_list[cmd_list.size() - 1].size() - 1].substr(1, string::npos);
                receiver = stoi(user);
                cout << "receiver: " << receiver << endl;
            }
        }
    }
    //deal with user pipe's success and error message
    bool up_in_error = false;
    bool up_out_error = false;
    // <n message first
    if(user_pipe_in){
        if(idmap.find(sender) == idmap.end()){
            up_in_error = true;
            string error_message = "*** Error: user #";
            error_message.append(to_string(sender));
            error_message.append(" does not exist yet. ***\n");
            write(fd, error_message.c_str(), error_message.size());            
        }
        else if(sender >= 1 && sender <= user_pipe_list.size()){
            if (!user_pipe_list[sender - 1][current_user.id - 1].pipe_exist)
            {
                up_in_error = true;
                string error_message = "*** Error: the pipe #";
                error_message.append(to_string(sender));
                error_message.append("->#");
                error_message.append(to_string(current_user.id));
                error_message.append(" does not exist yet. ***\n");
                write(fd, error_message.c_str(), error_message.size());
            }
            else
            {
                string success_message = "*** ";
                success_message.append(current_user.name);
                success_message.append(" (#");
                success_message.append(to_string(current_user.id));
                success_message.append(") just received from ");
                success_message.append(usermap[idmap[sender]].name);
                success_message.append(" (#");
                success_message.append(to_string(sender));
                success_message.append(") by '");
                for (int i = 0; i < cmd_list.size(); i++)
                {
                    for (int j = 0; j < cmd_list[i].size(); j++)
                    {
                        success_message.append(cmd_list[i][j]);
                        if (i != cmd_list.size() - 1 || j != cmd_list[i].size() - 1)
                        {
                            success_message.append(" ");
                        }
                    }
                    if(i != cmd_list.size() - 1){
                        success_message.append("| ");
                    }
                }
                if(has_np){
                    if(has_error){
                        success_message.append(" !");
                    }
                    else{
                        success_message.append(" |");
                    }
                    success_message.append(to_string(nplist[nplist.size() - 1].remainline));
                }
                success_message.append("' ***\n");

                for (sock_it = usermap.begin(); sock_it != usermap.end(); sock_it++)
                {
                    write(sock_it->first, success_message.c_str(), success_message.size());                   
                }
            }
        }       
    }

    // >n message
    if(has_user_pipe){
        if(idmap.find(receiver) == idmap.end()){
            up_out_error = true;
            string error_message = "*** Error: user #";
            error_message.append(to_string(receiver));
            error_message.append(" does not exist yet. ***\n");
            write(fd, error_message.c_str(), error_message.size());
        }
        else if (receiver >= 1 && receiver <= user_pipe_list.size())
        {
            if (user_pipe_list[current_user.id - 1][receiver - 1].pipe_exist)
            {
                up_out_error = true;
                string error_message = "*** Error: the pipe #";
                error_message.append(to_string(current_user.id));
                error_message.append("->#");
                error_message.append(to_string(receiver));
                error_message.append(" already exists. ***\n");
                write(fd, error_message.c_str(), error_message.size());
            }            
            else
            {
                string success_message = "*** ";
                success_message.append(current_user.name);
                success_message.append(" (#");
                success_message.append(to_string(current_user.id));
                success_message.append(") just piped '");
                for (int i = 0; i < cmd_list.size(); i++)
                {
                    for (int j = 0; j < cmd_list[i].size(); j++)
                    {
                        success_message.append(cmd_list[i][j]);
                        if (i != cmd_list.size() - 1 || j != cmd_list[i].size() - 1)
                        {
                            success_message.append(" ");
                        }
                    }
                    if (i != cmd_list.size() - 1)
                    {
                        success_message.append("| ");
                    }
                }
                if (has_np)
                {
                    if (has_error)
                    {
                        success_message.append(" !");
                    }
                    else
                    {
                        success_message.append(" |");
                    }
                    success_message.append(to_string(nplist[nplist.size() - 1].remainline));
                }
                success_message.append("' to ");
                success_message.append(usermap[idmap[receiver]].name);
                success_message.append(" (#");
                success_message.append(to_string(receiver));
                success_message.append(") ***\n");

                for (sock_it = usermap.begin(); sock_it != usermap.end(); sock_it++)
                {
                    write(sock_it->first, success_message.c_str(), success_message.size());                   
                }

            }
        }
    }

    //remove user pipe from command
    if(cmd_list.size() == 1){
        if(has_user_pipe){
            cmd_list[0].pop_back();           
        }
        if(user_pipe_in){
            if (cmd_list[0].size() >= 3 && cmd_list[0][cmd_list[0].size() - 3][0] == '<')
            {
                cmd_list[0].erase(cmd_list[0].begin() + cmd_list[0].size() - 3);
            }
            else{
                cmd_list[0].pop_back();
            }           
        }
    }
    else if (cmd_list.size() >= 2){
        if(has_user_pipe){
            cmd_list[cmd_list.size() - 1].pop_back();
        }
        if(user_pipe_in){
            cmd_list[0].pop_back();
        }
    }

    //deal with built-in commands exit,printenv and setenv
    if (cmd_list.size() == 1 && cmd_list[0][0] == "exit")
    {
        return 0;
    }
    else if (cmd_list.size() == 1 && cmd_list[0][0] == "who"){
        string column_name = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
        write(fd, column_name.c_str(), column_name.size());
        for(id_it = idmap.begin(); id_it != idmap.end(); id_it++){
            user u = usermap[id_it->second];
            string user_info;
            user_info.append(to_string(u.id));
            user_info.append("\t");
            user_info.append(u.name);
            user_info.append("\t");
            user_info.append(u.ip_port);           
            if(id_it->second == fd){
                user_info.append("\t<-me\n");
            }
            else{
                user_info.append("\n");
            }
            write(fd, user_info.c_str(), user_info.size());
        }
    }
    else if (cmd_list.size() == 1 && cmd_list[0].size() == 2 && cmd_list[0][0] == "printenv")
    {
        const char *c = cmd_list[0][1].c_str();
        char *value = getenv(c);
        if (value != NULL)
        {
            write(fd, value, strlen(value));
            write(fd, "\n", 1);
        }
    }
    else if (cmd_list.size() == 1 && cmd_list[0].size() == 3 && cmd_list[0][0] == "setenv")
    {       
        const char *c1 = cmd_list[0][1].c_str();
        const char *c2 = cmd_list[0][2].c_str();
        setenv(c1, c2, 1);
        //save environment variable change in user's env map       
        map<string, string>::iterator env_it;
        string env_variable;
        string value;
        env_variable.append(c1);
        value.append(c2);
        current_user.env[env_variable] = value;
    }
    else if (cmd_list.size() == 1 && cmd_list[0].size() == 2 && cmd_list[0][0] == "yell"){
        string yell_message = "*** ";
        yell_message.append(current_user.name);
        yell_message.append(" yelled ***: ");
        yell_message.append(cmd_list[0][1]);
        yell_message.append("\n");
        for(sock_it = usermap.begin(); sock_it != usermap.end(); sock_it++){
            write(sock_it->first, yell_message.c_str(), yell_message.size());            
        }
    }
    else if (cmd_list.size() == 1 && cmd_list[0].size() == 2 && cmd_list[0][0] == "name"){    
        bool name_exist = false;    
        for(sock_it = usermap.begin(); sock_it != usermap.end(); sock_it++){
            if(sock_it->first != fd && sock_it->second.name == cmd_list[0][1]){
                name_exist = true;
                break;
            }
        }
        if(name_exist){
            string exist_message = "*** User '";
            exist_message.append(cmd_list[0][1]);
            exist_message.append("' already exists. ***\n");
            write(fd, exist_message.c_str(), exist_message.size());
        }
        else{
            current_user.name = cmd_list[0][1];
            string change_message = "*** User from ";
            change_message.append(current_user.ip_port);
            change_message.append(" is named '");
            change_message.append(current_user.name);
            change_message.append("'. ***\n");
            for (sock_it = usermap.begin(); sock_it != usermap.end(); sock_it++)
            {
                write(sock_it->first, change_message.c_str(), change_message.size());
            }
        }
    }
    else if (cmd_list.size() == 1 && cmd_list[0].size() == 3 && cmd_list[0][0] == "tell"){
        int user_id = stoi(cmd_list[0][1]);
        id_it = idmap.find(user_id);
        if(id_it == idmap.end()){
            string error_message = "*** Error: user #";
            error_message.append(cmd_list[0][1]);
            error_message.append(" does not exist yet. ***\n");
            write(fd, error_message.c_str(), error_message.size());
        }
        else{
            string message = "*** ";
            message.append(current_user.name);
            message.append(" told you ***: ");
            message.append(cmd_list[0][2]);
            message.append("\n");
            write(id_it->second, message.c_str(), message.size());
        }
    }
    //execute one command
    else if (cmd_list.size() == 1)
    {
        //one command without >
        if (cmd_list[0].size() <= 2 || (cmd_list[0].size() > 2 && cmd_list[0][cmd_list[0].size() - 2] != ">"))
        {
            if (!has_np && !np_in && !has_user_pipe && !user_pipe_in)
            {
                while ((pid = fork()) < 0)
                {
                    waitpid(-1, NULL, 0);
                }
                if (pid == 0)
                {
                    dup2(fd, 1);
                    dup2(fd, 2);
                    for (int i = 0; i < nplist.size(); i++)
                    {
                        close(nplist[i].fd[0]);
                        close(nplist[i].fd[1]);
                    }
                    const char *p = cmd_list[0][0].c_str();
                    char **argv = new char *[cmd_list[0].size() + 1];
                    convert(cmd_list[0], argv);
                    if (execvp(p, argv) == -1)
                    {
                        cerr << "Unknown command: [" << p << "]." << endl;
                        //perror("execvp");
                    }
                    for(int i = 0; i < cmd_list[0].size(); i++){
                        delete [] argv[i];
                    }
                    delete [] argv;
                    exit(0);
                }
                else
                {
                    waitpid(pid, NULL, 0);
                }
            }
            else
            {
                if (has_np && !np_same)
                {
                    pipefd pfd;
                    pipe(pfd.fd);
                    nplist[nplist.size() - 1].fd[0] = pfd.fd[0];
                    nplist[nplist.size() - 1].fd[1] = pfd.fd[1];
                }
                else if (has_np && np_same)
                {
                    nplist[nplist.size() - 1].fd[0] = nplist[np_same_index].fd[0];
                    nplist[nplist.size() - 1].fd[1] = nplist[np_same_index].fd[1];
                }
                //create pipe and /dev/null for user pipe
                int nullfd_in;
                int nullfd_out;
                if(has_user_pipe){
                    if(!up_out_error){
                        pipe(user_pipe_list[current_user.id - 1][receiver - 1].fd);
                        user_pipe_list[current_user.id - 1][receiver - 1].pipe_exist = true;
                    }
                    else{
                        nullfd_out = open("/dev/null", O_WRONLY);
                    }
                }

                if(user_pipe_in){
                    if(!up_in_error){
                        user_pipe_list[sender - 1][current_user.id - 1].pipe_exist = false;
                    }
                    else{
                        nullfd_in = open("/dev/null", O_RDONLY);
                    }
                }

                while ((pid = fork()) < 0)
                {
                    waitpid(-1, NULL, 0);
                }
                if (pid == 0)
                {
                    if (has_np && !np_same)
                    {
                        if (dup2(nplist[nplist.size() - 1].fd[1], 1) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }
                        if (has_error)
                        {
                            if (dup2(nplist[nplist.size() - 1].fd[1], 2) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                        }
                    }
                    else if (has_np && np_same)
                    {
                        if (dup2(nplist[np_same_index].fd[1], 1) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }
                        if (has_error)
                        {
                            if (dup2(nplist[np_same_index].fd[1], 2) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                        }
                    }
                    if (np_in)
                    {
                        if (dup2(nplist[np_in_index].fd[0], 0) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }
                    }

                    if(has_user_pipe){
                        if(!up_out_error){
                            dup2(user_pipe_list[current_user.id - 1][receiver - 1].fd[1], 1);
                            close(user_pipe_list[current_user.id - 1][receiver - 1].fd[0]);
                        }
                        else{
                            dup2(nullfd_out, 1);
                        }
                    }

                    if(user_pipe_in){
                        if(!up_in_error){
                            dup2(user_pipe_list[sender - 1][current_user.id - 1].fd[0], 0);
                        }
                        else{
                            dup2(nullfd_in, 0);
                        }
                    }

                    for (int i = 0; i < nplist.size(); i++)
                    {
                        close(nplist[i].fd[0]);
                        close(nplist[i].fd[1]);
                    }               

                    if (!has_np && !has_user_pipe)
                    {
                        dup2(fd, 1);
                        dup2(fd, 2);
                    }
                    if ((has_np && !has_error) || has_user_pipe){
                        dup2(fd, 2);
                    }

                    const char *p = cmd_list[0][0].c_str();
                    char **argv = new char *[cmd_list[0].size() + 1];
                    convert(cmd_list[0], argv);
                    if (execvp(p, argv) == -1)
                    {
                        cerr << "Unknown command: [" << p << "]." << endl;
                        //perror("execvp");
                    }
                    for (int i = 0; i < cmd_list[0].size(); i++)
                    {
                        delete [] argv[i];
                    }
                    delete [] argv;
                    exit(0);
                }
                else
                {
                    if (has_np)
                    {
                        nplist[nplist.size() - 1].pid = pid;
                    }
                    if (np_in)
                    {
                        close(nplist[np_in_index].fd[0]);
                        close(nplist[np_in_index].fd[1]);
                        if (!has_np && !has_user_pipe)
                            waitpid(pid, NULL, 0);
                        if (np_in_list.size() == 1)
                        {
                            nplist.erase(nplist.begin() + np_in_index);
                        }
                        else
                        {
                            for (int i = np_in_list.size() - 1; i >= 0; i--)
                            {
                                nplist.erase(nplist.begin() + np_in_list[i]);
                            }
                        }
                    }
                    if(has_user_pipe){
                        if(!up_out_error){
                            close(user_pipe_list[current_user.id - 1][receiver - 1].fd[1]);
                        }
                        else{
                            close(nullfd_out);
                        }                       
                    }
                    if(user_pipe_in){
                        if(!up_in_error){
                            close(user_pipe_list[sender - 1][current_user.id - 1].fd[0]);
                        }
                        else{
                            close(nullfd_in);
                        }
                        if (!has_np && !has_user_pipe)
                            waitpid(pid, NULL, 0);
                    }
                }
            }
        }
        //one command include >
        else
        {
            const char *filename = cmd_list[0][cmd_list[0].size() - 1].c_str();
            int out = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            int nullfd_in;
            if (user_pipe_in)
            {
                if (!up_in_error)
                {
                    user_pipe_list[sender - 1][current_user.id - 1].pipe_exist = false;
                }
                else
                {
                    nullfd_in = open("/dev/null", O_RDONLY);
                }
            }
            while ((pid = fork()) < 0)
            {
                waitpid(-1, NULL, 0);
            }
            if (pid == 0)
            {
                if (np_in)
                {
                    if (dup2(nplist[np_in_index].fd[0], 0) < 0)
                    {
                        perror("dup2 error");
                        exit(-1);
                    }
                }
                if (user_pipe_in)
                {
                    if (!up_in_error)
                    {
                        dup2(user_pipe_list[sender - 1][current_user.id - 1].fd[0], 0);
                    }
                    else
                    {
                        dup2(nullfd_in, 0);
                    }
                }
                //replace stdout to out
                if (dup2(out, 1) < 0)
                {
                    perror("dup2 error");
                    exit(-1);
                }
                for (int i = 0; i < nplist.size(); i++)
                {
                    close(nplist[i].fd[0]);
                    close(nplist[i].fd[1]);
                }

                dup2(fd, 2);

                const char *p = cmd_list[0][0].c_str();
                char **argv = new char *[cmd_list[0].size() - 2 + 1];
                vector<string> cmd;
                cmd.resize(cmd_list[0].size() - 2);
                for (int i = 0; i < cmd_list[0].size() - 2; i++)
                {
                    cmd[i] = cmd_list[0][i];
                }
                convert(cmd, argv);
                if (execvp(p, argv) == -1)
                {
                    cerr << "Unknown command: [" << p << "]." << endl;
                    //perror("execvp");
                }
                for (int i = 0; i < cmd.size(); i++)
                {
                    delete [] argv[i];
                }
                delete [] argv;
                exit(0);
            }
            else
            {
                if (!np_in && !user_pipe_in)
                {
                    close(out);
                    waitpid(pid, NULL, 0);
                }
                else if(np_in)
                {
                    close(nplist[np_in_index].fd[0]);
                    close(nplist[np_in_index].fd[1]);
                                      
                    close(out);
                    waitpid(pid, NULL, 0);
                    if (np_in_list.size() == 1)
                    {
                        nplist.erase(nplist.begin() + np_in_index);
                    }
                    else
                    {
                        for (int i = np_in_list.size() - 1; i >= 0; i--)
                        {
                            nplist.erase(nplist.begin() + np_in_list[i]);
                        }
                    }                   
                }
                else if(user_pipe_in){
                    if (!up_in_error)
                    {
                        close(user_pipe_list[sender - 1][current_user.id - 1].fd[0]);
                    }
                    else
                    {
                        close(nullfd_in);
                    }
                    close(out);
                    waitpid(pid, NULL, 0);
                }
            }
        }
    }
    //execute command with pipe
    else if (cmd_list.size() >= 2)
    {
        //cout << "open pipe!" << endl;
        int cmdnum = cmd_list.size();
        vector<pid_t> pidlist;
        //cout << "current pipe index: " << currentpipeindex << endl;
        if (!has_np && !np_in && !has_user_pipe && !user_pipe_in)
        {
            for (int i = 0; i < cmdnum; i++)
            {
                if (i != cmdnum - 1)
                {
                    pipefd pfd;
                    if (pipe(pfd.fd) < 0)
                    {
                        perror("pipe error");
                    }
                    pipefd_list.push_back(pfd);
                    while ((pid = fork()) < 0)
                    {
                        waitpid(-1, NULL, 0);
                    }
                    if (pid == 0)
                    {
                        if (i == 0)
                        {
                            if (dup2(pipefd_list[i].fd[1], 1) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }                           
                        }
                        else
                        {
                            if (dup2(pipefd_list[i - 1].fd[0], 0) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                            if (dup2(pipefd_list[i].fd[1], 1) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                        }

                        close(pipefd_list[i].fd[0]);

                        for (int i = 0; i < nplist.size(); i++)
                        {
                            close(nplist[i].fd[0]);
                            close(nplist[i].fd[1]);
                        }

                        dup2(fd, 2);

                        const char *p = cmd_list[i][0].c_str();
                        char **argv = new char *[cmd_list[i].size() + 1];
                        convert(cmd_list[i], argv);
                        if (execvp(p, argv) == -1)
                        {
                            cerr << "Unknown command: [" << p << "]." << endl;
                            //perror("execvp");
                        }
                        for (int i = 0; i < cmd_list[i].size(); i++)
                        {
                            delete [] argv[i];
                        }
                        delete [] argv;
                        exit(0);
                    }
                    else
                    {
                        if (i == 0)
                        {
                            close(pipefd_list[0].fd[1]);
                        }
                        else
                        {
                            close(pipefd_list[i - 1].fd[0]);
                            close(pipefd_list[i].fd[1]);
                        }
                        pidlist.push_back(pid);
                    }
                }
                //last command not include >
                else if (cmd_list[cmdnum - 1].size() <= 2 || (cmd_list[cmdnum - 1].size() > 2 && cmd_list[cmdnum - 1][cmd_list[cmdnum - 1].size() - 2] != ">"))
                {
                    while ((pid = fork()) < 0)
                    {
                        waitpid(-1, NULL, 0);
                    }
                    if (pid == 0)
                    {
                        if (dup2(pipefd_list[i - 1].fd[0], 0) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }

                        for (int i = 0; i < nplist.size(); i++)
                        {
                            close(nplist[i].fd[0]);
                            close(nplist[i].fd[1]);
                        }

                        dup2(fd, 1);
                        dup2(fd, 2);

                        const char *p = cmd_list[i][0].c_str();
                        char **argv = new char *[cmd_list[i].size() + 1];
                        convert(cmd_list[i], argv);
                        if (execvp(p, argv) == -1)
                        {
                            cerr << "Unknown command: [" << p << "]." << endl;
                            //perror("execvp");
                        }
                        for (int i = 0; i < cmd_list[i].size(); i++)
                        {
                            delete [] argv[i];
                        }
                        delete [] argv;
                        exit(0);
                    }
                    else
                    {
                        close(pipefd_list[i - 1].fd[0]);
                        pidlist.push_back(pid);
                    }
                }
                //last command include >
                else
                {
                    const char *filename = cmd_list[i][cmd_list[i].size() - 1].c_str();
                    int out = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    while ((pid = fork()) < 0)
                    {
                        waitpid(-1, NULL, 0);
                    }
                    if (pid == 0)
                    {
                        if (dup2(pipefd_list[i - 1].fd[0], 0) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }
                        //replace stdout to out
                        if (dup2(out, 1) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }

                        for (int i = 0; i < nplist.size(); i++)
                        {
                            close(nplist[i].fd[0]);
                            close(nplist[i].fd[1]);
                        }

                        dup2(fd, 2);
                        const char *p = cmd_list[i][0].c_str();
                        char **argv = new char *[cmd_list[i].size() - 2 + 1];
                        vector<string> cmd;
                        cmd.resize(cmd_list[i].size() - 2);
                        for (int j = 0; j < cmd_list[i].size() - 2; j++)
                        {
                            cmd[j] = cmd_list[i][j];
                        }
                        convert(cmd, argv);
                        if (execvp(p, argv) == -1)
                        {
                            cerr << "Unknown command: [" << p << "]." << endl;
                            //perror("execvp");
                        }
                        for (int i = 0; i < cmd.size(); i++)
                        {
                            delete [] argv[i];
                        }
                        delete [] argv;
                        exit(0);
                    }
                    else
                    {
                        close(pipefd_list[i - 1].fd[0]);
                        close(out);
                        pidlist.push_back(pid);
                    }
                }
            }
            for (int i = 0; i < cmdnum; i++)
            {
                waitpid(pidlist[i], NULL, 0);
            }
            pidlist.clear();
            //remove pipe for pipe_list
            pipefd_list.clear();
        }
        //pipe contains number pipe and user pipe
        else
        {
            //cout << "create pipe with number pipe or number pipe in" << endl;
            if (has_np && !np_same)
            {
                pipefd pfd;
                pipe(pfd.fd);
                nplist[nplist.size() - 1].fd[0] = pfd.fd[0];
                nplist[nplist.size() - 1].fd[1] = pfd.fd[1];
            }
            else if (has_np && np_same)
            {
                nplist[nplist.size() - 1].fd[0] = nplist[np_same_index].fd[0];
                nplist[nplist.size() - 1].fd[1] = nplist[np_same_index].fd[1];
            }

            int nullfd_in;
            int nullfd_out;
            if (has_user_pipe)
            {
                if (!up_out_error)
                {
                    pipe(user_pipe_list[current_user.id - 1][receiver - 1].fd);
                    user_pipe_list[current_user.id - 1][receiver - 1].pipe_exist = true;
                }
                else
                {
                    nullfd_out = open("/dev/null", O_WRONLY);
                }
            }

            if (user_pipe_in)
            {
                if (!up_in_error)
                {
                    user_pipe_list[sender - 1][current_user.id - 1].pipe_exist = false;
                }
                else
                {
                    nullfd_in = open("/dev/null", O_RDONLY);
                }
            }

            for (int i = 0; i < cmdnum; i++)
            {
                if (i != cmdnum - 1)
                {
                    pipefd pfd;
                    if (pipe(pfd.fd) < 0)
                    {
                        perror("pipe error");
                    }
                    pipefd_list.push_back(pfd);
                    while ((pid = fork()) < 0)
                    {
                        waitpid(-1, NULL, 0);
                    }
                    if (pid == 0)
                    {
                        if (i == 0)
                        {
                            if (dup2(pipefd_list[i].fd[1], 1) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                            if (np_in)
                            {
                                if (dup2(nplist[np_in_index].fd[0], 0) < 0)
                                {
                                    perror("dup2 error");
                                    exit(-1);
                                }
                            }
                            if (user_pipe_in)
                            {
                                if (!up_in_error)
                                {
                                    dup2(user_pipe_list[sender - 1][current_user.id - 1].fd[0], 0);
                                }
                                else
                                {
                                    dup2(nullfd_in, 0);
                                }
                            }
                        }
                        else
                        {
                            if (dup2(pipefd_list[i - 1].fd[0], 0) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                            if (dup2(pipefd_list[i].fd[1], 1) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                        }

                        close(pipefd_list[i].fd[0]);

                        for (int i = 0; i < nplist.size(); i++)
                        {
                            close(nplist[i].fd[0]);
                            close(nplist[i].fd[1]);
                        }

                        dup2(fd, 2);

                        const char *p = cmd_list[i][0].c_str();
                        char **argv = new char *[cmd_list[i].size() + 1];
                        convert(cmd_list[i], argv);
                        if (execvp(p, argv) == -1)
                        {
                            cerr << "Unknown command: [" << p << "]." << endl;
                            //perror("execvp");
                        }
                        for (int i = 0; i < cmd_list[i].size(); i++)
                        {
                            delete [] argv[i];
                        }
                        delete [] argv;
                        exit(0);
                    }
                    else
                    {
                        if (i == 0)
                        {
                            if(user_pipe_in){
                                if(!up_in_error){
                                    close(user_pipe_list[sender - 1][current_user.id - 1].fd[0]);
                                }
                                else{
                                    close(nullfd_in);
                                }
                            }
                            close(pipefd_list[0].fd[1]);
                        }
                        else
                        {
                            close(pipefd_list[i - 1].fd[0]);
                            close(pipefd_list[i].fd[1]);
                        }
                        pidlist.push_back(pid);
                    }
                }
                //last command not include >
                else if (cmd_list[cmdnum - 1].size() <= 2 || (cmd_list[cmdnum - 1].size() > 2 && cmd_list[cmdnum - 1][cmd_list[cmdnum - 1].size() - 2] != ">"))
                {
                    while ((pid = fork()) < 0)
                    {
                        waitpid(-1, NULL, 0);
                    }
                    if (pid == 0)
                    {
                        if (dup2(pipefd_list[i - 1].fd[0], 0) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }

                        if (has_np && !np_same)
                        {
                            if (dup2(nplist[nplist.size() - 1].fd[1], 1) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                            if (has_error)
                            {
                                if (dup2(nplist[nplist.size() - 1].fd[1], 2) < 0)
                                {
                                    perror("dup2 error");
                                    exit(-1);
                                }
                            }
                        }
                        else if (has_np && np_same)
                        {
                            if (dup2(nplist[np_same_index].fd[1], 1) < 0)
                            {
                                perror("dup2 error");
                                exit(-1);
                            }
                            if (has_error)
                            {
                                if (dup2(nplist[np_same_index].fd[1], 2) < 0)
                                {
                                    perror("dup2 error");
                                    exit(-1);
                                }
                            }
                        }

                        if (has_user_pipe)
                        {
                            if (!up_out_error)
                            {
                                dup2(user_pipe_list[current_user.id - 1][receiver - 1].fd[1], 1);
                                close(user_pipe_list[current_user.id - 1][receiver - 1].fd[0]);
                            }
                            else
                            {
                                dup2(nullfd_out, 1);
                            }
                        }

                        for (int i = 0; i < nplist.size(); i++)
                        {
                            close(nplist[i].fd[0]);
                            close(nplist[i].fd[1]);
                        }

                        if (!has_np && !has_user_pipe)
                        {
                            dup2(fd, 1);
                            dup2(fd, 2);
                        }
                        if((has_np && !has_error) || has_user_pipe){
                            dup2(fd, 2);
                        }                        

                        const char *p = cmd_list[i][0].c_str();
                        char **argv = new char *[cmd_list[i].size() + 1];
                        convert(cmd_list[i], argv);
                        if (execvp(p, argv) == -1)
                        {
                            cerr << "Unknown command: [" << p << "]." << endl;
                            //perror("execvp");
                        }
                        for (int i = 0; i < cmd_list[i].size(); i++)
                        {
                            delete [] argv[i];
                        }
                        delete [] argv;
                        exit(0);
                    }
                    else
                    {
                        if (has_np)
                        {
                            nplist[nplist.size() - 1].pid = pid;
                        }
                        else
                        {
                            pidlist.push_back(pid);
                        }

                        close(pipefd_list[i - 1].fd[0]);
                        //remove pipe for pipe_list
                        pipefd_list.clear();

                        if (np_in)
                        {
                            close(nplist[np_in_index].fd[0]);
                            close(nplist[np_in_index].fd[1]);
                            if (np_in_list.size() == 1)
                            {
                                nplist.erase(nplist.begin() + np_in_index);
                            }
                            else
                            {
                                for (int i = np_in_list.size() - 1; i >= 0; i--)
                                {
                                    nplist.erase(nplist.begin() + np_in_list[i]);
                                }
                            }
                        }

                        if (has_user_pipe)
                        {
                            if (!up_out_error)
                            {
                                close(user_pipe_list[current_user.id - 1][receiver - 1].fd[1]);
                            }
                            else
                            {
                                close(nullfd_out);
                            }
                        }

                        if (!has_np && !has_user_pipe)
                        {
                            for (int i = 0; i < cmdnum; i++)
                            {
                                waitpid(pidlist[i], NULL, 0);
                            }
                        }
                        pidlist.clear();
                    }
                }
                //last command include >
                else
                {
                    const char *filename = cmd_list[i][cmd_list[i].size() - 1].c_str();
                    int out = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    while ((pid = fork()) < 0)
                    {
                        waitpid(-1, NULL, 0);
                    }
                    if (pid == 0)
                    {
                        if (dup2(pipefd_list[i - 1].fd[0], 0) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }
                        //replace stdout to out
                        if (dup2(out, 1) < 0)
                        {
                            perror("dup2 error");
                            exit(-1);
                        }

                        for (int i = 0; i < nplist.size(); i++)
                        {
                            close(nplist[i].fd[0]);
                            close(nplist[i].fd[1]);
                        }

                        dup2(fd, 2);

                        const char *p = cmd_list[i][0].c_str();
                        char **argv = new char *[cmd_list[i].size() - 2 + 1];
                        vector<string> cmd;
                        cmd.resize(cmd_list[i].size() - 2);
                        for (int j = 0; j < cmd_list[i].size() - 2; j++)
                        {
                            cmd[j] = cmd_list[i][j];
                        }
                        convert(cmd, argv);
                        if (execvp(p, argv) == -1)
                        {
                            cerr << "Unknown command: [" << p << "]." << endl;
                            //perror("execvp");
                        }
                        for (int i = 0; i < cmd.size(); i++)
                        {
                            delete [] argv[i];
                        }
                        delete [] argv;
                        exit(0);
                    }
                    else
                    {
                        close(pipefd_list[i - 1].fd[0]);
                        close(out);
                        pidlist.push_back(pid);

                        //remove pipe for pipe_list
                        pipefd_list.clear();

                        if (np_in)
                        {
                            close(nplist[np_in_index].fd[0]);
                            close(nplist[np_in_index].fd[1]);
                            if (np_in_list.size() == 1)
                            {
                                nplist.erase(nplist.begin() + np_in_index);
                            }
                            else
                            {
                                for (int i = np_in_list.size() - 1; i >= 0; i--)
                                {
                                    nplist.erase(nplist.begin() + np_in_list[i]);
                                }
                            }
                        }
                        for (int i = 0; i < cmdnum; i++)
                        {
                            waitpid(pidlist[i], NULL, 0);
                        }
                        pidlist.clear();
                    }
                }
            }
        }
    }
    //save nplist to current user
    current_user.nplist = nplist; 
    //print command line prompt
    write(fd, "% ", 2);

    return readbyte;
}

int main(int argc, char **argv)
{
    char *service = argv[1];
    struct sockaddr_in fsin;
    int alen;
    fd_set rfds;
    fd_set afds;
    int nfds;
    //set master socket
    msock = passiveTCP(service, QLEN);
    cout << "msock = " << msock << endl;
    //prepare for select funciton
    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(msock,&afds);
    //set signal handler to wait for child
    signal(SIGCHLD, childHandler);
    //iterator to find
    map<int, int>::iterator id_it;
    map<int, user>::iterator sock_it;
    map<string, string>::iterator env_it;

    user current_user;

    //initialize user pipe list
    user_pipe_list.resize(30);
    for(int i = 0; i < user_pipe_list.size(); i++){
        user_pipe_list[i].resize(30);
        for(int j = 0; j < user_pipe_list[i].size(); j++){
            user_pipe_list[i][j].pipe_exist = false;
        }
    }

    while (1)
    {
        memcpy(&rfds, &afds, sizeof(rfds));

        if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0){
            if (errno == EINTR)
                continue;
            cerr << "select error" << endl;
        }

        if(FD_ISSET(msock, &rfds)){
            int ssock;
            alen = sizeof(fsin);
            ssock = accept(msock, (struct sockaddr *)&fsin, (socklen_t *)&alen);
            if (ssock < 0)
            {
                if (errno == EINTR)
                    continue;
                cerr << "accept error" << endl;
            }
            FD_SET(ssock, &afds);
            int user_id = 0;
            //store id-sockfd pair in map
            for(int i = 1; i <= 30; i++){
                id_it = idmap.find(i);
                if(id_it == idmap.end()){
                    user_id = i;
                    idmap.insert(pair<int, int>(i, ssock));
                    break;
                }
            }
            //create new user
            user new_user;
            new_user.name = "(no name)";
            new_user.env.insert(pair<string, string>("PATH", "bin:."));
            string ip = string(inet_ntoa(fsin.sin_addr));
            string port = to_string(ntohs(fsin.sin_port));
            string ip_port;
            ip_port.append(ip);
            ip_port.append(":");
            ip_port.append(port);
            new_user.ip_port = ip_port;
            new_user.id = user_id;
            //store sockfd-user pair in map
            sock_it = usermap.find(ssock);
            if(sock_it != usermap.end()){
                cout << "user with sockfd " << ssock << "hasn't deleted!" << endl;
                usermap.erase(sock_it);
            }
            usermap.insert(pair<int, user>(ssock, new_user));
            //print welcome message
            write(ssock, "****************************************\n** Welcome to the information server. **\n****************************************\n", 123);
            //broadcast login message            
            string login_message = "*** User '";
            login_message.append(new_user.name);
            login_message.append("' entered from ");
            login_message.append(new_user.ip_port);
            login_message.append(". ***\n");  
            for(sock_it = usermap.begin(); sock_it != usermap.end(); sock_it++){
                write(sock_it->first, login_message.c_str(), login_message.size());
            }           
            //print command line prompt
            write(ssock, "% ", 2);
        }
        for(int fd = 0; fd < nfds; fd++){
            if(fd != msock && FD_ISSET(fd, &rfds)){
                //clear all the environment variable
                clearenv();
                //set user own envirionment variable
                current_user = usermap[fd];
                //cout << "nplist size of sockfd " << fd << " is:" << current_user.nplist.size() << endl;
                
                for(env_it = current_user.env.begin(); env_it != current_user.env.end(); env_it++){
                    const char *c1 = env_it->first.c_str();
                    const char *c2 = env_it->second.c_str();
                    //cout << "set environment variable " << env_it->first << " : " << env_it->second << endl;
                    setenv(c1, c2, 1);
                }
                if(npshell(fd, current_user.nplist, current_user.pipefd_list, usermap[fd]) == 0){
                    //user leave
                    //clear the user structure
                    cout << "user leave" << endl;
                    //close user pipe and set pipe_exist to false                   
                    for(int i = 0; i < user_pipe_list.size(); i++){
                        if(user_pipe_list[current_user.id - 1][i].pipe_exist){
                            close(user_pipe_list[current_user.id - 1][i].fd[0]);
                            user_pipe_list[current_user.id - 1][i].pipe_exist = false;
                        }
                        if(user_pipe_list[i][current_user.id - 1].pipe_exist){
                            close(user_pipe_list[i][current_user.id - 1].fd[0]);
                            user_pipe_list[i][current_user.id - 1].pipe_exist = false;
                        }
                    }
                    for(int i = 0; i < current_user.nplist.size(); i++){
                        close(current_user.nplist[i].fd[0]);
                        close(current_user.nplist[i].fd[1]);
                    }
                    current_user.pipefd_list.clear();
                    id_it = idmap.find(current_user.id);
                    if(id_it != idmap.end()){
                        idmap.erase(id_it);
                    }
                    sock_it = usermap.find(fd);
                    if(sock_it != usermap.end()){
                        usermap.erase(sock_it);
                    }
                    //send leave message
                    string leave_message = "*** User '";
                    leave_message.append(current_user.name);
                    leave_message.append("' left. ***\n");
                    for (sock_it = usermap.begin(); sock_it != usermap.end(); sock_it++)
                    {
                        write(sock_it->first, leave_message.c_str(), leave_message.size());
                    }
                    //close socket and clear index on afds
                    (void)close(fd);
                    FD_CLR(fd, &afds);
                }
            }
        }
    }
}
