#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>
#include <stdlib.h>

#define OUT STDOUT_FILENO
#define IN STDIN_FILENO
#define PORT_DIGITS 4

const uint test_note = 21;
const char* test_cmd = "notify-send 'hello from pianoterm!'";
const char *N_OFF = "Note off";
const char *N_ON = "Note on";

// first argument - midi port (24)
// TODO: if no argument is passed try to find the port, using aconnect -i
// TODO: config to run command if (note on vs note off) (press/release)
// TODO: allow config to use standard notation and convert to key code (C#1 = "echo hello")
//
// alsactl (aseqdump) version 1.2.15.2
//

struct app_data_t {
    int channel[2];
    char buffer[256];
    uint port;
    bool act_on_release;
};

typedef struct app_data_t Data;

int getNote(Data data);

int main(int argc, char**argv) {
    Data data;
    data.port = 24;
    data.act_on_release = false;
    char target_port_str[PORT_DIGITS];

    if(argc == 2)
    {
        // if(strlen(argv[1]))
    }

    if(argc <= 1)
    {
        // try to find port using aconnect -i
    }

    if(pipe(data.channel) == -1)
    {
        fprintf(stderr, "pipe error\n");
        return 1;
    };

    int pid = fork();
    if(pid == -1)
    {
        fprintf(stderr, "fork error\n");
        return 1;
    }

    if(pid == 0)
    {
        close(data.channel[IN]);
        dup2(data.channel[OUT], OUT);

        execlp("aseqdump", "aseqdump", "-p", "24", 0);
        printf("_exit\n");
    } else {
        while(1)
        {
            int bytes = read(data.channel[IN], data.buffer, 256);

            if(strncmp(data.buffer, "_exit", 5) == 0)
            {
                fprintf(stderr, "could not find/start aseqdump\n");
                return 1;
            }
            if(bytes == -1)
            {
                fprintf(stderr, "read error\n");
                return 1;
            }
            data.buffer[bytes] = 0;

            int note = getNote(data);
            if(note)
            {  
                printf("Note pressed: %d\n", note); 
            }

        }
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }

    return 0;
}

// return the code of the key pressed (0 if no note)
int getNote(Data data)
{
    uint note, port;
    int n;

    n = sscanf(data.buffer, "%3u:", &port);
    if(n != 1 || port != data.port) return 0;

    const char *trigger = data.act_on_release ? N_OFF : N_ON;
    if(strstr(data.buffer, trigger) == NULL) return 0;

    char *note_pos = strstr(data.buffer, "note ");
    if(!note_pos) return 0;

    n = sscanf(note_pos, "note %3u,", &note);
    if(n != 1) return 0;

    return note;
}
