#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <wait.h>

#define _out STDOUT_FILENO
#define _in STDIN_FILENO
#define _err STDERR_FILENO
#define _aseq_header_len 78
#define _aseq_log_len 58
#define _port_digits 4
#define _conf_path "/.config/pianoterm/config"
#define _wlen(str) str, strlen(str)
#define _wsize(str) str, sizeof(str)
#define _wnext(c) while(*(c) == ' ') c++;

const uint test_note = 21;
const char *N_OFF = "Note off";
const char *N_ON = "Note on";

// first argument - midi port (24)
// TODO: if no argument is passed try to find the port, using aconnect -i
// TODO: add support for multiple commands (&&) and commands not in path $HOME/my_script.sh
// TODO: config to run command if (note on vs note off) (press/release)
// TODO: allow config to use standard notation and convert to key code (C#1 = "echo hello")
// TODO: allow usage just by passing in arguments, without config file
// TODO: option to reload config file?
//
// alsactl (aseqdump) version 1.2.15.2
//

struct shell_command {
    char *path;
    uint argc;
    char **argv;
};
typedef struct shell_command ShellCommand;

struct user_command {
    uint note;
    char *str;
};
typedef struct user_command UserCommand;

struct app_data {
    int channel[2];
    char buffer[124];
    uint port;
    bool act_on_release;
    UserCommand *commands;
    uint n_commands;
};
typedef struct app_data Data;

int readLine(Data *app, int len);
int getNote(Data app);
void runCommand(Data app, uint for_note);
ShellCommand *parseCommand(const char* src);
void freeCommand(ShellCommand *cmd);
void loadConfig(Data *app);

int main(int argc, char**argv) {
    Data app;
    app.port = 24;
    app.act_on_release = false;

    if(argc == 2){
        // if(strlen(argv[1]))
    }

    if(argc <= 1){
        // try to find port using aconnect -i
    }

    char port_str[_port_digits];
    snprintf(port_str, _port_digits, "%u", app.port);
    loadConfig(&app);

    if(pipe(app.channel) == -1){
        write(_err, _wlen("pipe error\n"));
        return 1;
    };

    int pid = fork();
    if(pid == -1) {
        write(_err, _wlen("fork error\n"));
        return 1;
    }

    if(pid == 0){
        close(app.channel[_in]);
        dup2(app.channel[_out], _out);
        dup2(app.channel[_out], _err);

        execlp("aseqdump", "aseqdump", "-p", port_str, 0);
        write(_out, _wlen("_exit\n"));
    } else {
        write(_out, _wlen("Listening for MIDI input on port "));
        write(_out, _wlen(port_str));
        write(_out, _wlen("\n"));

        bool blocked = true;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(app.channel[_in], &fds);

        // ignore any lingering messages
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
        int leftover_msgs = select(app.channel[_in] + 1, &fds, 0, 0, &timeout);
        while(leftover_msgs){
            readLine(&app, _aseq_header_len);
            leftover_msgs = select(app.channel[_in] + 1, &fds, 0, 0, &timeout);
            blocked = false;
        }

        while(1) {
            if(blocked) { 
                readLine(&app, _aseq_header_len);
                blocked = false;
            }

            if(readLine(&app, _aseq_log_len) == -1)
                break;

            int note = getNote(app);
            if(note) runCommand(app, note);
        }

        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }

    return 0;
}

void loadConfig(Data *app){
    const char* home = getenv("HOME");
    if(!home) {
        write(_err, _wlen("$HOME variable not set\n"));
        return;
    }

    char path[strlen(home) + strlen(_conf_path) + 1];
    snprintf(_wsize(path), "%s%s", home, _conf_path);

    int fd = open(path, O_RDONLY);
    if(fd == -1) {
        write(_err, _wlen("Error opening config file "));
        write(_err, _wlen(path));
        write(_err, _wlen("\n"));
        return;
    }

    char b;
    uint l_count = 0;
    while(read(fd, &b, 1) > 0){
       if(b == '\n') l_count++;
    }
    lseek(fd, 0, SEEK_SET);

    char *lines[l_count];

    uint l_cur = 0;
    uint l_bytes = 0;
    uint pos = 0;
    while(read(fd, &b, 1) > 0){
       pos++;
       l_bytes++;
       if(b != '\n') continue;

       //TODO: error handling
       lines[l_cur] = (char*)malloc(l_bytes*sizeof(char));
       lseek(fd, pos - l_bytes, SEEK_SET);
       for(int i = 0; i < l_bytes; i++){
           read(fd, &b, 1);
           lines[l_cur][i] = b;
       }

       lines[l_cur][l_bytes-1] = 0;
       l_bytes = 0;
       l_cur++;
    }

    UserCommand commands[l_count];

    for(l_cur = 0; l_cur < l_count; l_cur++){
        char *c = lines[l_cur];
        _wnext(c);
        if(*c == '#' || *c == 0)
            continue;

        char *end;
        long int note = strtol(c, &end, 10);
        if(note == 0) continue;
        if(end) c = end;

        _wnext(c);
        if(*c != '=') continue;
        c++; _wnext(c);
        if(*c == 0) continue;

        int last = strlen(lines[l_cur]);
        int cmd_len = (int)(&lines[l_cur][last] - c);

        if(app->n_commands == 0)
            app->commands = (UserCommand*)malloc(sizeof(UserCommand));
        else
            app->commands = (UserCommand*)realloc(app->commands, sizeof(UserCommand)*(app->n_commands + 1));

        app->commands[app->n_commands].str = (char*) malloc(sizeof(char)*cmd_len);
        for(int i = 0; i < cmd_len; i++)
            app->commands[app->n_commands].str[i] = *(c++);
        app->commands[app->n_commands].note = note;

        //assert(*c == 0)
        if(*c != 0){
            write(_err, _wlen("panic - c should be 0\n"));
            exit(-1);
        }

        app->n_commands++;
    }

    for(l_cur = 0; l_cur < l_count; l_cur++)
        free(lines[l_cur]);

    close(fd);
}

void runCommand(Data app, uint for_note){
    for(int i = 0; i < app.n_commands; i++)
    {
        if(for_note != app.commands[i].note) continue;

        ShellCommand *c = parseCommand(app.commands[i].str);
        if(c){
            int pid = fork();
            if(pid == 0)
                execvp(c->path, c->argv);
            else
                waitpid(pid, 0, 0);

            freeCommand(c);
        };
    }
}

ShellCommand* parseCommand(const char* src){
    ShellCommand *c = (ShellCommand*) malloc(sizeof(ShellCommand));
    c->argv = 0;
    c->path = 0;
    uint len = strlen(src);
    bool in_quote = false;
    uint pos = 0;

    while(src[pos] == ' ') pos++;
    uint word_start = pos;
mainloop: while(pos <= len){
       if(src[pos] == '\'') in_quote = !in_quote;
       if(in_quote) { pos++; continue; };

       if(src[pos] == ' ' || pos == len){
           uint word_size = pos - word_start + 1;
           char *new_word = (char*)malloc(sizeof(char)*word_size);
           snprintf(new_word, word_size, "%s", &src[word_start]);

           char **tmp = (char**)malloc((c->argc + 1)* sizeof(char*));
           for(int i = 0; i < c->argc; i++)
               tmp[i] = c->argv[i];

           if(c->argv)
               free(c->argv);

           c->argv = tmp;
           c->argv[c->argc] = new_word;
           c->argc++;

           while(src[pos] == ' ' || src[pos] == 0){
               pos++; 
               if(pos >= len) break mainloop;
           }
           word_start = pos;
           continue;
       }

       pos++;
    }

    if(in_quote){
        write(_err, _wlen("command syntax error: unclosed quote\n"));
        freeCommand(c);
        return 0;
    }

    // prepare for execvp
    char **tmp = (char**)malloc((c->argc + 1)* sizeof(char*));
    for(int i = 0; i < c->argc; i++)
        tmp[i] = c->argv[i];

    if(c->argv)
        free(c->argv);

    c->argv = tmp;
    c->argv[c->argc] = 0;
    c->path = c->argv[0];

    return c;
}

void freeCommand(ShellCommand *cmd) {
    if(!cmd) return;
    for(int i = 0; i < cmd->argc; i++)
        free(cmd->argv[i]);

    free(cmd);
}

// read line from aseqdump and update buffer
int readLine(Data *app, int len){
    int bytes = read(app->channel[_in], app->buffer, len);

    if(strncmp(app->buffer, "_exit", 5) == 0){
        write(_err, _wlen("could not find/start aseqdump\n"));
        return -1;
    }

    if(strstr(app->buffer, "Cannot connect") != 0){
        write(_err, _wlen("Could not connect to port\n"));
        return -1;
    }

    if(strstr(app->buffer, "Port unsubscribed") != 0){
        write(_err, _wlen("Lost connection to port %d\n"));
        return -1;
    }

    if(bytes == -1){
        write(_err, _wlen("read error\n"));
        return -1;
    }
    app->buffer[bytes] = 0;

    return 0;
}

// return the code of the key pressed (0 if no note)
int getNote(Data app){
    uint note = 0, port = 0;
    int n;

    n = sscanf(app.buffer, "%3u:", &port);
    if(n != 1 || port != app.port) return 0;

    const char *trigger = app.act_on_release ? N_OFF : N_ON;
    if(strstr(app.buffer, trigger) == NULL) return 0;

    char *note_pos = strstr(app.buffer, "note ");
    if(!note_pos) return 0;

    n = sscanf(note_pos, "note %3u,", &note);
    if(n != 1) return 0;

    return note;
}
