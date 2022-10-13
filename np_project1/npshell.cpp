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
using namespace std;

//struct to deal with number pipe
typedef struct numberpipe{
    int fd[2];
    int remainline;
    bool error;
    pid_t pid;
} numberpipe;

vector<numberpipe> nplist; 

//parse cmd function
vector< vector<string>> parse(string cmd){
    vector< vector<string>> cmd_list;
	vector<string> parsed_cmd;
	istringstream iss(cmd);
    string s;
    while(iss >> s){
        if(s[0] != '|' && s[0] != '!'){
            parsed_cmd.push_back(s);
        }
        else if(s[0] == '|' && s.size() >= 2){
            cmd_list.push_back(parsed_cmd);
            parsed_cmd.clear();

            numberpipe np;
            np.remainline = stoi(s.substr(1,s.size()-1));
            np.error = false;
            nplist.push_back(np);
        }
        else if(s[0] == '!' && s.size() >= 2){
            cmd_list.push_back(parsed_cmd);
            parsed_cmd.clear();

            numberpipe np;
            np.remainline = stoi(s.substr(1,s.size()-1));
            np.error = true;
            nplist.push_back(np);
        }
        else{
            cmd_list.push_back(parsed_cmd);
            parsed_cmd.clear();
        }
    }
    if(parsed_cmd.size() != 0){
        cmd_list.push_back(parsed_cmd);
    }
	return cmd_list;
}
//convert vector<string> to char**
void convert(const vector<string> &v, char** argv){
    for(unsigned int i = 0; i < v.size(); ++i){
            argv[i] = new char[v[i].size()+1]; 
            strcpy(argv[i], v[ i ].c_str());              
    }
    argv[v.size()] = NULL;
}
//struct of a single pipe
typedef struct pipefd{
    int fd[2];
    pid_t pid;
} pipefd;
//the pipe list
vector<pipefd> pipefd_list;

void childHandler(int signo){
    int status;
    while(waitpid(-1,&status,WNOHANG) > 0){

    }
}

int main(int argc, char const *argv[], char *envp[])
{
    //set initial PATH
	setenv("PATH","bin:.",1);
    //check environment variable
    /*
    for (char **env = envp; *env != 0; env++){
        char *thisEnv = *env;
        cout << thisEnv << endl;  
    }
    */
	while(1){
        
        //print command line prompt
		cout << "% ";
		//read command
		string cmd;
		getline(cin, cmd);
        //save nplist size to check whether nplist increased
        int npnum = nplist.size();
		//parse the command
        vector< vector<string>> cmd_list =  parse(cmd);
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
        if(nplist.size() != npnum){
            //cout << "new number pipe!" << endl;
            has_np = true;
            has_error = nplist[nplist.size()-1].error;
        }

        //count down all the number pipe except the new one       
        if(cmd_list.size() != 0){
            if(has_np){
                if(nplist.size() >= 2){
                    for(int i = 0; i < nplist.size()-1; i++){
                        nplist[i].remainline--;
                    }
                }   
            }
            else{
                for(int i = 0; i < nplist.size(); i++){
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
        if(nplist.size() != 0){
            for(int i = 0; i < nplist.size(); i++){
                if(nplist[i].remainline == 0){
                    np_in = true;
                    //only record first number pipe because output need to merge to first number pipe
                    if(np_in_index == -1)
                        np_in_index = i;
                    np_in_list.push_back(i);
                }
            }
        } 

        //if command contains number pipe, check whether line is same as element in nplist
        bool np_same = false;
        int np_same_index = -1;
        if(nplist.size() >= 2){
            for(int i = 0; i < nplist.size()-1; i++){
                if(nplist[i].remainline == nplist[nplist.size()-1].remainline){
                    np_same = true;
                    np_same_index = i;
                    break;
                }
            }
        }     

        signal(SIGCHLD, childHandler);   
		
		//deal with built-in commands exit,printenv and setenv
        if(cmd_list.size() == 1 && cmd_list[0][0] == "exit"){
        	exit(0);
        }
        else if(cmd_list.size() == 1 && cmd_list[0].size() == 2 && cmd_list[0][0] == "printenv"){
    		const char *c = cmd_list[0][1].c_str();
    		char* value = getenv(c);
    		if(value != NULL){
    			cout << value << endl;
    		}
        }
        else if (cmd_list.size() == 1 && cmd_list[0].size() == 3 && cmd_list[0][0] == "setenv"){
    		const char *c1 = cmd_list[0][1].c_str();
    		const char *c2 = cmd_list[0][2].c_str();
    		setenv(c1,c2,1);
        }
        //execute one command 
        else if (cmd_list.size() == 1){
            //one command without >
            if(cmd_list[0].size() <=2 || (cmd_list[0].size() > 2 && cmd_list[0][cmd_list[0].size()-2] != ">")){    
                if(!has_np && !np_in){
                    while((pid = fork()) < 0){
                        waitpid(-1, NULL, 0);
                    }                        
                    if(pid == 0){
                        for(int i = 0; i < nplist.size(); i++){
                            close(nplist[i].fd[0]);
                            close(nplist[i].fd[1]);
                        }  
                        const char *p = cmd_list[0][0].c_str();
                        char ** argv = new char *[cmd_list[0].size()+1];
                        convert(cmd_list[0],argv);     
                        if(execvp(p,argv) == -1){
                            cerr << "Unknown command: [" << p << "]." << endl;
                            //perror("execvp");
                        }
                        delete argv;
                        exit(0);
                    }
                    else{
                        waitpid(pid,NULL,0);
                    }
                }  
                else{
                    if(has_np && !np_same){
                        pipefd pfd;
                        pipe(pfd.fd);
                        nplist[nplist.size()-1].fd[0] = pfd.fd[0];
                        nplist[nplist.size()-1].fd[1] = pfd.fd[1];
                    }
                    else if(has_np && np_same){
                        nplist[nplist.size()-1].fd[0] = nplist[np_same_index].fd[0];
                        nplist[nplist.size()-1].fd[1] = nplist[np_same_index].fd[1];
                    }

                    while((pid = fork()) < 0){
                        waitpid(-1, NULL, 0);
                    }
                    if(pid == 0){
                        if(has_np && !np_same){
                            if(dup2(nplist[nplist.size()-1].fd[1] , 1) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }
                            if(has_error){
                                if(dup2(nplist[nplist.size()-1].fd[1] , 2) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                            }
                        }
                        else if(has_np && np_same){
                            if(dup2(nplist[np_same_index].fd[1] , 1) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }
                            if(has_error){
                                if(dup2(nplist[np_same_index].fd[1] , 2) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                            }
                        }    
                        if(np_in){
                            if(dup2(nplist[np_in_index].fd[0] , 0) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }
                        }
                        for(int i = 0; i < nplist.size(); i++){
                            close(nplist[i].fd[0]);
                            close(nplist[i].fd[1]);
                        }  
                        const char *p = cmd_list[0][0].c_str();
                        char ** argv = new char *[cmd_list[0].size()+1];
                        convert(cmd_list[0],argv);     
                        if(execvp(p,argv) == -1){
                            cerr << "Unknown command: [" << p << "]." << endl;
                            //perror("execvp");
                        }
                        delete argv;
                        exit(0);
                    }
                    else{
                        if(has_np){
                            nplist[nplist.size()-1].pid = pid;
                        }
                        if(np_in){
                            close(nplist[np_in_index].fd[0]);
                            close(nplist[np_in_index].fd[1]);
                            if(!has_np)
                                waitpid(pid, NULL, 0);
                            if(np_in_list.size() == 1){
                                nplist.erase(nplist.begin()+np_in_index);
                            }
                            else{                                                   
                                for(int i = np_in_list.size()-1; i >= 0; i--){
                                    nplist.erase(nplist.begin()+np_in_list[i]);
                                } 
                            }
                        }
                    }
                }        
            }
            //one command include >
            else{
                const char *filename = cmd_list[0][cmd_list[0].size()-1].c_str();
                int out = open(filename,O_WRONLY|O_CREAT|O_TRUNC, 0666);
                while((pid = fork()) < 0){
                    waitpid(-1, NULL, 0);
                }
                if(pid == 0){
                    if(np_in){
                        if(dup2(nplist[np_in_index].fd[0] , 0) < 0){
                            perror("dup2 error");
                            exit(-1);
                        }
                    }
                    //replace stdout to out
                    if(dup2(out, 1) < 0){
                        perror("dup2 error");
                        exit(-1);
                    }
                    for(int i = 0; i < nplist.size(); i++){
                        close(nplist[i].fd[0]);
                        close(nplist[i].fd[1]);
                    } 
                    const char *p = cmd_list[0][0].c_str();
                    char ** argv = new char *[cmd_list[0].size()-2+1];
                    vector<string> cmd;
                    cmd.resize(cmd_list[0].size()-2);
                    for(int i = 0; i < cmd_list[0].size()-2; i++){
                        cmd[i] = cmd_list[0][i];
                    }
                    convert(cmd,argv);     
                    if(execvp(p,argv) == -1){
                        cerr << "Unknown command: [" << p << "]." << endl;
                        //perror("execvp");
                    }
                    delete argv;
                    exit(0);
                }
                else{
                    if(!np_in){
                        close(out);
                        waitpid(pid,NULL,0);
                    }
                    else{
                        close(nplist[np_in_index].fd[0]);
                        close(nplist[np_in_index].fd[1]);
                        close(out);
                        waitpid(pid, NULL, 0);                      
                        if(np_in_list.size() == 1){
                            nplist.erase(nplist.begin()+np_in_index);
                        }
                        else{                                             
                            for(int i = np_in_list.size()-1; i >= 0; i--){
                                nplist.erase(nplist.begin()+np_in_list[i]);
                            } 
                        }
                    } 
                }   
            }
        }
        //execute command with pipe
        else if(cmd_list.size() >= 2){
            //cout << "open pipe!" << endl;
            int cmdnum = cmd_list.size();
            vector<pid_t> pidlist;
            //cout << "current pipe index: " << currentpipeindex << endl;
            if(!has_np && !np_in){                                    
                for(int i = 0 ; i < cmdnum; i++){  
                    if(i != cmdnum - 1){
                        pipefd pfd;                
                        if(pipe(pfd.fd) < 0){
                            perror("pipe error");
                        }
                        pipefd_list.push_back(pfd);
                        while((pid = fork()) < 0){
                            waitpid(-1, NULL, 0);
                        }
                        if(pid == 0){
                            if(i == 0){
                                if(dup2(pipefd_list[i].fd[1] , 1) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                            }
                            else{
                                if(dup2(pipefd_list[i-1].fd[0] , 0) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                                if(dup2(pipefd_list[i].fd[1] , 1) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                            }    

                            close(pipefd_list[i].fd[0]);
                            
                            for(int i = 0; i < nplist.size(); i++){
                                close(nplist[i].fd[0]);
                                close(nplist[i].fd[1]);
                            }

                            const char *p = cmd_list[i][0].c_str();
                            char ** argv = new char *[cmd_list[i].size()+1];
                            convert(cmd_list[i],argv);     
                            if(execvp(p,argv) == -1){
                                cerr << "Unknown command: [" << p << "]." << endl;
                                //perror("execvp");
                            }
                            delete argv;
                            exit(0);
                        }
                        else{
                            if(i == 0){
                                close(pipefd_list[0].fd[1]);
                            }
                            else{
                                close(pipefd_list[i-1].fd[0]);
                                close(pipefd_list[i].fd[1]);
                            }
                            pidlist.push_back(pid);
                        }
                    }   
                    //last command not include >
                    else if(cmd_list[cmdnum-1].size() <= 2 || (cmd_list[cmdnum-1].size() > 2 && cmd_list[cmdnum-1][cmd_list[cmdnum-1].size()-2] != ">")){
                        while((pid = fork()) < 0){
                            waitpid(-1, NULL, 0);
                        }
                        if(pid == 0){
                            if(dup2(pipefd_list[i-1].fd[0] , 0) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }    

                            for(int i = 0; i < nplist.size(); i++){
                                close(nplist[i].fd[0]);
                                close(nplist[i].fd[1]);
                            }

                            const char *p = cmd_list[i][0].c_str();
                            char ** argv = new char *[cmd_list[i].size()+1];
                            convert(cmd_list[i],argv);     
                            if(execvp(p,argv) == -1){
                                cerr << "Unknown command: [" << p << "]." << endl;
                                //perror("execvp");
                            }
                            delete argv;
                            exit(0);
                        }
                        else{
                            close(pipefd_list[i-1].fd[0]);
                            pidlist.push_back(pid);
                        }
                    }          
                    //last command include >                    
                    else{
                        const char *filename = cmd_list[i][cmd_list[i].size()-1].c_str();
                        int out = open(filename,O_WRONLY|O_CREAT|O_TRUNC, 0666);
                        while((pid = fork()) < 0){
                            waitpid(-1, NULL, 0);
                        }
                        if(pid == 0){
                            if(dup2(pipefd_list[i-1].fd[0] , 0) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }
                            //replace stdout to out
                            if(dup2(out, 1) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }
                            
                            for(int i = 0; i < nplist.size(); i++){
                                close(nplist[i].fd[0]);
                                close(nplist[i].fd[1]);
                            }

                            const char *p = cmd_list[i][0].c_str();
                            char ** argv = new char *[cmd_list[i].size()-2+1];
                            vector<string> cmd;
                            cmd.resize(cmd_list[i].size()-2);
                            for(int j = 0; j < cmd_list[i].size()-2; j++){
                                cmd[j] = cmd_list[i][j];
                            }
                            convert(cmd,argv);     
                            if(execvp(p,argv) == -1){
                                cerr << "Unknown command: [" << p << "]." << endl;
                                //perror("execvp");
                            }
                            delete argv;
                            exit(0);
                        }
                        else{
                            close(pipefd_list[i-1].fd[0]);
                            close(out);
                            pidlist.push_back(pid);
                        }
                    }
                }
                for(int i = 0; i < cmdnum; i++){
                    waitpid(pidlist[i], NULL, 0);
                } 
                pidlist.clear();        
                //remove pipe for pipe_list
                pipefd_list.clear();
            }
            //pipe contains number pipe
            else{
                //cout << "create pipe with number pipe or number pipe in" << endl;
                if(has_np && !np_same){
                    pipefd pfd;
                    pipe(pfd.fd);
                    nplist[nplist.size()-1].fd[0] = pfd.fd[0];
                    nplist[nplist.size()-1].fd[1] = pfd.fd[1];
                }
                else if(has_np && np_same){
                    nplist[nplist.size()-1].fd[0] = nplist[np_same_index].fd[0];
                    nplist[nplist.size()-1].fd[1] = nplist[np_same_index].fd[1];
                }
                for(int i = 0 ; i < cmdnum; i++){  
                    if(i != cmdnum - 1){
                        pipefd pfd;                
                        if(pipe(pfd.fd) < 0){
                            perror("pipe error");
                        }
                        pipefd_list.push_back(pfd);
                        while((pid = fork()) < 0){
                            waitpid(-1, NULL, 0);
                        }
                        if(pid == 0){
                            if(i == 0){
                                if(dup2(pipefd_list[i].fd[1] , 1) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                                if(np_in){
                                    if(dup2(nplist[np_in_index].fd[0] , 0) < 0){
                                        perror("dup2 error");
                                        exit(-1);
                                    }
                                }
                            }
                            else{
                                if(dup2(pipefd_list[i-1].fd[0] , 0) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                                if(dup2(pipefd_list[i].fd[1] , 1) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                            }          

                            close(pipefd_list[i].fd[0]);

                            for(int i = 0; i < nplist.size(); i++){
                                close(nplist[i].fd[0]);
                                close(nplist[i].fd[1]);
                            }
                            const char *p = cmd_list[i][0].c_str();
                            char ** argv = new char *[cmd_list[i].size()+1];
                            convert(cmd_list[i],argv);     
                            if(execvp(p,argv) == -1){
                                cerr << "Unknown command: [" << p << "]." << endl;
                                //perror("execvp");
                            }
                            delete argv;
                            exit(0);
                        }
                        else{
                            if(i == 0){
                                close(pipefd_list[0].fd[1]);
                            }
                            else{
                                close(pipefd_list[i-1].fd[0]);
                                close(pipefd_list[i].fd[1]);
                            }
                            pidlist.push_back(pid);
                        }
                    }   
                    //last command not include >
                    else if(cmd_list[cmdnum-1].size() <= 2 || (cmd_list[cmdnum-1].size() > 2 && cmd_list[cmdnum-1][cmd_list[cmdnum-1].size()-2] != ">")){
                        while((pid = fork()) < 0){
                            waitpid(-1, NULL, 0);
                        }
                        if(pid == 0){
                            if(dup2(pipefd_list[i-1].fd[0] , 0) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }    

                            if(has_np && !np_same){
                                if(dup2(nplist[nplist.size()-1].fd[1] , 1) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                                if(has_error){
                                    if(dup2(nplist[nplist.size()-1].fd[1] , 2) < 0){
                                        perror("dup2 error");
                                        exit(-1);
                                    }
                                }
                            }
                            else if(has_np && np_same){
                                if(dup2(nplist[np_same_index].fd[1] , 1) < 0){
                                    perror("dup2 error");
                                    exit(-1);
                                }
                                if(has_error){
                                    if(dup2(nplist[np_same_index].fd[1] , 2) < 0){
                                        perror("dup2 error");
                                        exit(-1);
                                    }
                                }
                            }                                           

                            for(int i = 0; i < nplist.size(); i++){
                                close(nplist[i].fd[0]);
                                close(nplist[i].fd[1]);
                            }
                            const char *p = cmd_list[i][0].c_str();
                            char ** argv = new char *[cmd_list[i].size()+1];
                            convert(cmd_list[i],argv);     
                            if(execvp(p,argv) == -1){
                                cerr << "Unknown command: [" << p << "]." << endl;
                                //perror("execvp");
                            }
                            delete argv;
                            exit(0);
                        }
                        else{
                            if(has_np){
                                nplist[nplist.size()-1].pid = pid;
                            }
                            else{
                                pidlist.push_back(pid);
                            }
          
                            //remove pipe for pipe_list
                            pipefd_list.clear();

                            if(np_in){
                                close(nplist[np_in_index].fd[0]);
                                close(nplist[np_in_index].fd[1]);
                                if(np_in_list.size() == 1){
                                    nplist.erase(nplist.begin()+np_in_index);
                                }
                                else{                                                       
                                    for(int i = np_in_list.size()-1; i >= 0; i--){
                                        nplist.erase(nplist.begin()+np_in_list[i]);
                                    } 
                                }
                            }  
                            if(!has_np){
                                for(int i = 0; i < cmdnum; i++){
                                    waitpid(pidlist[i], NULL, 0);
                                } 
                            }                                                         
                            pidlist.clear();
                        }
                    }          
                    //last command include >                    
                    else{
                        const char *filename = cmd_list[i][cmd_list[i].size()-1].c_str();
                        int out = open(filename,O_WRONLY|O_CREAT|O_TRUNC, 0666);
                        while((pid = fork()) < 0){
                            waitpid(-1, NULL, 0);
                        }
                        if(pid == 0){
                            if(dup2(pipefd_list[i-1].fd[0] , 0) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }
                            //replace stdout to out
                            if(dup2(out, 1) < 0){
                                perror("dup2 error");
                                exit(-1);
                            }

                            for(int i = 0; i < nplist.size(); i++){
                                close(nplist[i].fd[0]);
                                close(nplist[i].fd[1]);
                            }
                            const char *p = cmd_list[i][0].c_str();
                            char ** argv = new char *[cmd_list[i].size()-2+1];
                            vector<string> cmd;
                            cmd.resize(cmd_list[i].size()-2);
                            for(int j = 0; j < cmd_list[i].size()-2; j++){
                                cmd[j] = cmd_list[i][j];
                            }
                            convert(cmd,argv);     
                            if(execvp(p,argv) == -1){
                                cerr << "Unknown command: [" << p << "]." << endl;
                                //perror("execvp");
                            }
                            delete argv;
                            exit(0);
                        }
                        else{
                            close(pipefd_list[i-1].fd[0]);
                            close(out);
                            pidlist.push_back(pid);

                            //remove pipe for pipe_list
                            pipefd_list.clear();

                            if(np_in){
                                close(nplist[np_in_index].fd[0]);
                                close(nplist[np_in_index].fd[1]);
                                if(np_in_list.size() == 1){
                                    nplist.erase(nplist.begin()+np_in_index);
                                }
                                else{                                                      
                                    for(int i = np_in_list.size()-1; i >= 0; i--){
                                        nplist.erase(nplist.begin()+np_in_list[i]);
                                    } 
                                }
                            }
                            for(int i = 0; i < cmdnum; i++){
                                waitpid(pidlist[i], NULL, 0);
                            } 
                            for(int i = 0; i < cmdnum; i++){
                                pidlist.pop_back();
                            } 
                        }
                    }
                }
            }   
        }

	}

	return 0;
}
