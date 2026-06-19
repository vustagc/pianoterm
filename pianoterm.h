#include <alsa/asoundlib.h>

#define _conf_path "/.config/pianoterm/config"
#define _retry_connection_secs 60
#define _on_hold_repeat_delay_ms 100
#define _port_digits 4

#define _aseq_log_len 78
#define _out STDOUT_FILENO
#define _in STDIN_FILENO
#define _err STDERR_FILENO
#define _wlen(str) str, strlen(str)
#define _wsize(str) str, sizeof(str)
#define _wstart(c)                                                             \
  while (*(c) == ' ')                                                          \
    c++;
#define _wend(c)                                                               \
  while (!(*c == ' ' || *c == 0 || *c == '#'))                                 \
    c++;
#define _cmdend(c)                                                             \
  while (!(*c == 0 || *c == '#'))                                              \
    c++;

const char *N_OFF = "Note off";
const char *N_ON = "Note on";
const char *C_CH = "Control change";

typedef enum {
  on_press,
  on_release,
  on_hold,
} Trigger;

typedef enum {
  e_note,
  e_controller,
} EventType;

typedef enum {
  f_port = 'p',
  f_name = 'n',
  f_config = 'c',
} Flag;

typedef enum {
  re_retry = -2,
  re_exit = -1,
  re_ignore = 0,
  re_ok = 1,
} ReadError;

typedef struct {
  char *path;
  uint argc;
  char **argv;
} ShellCommand;

typedef struct user_command {
  uint midi_id; // note or controller
  EventType type;
  union {
    Trigger note_trigger;
    uint controller_value;
  };
  char *str;
  int pid;
} UserCommand;

typedef struct midi_event {
  uint id;
  EventType type;
  union {
    Trigger note_trigger;
    uint controller_value;
  };
} MidiEvent;

typedef struct app_data {
  char buffer[124];
  uint port;
  char *name;
  char *config;
  Trigger trigger_state;
  UserCommand *commands;
  uint n_commands;

  snd_seq_t *h_alsa;
  snd_seq_port_subscribe_t *sub_handle;
  int client_id; // alsa client 128 -> 192
} Data;

int parseArguments(Data *app, int argc, char **argv);
void runCommand(Data *app, MidiEvent e);
ShellCommand *parseCommand(char *src);
void freeCommand(ShellCommand *cmd);
int loadConfig(Data *app);
char *seekToNext(char *cur, char target);
void logCommands(Data app);
void waitForConnection(Data *app);
void clearChannel(Data *app);
int parseOption(Data *app, char flag, char *value);
void resetSubscription(Data *app);
void subscribeToClient(Data *app, int client_id, int client_port);
int tryFindClient(Data *app);
int initAlsa(Data *app);
