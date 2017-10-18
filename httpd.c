/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(int);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);


//处理套接字上监听到的HTTP请求   */
void accept_request(int client)
{
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;      /* becomes true if server decides this is a CGI
                    * program */
    char *query_string = NULL;

    //取得HTTP报文的第一行
    numchars = get_line(client, buf, sizeof(buf));
    i = 0; 
    j = 0;

    //取得方法
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[j];
        i++; 
        j++;
    }
    method[i] = '\0';

    //strcasecmp比较两个字符串是否相等，自动忽略大小小
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        //发送"未实现该方法"
        //TinyHttpd只实现了GET和POST方法
        unimplemented(client);
        return;
    }

    //如果是post方法，将CGI置为真
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    //跳过所有空格，method和url之间可能有多个空格
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    //将url拷贝到url[]中
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; 
        j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        //如果url中带有解析参数，需要进行截取
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    //url格式化到path
    sprintf(path, "htdocs%s", url);
    //如果是path是目录，默认为首页index.html
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    //stat()通过文件名(path)获取信息
    //保存在结构体st中
    //成功返回0，失败返回-1
    if (stat(path, &st) == -1) //寻找该文件是否存在
    {
        //如果不存在，忽略该请求后续内容(head、body)
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        //发送一个找不到文件的response给客户端
        not_found(client);
    }
    //如果找到该文件
    else
    {
        //S_IFMT为掩码
        //&运算把无关位置0，然后比较
        //判断是否为目录
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            //是目录，则结尾再加个index.html
            strcat(path, "/index.html");

        // 用户-执行 组-执行 其他-执行 
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            //如果是可执行文件，不管权限如何，cgi置为1
            cgi = 1;
        if (!cgi)
            //如果不需要CGI机制
            serve_file(client, path);
        else
            //需要CGI机制则调用
            execute_cgi(client, path, method, query_string);
    }

    //关闭套接字
    close(client);
}
//通知客户端有错误发生
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

//告诉客户端CGI脚本不能被执行
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 perror(sc);
 exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
 char buf[1024];
 int cgi_output[2];
 int cgi_input[2];
 pid_t pid;
 int status;
 int i;
 char c;
 int numchars = 1;
 int content_length = -1;

 buf[0] = 'A'; buf[1] = '\0';
 if (strcasecmp(method, "GET") == 0)
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
 else    /* POST */
 {
  numchars = get_line(client, buf, sizeof(buf));
  while ((numchars > 0) && strcmp("\n", buf))
  {
   buf[15] = '\0';
   if (strcasecmp(buf, "Content-Length:") == 0)
    content_length = atoi(&(buf[16]));
   numchars = get_line(client, buf, sizeof(buf));
  }
  if (content_length == -1) {
   bad_request(client);
   return;
  }
 }

 sprintf(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);

 if (pipe(cgi_output) < 0) {
  cannot_execute(client);
  return;
 }
 if (pipe(cgi_input) < 0) {
  cannot_execute(client);
  return;
 }

 if ( (pid = fork()) < 0 ) {
  cannot_execute(client);
  return;
 }
 if (pid == 0)  /* child: CGI script */
 {
  char meth_env[255];
  char query_env[255];
  char length_env[255];

  dup2(cgi_output[1], 1);
  dup2(cgi_input[0], 0);
  close(cgi_output[0]);
  close(cgi_input[1]);
  sprintf(meth_env, "REQUEST_METHOD=%s", method);
  putenv(meth_env);
  if (strcasecmp(method, "GET") == 0) {
   sprintf(query_env, "QUERY_STRING=%s", query_string);
   putenv(query_env);
  }
  else {   /* POST */
   sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
   putenv(length_env);
  }
  execl(path, path, NULL);
  exit(0);
 } else {    /* parent */
  close(cgi_output[1]);
  close(cgi_input[0]);
  if (strcasecmp(method, "POST") == 0)
   for (i = 0; i < content_length; i++) {
    recv(client, &c, 1, 0);
    write(cgi_input[1], &c, 1);
   }
  while (read(cgi_output[0], &c, 1) > 0)
   send(client, &c, 1, 0);

  close(cgi_output[0]);
  close(cgi_input[1]);
  waitpid(pid, &status, 0);
 }
}


//从socket buffer中获取一行
int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        //读一个字节存放在C中
        //成功则返回实际读取字节数
        //最后一个参数flags设为0，表示读取数据并且从sock中删除已经读取的数据
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            //如果是回车符继续读取
            if (c == '\r')
            {
                //使用 MSG_PEEK 标志使下一次读取依然可以得到这次读取的内容
                //即不删除已读数据 
                n = recv(sock, &c, 1, MSG_PEEK);
                //如果是换行符，继续读取下一个字符
                //并删除已读数据
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                //只读取到回车符而没有换行符
                //c置为换行符，终止读取
                else
                    c = '\n';
            }
        buf[i] = c;
        i++;
        }
        //没有读到任何数据
        else
            c = '\n';
    }
    buf[i] = '\0';
 
    //返回读到字节数，包括'\0'
    return(i);
}


//返回HTTP响应头
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}


//给客户端404错误(not found)
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    //以只读方式打开文件
    resource = fopen(filename, "r");

    //如果文件不存在
    if (resource == NULL)
        not_found(client);
    else
    {
        //先返回文件头部消息
        headers(client, filename);
        cat(client, resource);
    }
    //关闭
    fclose(resource);
}


//开启http服务，包括绑定端口，监听，开启线程处理链接
int startup(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;
    //创建套接字
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    //创建失败，输出错误，退出程序
    if (httpd == -1)
        error_die("socket");
    //name参数赋值
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    //bind及失败处理
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");

    if (*port == 0)  /* if dynamically allocating a port */
    {
        int namelen = sizeof(name);
        //在以端口号0调用bind(告知内核去动态选择本地端口号)后，getsockname用来返回由内核赋予的本地端口号
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        //更新端口号
        //ntohs用来把short字节顺序从网络顺序改为主机顺序(Network to Host Short)
        *port = ntohs(name.sin_port);
    }
    //监听
    if (listen(httpd, 5) < 0)
        error_die("listen");

    //返回套接字描述符
    return(httpd);
}


//告诉客户端该方法(method)未被实现
void unimplemented(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</TITLE></HEAD>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}


int main(void)
{
    int server_sock = -1;
    //unsigned short (2 bytes)
    u_short port = 0;
    int client_sock = -1;

    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);

    pthread_t newthread;

    //监听套接字描述符
    server_sock = startup(&port);

    printf("httpd running on port %d\n", port);

    while (1)
    {
        client_sock = accept(server_sock,
            (struct sockaddr *)&client_name,
                        &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        /* accept_request(client_sock); */

        //启动线程处理新的连接
        if (pthread_create(&newthread , NULL, accept_request, client_sock) != 0)
            perror("pthread_create");
    }

    //关闭套接字
    close(server_sock);

    return(0);
}
