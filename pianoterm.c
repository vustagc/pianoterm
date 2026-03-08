#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

#define _conf_path "/.config/pianoterm/config"
#define _retry_connection_secs 60
#define _on_hold_repeat_delay 100000
#define _port_digits 4

#define UINT16_MAX 65535
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

// TODO: chord on_press support (store multiple notes in a cmd)
// define a limit (10 for 10 fingers)
// change user_cmd to store an array of notes (can be static, since only 10)
// read the next 10 lines, instead of one-by-one, check if 'note on' matches
// notes in cmd (for n lines in cmd) also figure out syntax for config file
// TODO: if no argument is passed try to find the port, using aconnect -i
// TODO: allow config to use standard notation and convert to key code (C#1 =
// "echo hello")
// TODO: option to pass in config file path
// TODO: option to reload config file?
// TODO: check if instance already running on the same port
//
// alsactl (aseqdump) version 1.2.15.2

enum trigger {
  on_press,
  on_release,
  on_hold,
};
typedef enum trigger Trigger;

enum event_type { e_note, e_controller };
typedef enum event_type EventType;

struct shell_command {
  char *path;
  uint argc;
  char **argv;
};
typedef struct shell_command ShellCommand;

struct user_command {
  uint midi_id; // note or controller
  EventType type;
  union {
    Trigger note_trigger;
    uint controller_value;
  };
  char *str;
  int pid;
};
typedef struct user_command UserCommand;

struct midi_event {
  uint id;
  EventType type;
  union {
    Trigger note_trigger;
    uint controller_value;
  };
};
typedef struct midi_event MidiEvent;

struct app_data {
  int channel[2];
  char buffer[124];
  uint port;
  char port_str[_port_digits];
  Trigger trigger_state;
  UserCommand *commands;
  uint n_commands;
};
typedef struct app_data Data;

int readLine(Data *app, int len);
MidiEvent getEvent(Data app);
void runCommand(Data *app, MidiEvent e);
ShellCommand *parseCommand(char *src);
void freeCommand(ShellCommand *cmd);
void loadConfig(Data *app);
char *seekToNext(char *cur, char target);
void logCommands(Data app);
void waitForConnection(Data *app);
int startAseqDump(Data *app, int last_pid);
void clearChannel(Data *app);

int main(int argc, char **argv) {
  Data app;
  app.trigger_state = on_press;
  app.port = 0;
  app.n_commands = 0;
  memset(app.buffer, 0, sizeof(app.buffer));

  if (argc >= 2) {
    long int port = strtol(argv[1], NULL, 10);
    if (port <= 0 || port >= UINT16_MAX) {
      write(_out, _wlen("Invalid port\n"));
      return 1;
    }
    app.port = (uint)port;
  }

  if (argc < 2) {
    // TODO: try to find port automatically using aconnect -i
    write(_out, _wlen("Usage: "));
    write(_out, _wlen(argv[0]));
    write(_out, _wlen(" <port>\n"));
    return 1;
  }

  snprintf(app.port_str, _port_digits, "%u", app.port);
  loadConfig(&app);
  // logCommands(app);

  if (pipe(app.channel) == -1) {
    write(_err, _wlen("pipe error\n"));
    return 1;
  };
  write(_out, _wlen("MIDI port "));
  write(_out, _wlen(app.port_str));
  write(_out, _wlen("\n"));

  int pid = -1;

  waitForConnection(&app);
  pid = startAseqDump(&app, pid);
  clearChannel(&app);

  while (1) {
    int ret = readLine(&app, _aseq_log_len);

    if (ret == -1) // error
      break;
    else if (ret == -2) { // reconnect
      waitForConnection(&app);
      pid = startAseqDump(&app, pid);
      clearChannel(&app);
      continue;
    } else if (ret == 0) // wrong format
      continue;

    MidiEvent event = getEvent(app);
    if (event.id == -1) {
      printf("Unexpected error\n");
      continue;
    }
    runCommand(&app, event);
  }

  kill(pid, SIGKILL);
  waitpid(pid, NULL, 0);

  return 0;
}

void loadConfig(Data *app) {
  const char *home = getenv("HOME");
  if (!home) {
    write(_err, _wlen("$HOME variable not set\n"));
    return;
  }

  char path[strlen(home) + strlen(_conf_path) + 1];
  snprintf(_wsize(path), "%s%s", home, _conf_path);

  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    return;
  }
  write(_out, _wlen("Loading config: "));
  write(_out, _wlen(path));
  write(_out, _wlen("\n"));

  char b;
  uint l_count = 0;
  while (read(fd, &b, 1) > 0) {
    if (b == '\n')
      l_count++;
  }
  lseek(fd, 0, SEEK_SET);

  char *lines[l_count];

  uint l_cur = 0;
  uint l_bytes = 0;
  uint pos = 0;
  while (read(fd, &b, 1) > 0) {
    pos++;
    l_bytes++;
    if (b != '\n')
      continue;

    // TODO: error handling
    lines[l_cur] = (char *)malloc(l_bytes * sizeof(char));
    lseek(fd, pos - l_bytes, SEEK_SET);
    for (int i = 0; i < l_bytes; i++) {
      read(fd, &b, 1);
      lines[l_cur][i] = b;
    }

    lines[l_cur][l_bytes - 1] = 0;
    l_bytes = 0;
    l_cur++;
  }

  // TODO: break this up into functions
  for (l_cur = 0; l_cur < l_count; l_cur++) {
    char *c = lines[l_cur];
    _wstart(c);
    if (*c == '#' || *c == 0)
      continue;

    // check for on_press/on_release keyword
    {
      char *w = c;
      _wend(w);
      int size = (int)(w - c);
      if (size <= 0)
        continue;
      char word[size + 1];
      w = c;
      for (int i = 0; i < size; i++)
        word[i] = *(w++);
      word[size] = 0;

      if (strcmp(word, "on_press") == 0) {
        app->trigger_state = on_press;
        continue;
      }
      if (strcmp(word, "on_release") == 0) {
        app->trigger_state = on_release;
        continue;
      }
      if (strcmp(word, "on_hold") == 0) {
        app->trigger_state = on_hold;
        continue;
      }
    }

    errno = 0;
    char *end = 0;
    long int midi_id = strtol(c, &end, 10);
    if (errno || end == c)
      continue;
    c = end;

    _wstart(c);

    bool is_controller = false;
    int controller_trigger_val = -1;
    if (*c == '(') {
      is_controller = true;
      char *val_end = seekToNext(c, ')');
      if (*val_end == 0) {
        write(_err, _wlen("Syntax error: Unclosed '('\n"));
        continue;
      }
      c++;

      end = 0;
      errno = 0;
      long int num = strtol(c, &end, 10);
      if (errno || end == c || num < 0 || num > UINT16_MAX) {
        write(_err, _wlen("Error: Invalid controller value\n"));
        continue;
      }
      controller_trigger_val = num;

      c = val_end;
      c++;
      _wstart(c);
    }

    if (*c != '=')
      continue;

    c++;
    _wstart(c);
    if (*c == 0)
      continue;

    char *w = c;
    _cmdend(w);

    int cmd_len = (int)(w - c);
    if (cmd_len <= 0)
      continue;

    if (app->n_commands == 0)
      app->commands = (UserCommand *)malloc(sizeof(UserCommand));
    else
      app->commands = (UserCommand *)realloc(
          app->commands, sizeof(UserCommand) * (app->n_commands + 1));

    app->commands[app->n_commands].str =
        (char *)malloc(sizeof(char) * (cmd_len + 1));
    for (int i = 0; i < cmd_len; i++)
      app->commands[app->n_commands].str[i] = *(c++);

    app->commands[app->n_commands].str[cmd_len] = 0;
    app->commands[app->n_commands].midi_id = midi_id;
    app->commands[app->n_commands].pid = -1;

    if (is_controller) {
      app->commands[app->n_commands].type = e_controller;
      app->commands[app->n_commands].controller_value = controller_trigger_val;
    } else {
      app->commands[app->n_commands].type = e_note;
      app->commands[app->n_commands].note_trigger = app->trigger_state;
    }

    app->n_commands++;
  }

  for (l_cur = 0; l_cur < l_count; l_cur++)
    free(lines[l_cur]);

  close(fd);
}

void runCommand(Data *app, MidiEvent e) {
  for (int i = 0; i < app->n_commands; i++) {
    if (app->commands[i].midi_id != e.id)
      continue;

    ShellCommand *c = parseCommand(app->commands[i].str);
    if (!c)
      continue;

    switch (e.type) {
    case e_note:
      Trigger t = app->commands[i].note_trigger;
      if (t == e.note_trigger && t == on_press || t == on_release) {
        int pid = fork();
        if (pid == -1) {
          continue;
        }

        if (pid == 0) {
          execvp(c->path, c->argv);
          exit(0);
        } else {
          waitpid(pid, 0, 0);
        }

      } else if (t == on_hold) {
        if (e.note_trigger == on_press) {
          if (app->commands[i].pid > 0) {
            kill(app->commands[i].pid, SIGKILL);
            waitpid(app->commands[i].pid, NULL, 0);
          }

          int pid = fork();
          if (pid == 0) { // repeating process
            // this should prob be a thread instead
            while (true) {
              int c_pid = fork();
              if (c_pid) {
                execvp(c->path, c->argv);
                exit(0);
              } else {
                waitpid(c_pid, 0, 0);
              }
              usleep(_on_hold_repeat_delay);
            }
          }
          app->commands[i].pid = pid;
        }
        if (e.note_trigger == on_release) {
          if (app->commands[i].pid > 0) {
            kill(app->commands[i].pid, SIGKILL);
            waitpid(app->commands[i].pid, NULL, 0);
          }
          app->commands[i].pid = -1;
        }
      }

      break;
    case e_controller:
      if (e.controller_value == app->commands[i].controller_value) {
        if (fork() == 0) {
          execvp(c->path, c->argv);
          exit(0);
        }
      }

      break;
    }

    freeCommand(c);
  }
}

// separate cmd string into multiple args
ShellCommand *parseCommand(char *src) {
  ShellCommand *c = (ShellCommand *)malloc(sizeof(ShellCommand));
  c->argv = 0;
  c->path = 0;

  char *cur = src;
  while (*cur != 0) {
    _wstart(cur);
    const char *start = cur;
    if (*start == '\"' || *start == '\'') {
      cur++;
      cur = seekToNext(cur, *start);
      if (*cur == 0)
        goto err_unclosed;
    }
    _wend(cur);

    int len = (int)(cur - start);
    if (c->argc == 0)
      c->argv = malloc(sizeof(char *));
    else
      c->argv = realloc(c->argv, sizeof(char *) * (c->argc + 1));

    c->argv[c->argc] = malloc(sizeof(char) * (len + 1));
    snprintf(c->argv[c->argc], len + 1, "%s", start);
    c->argv[c->argc][len] = 0;
    c->argc++;
  }
  if (c->argc == 0)
    return 0;

  // prepare for execvp
  c->argv = realloc(c->argv, (c->argc + 1) * sizeof(char *));
  c->argv[c->argc] = 0;
  c->path = c->argv[0];

  return c;

err_unclosed:
  write(_err, _wlen("command syntax error: unclosed quote\n"));
  freeCommand(c);
  return 0;
}

void freeCommand(ShellCommand *cmd) {
  if (!cmd)
    return;
  for (int i = 0; i < cmd->argc; i++)
    free(cmd->argv[i]);

  free(cmd);
}

// read line from aseqdump and update buffer
// return values:
// (-2) should reconnect
// (-1) error
// (0) ignore, wrong format
// (1) ok, expected format
int readLine(Data *app, int len) {
  int bytes = read(app->channel[_in], app->buffer, len);
  if (bytes == -1) {
    write(_err, _wlen("read error\n"));
    return -1;
  }
  app->buffer[bytes] = 0;

  if (strncmp(app->buffer, "_exit", 5) == 0) {
    write(_err, _wlen("could not find/start aseqdump\n"));
    return -1;
  }

  if (strstr(app->buffer, "Cannot connect") != 0) {
    // write(_err, _wlen("Could not connect to port\n"));
    return -2;
  }

  if (strstr(app->buffer, "Port unsubscribed") != 0) {
    write(_err, _wlen("Lost connection to port\n"));
    return -2;
  }

  uint port = 0;
  int n = sscanf(app->buffer, "%3u:", &port);
  if (!(n == 1 && port == app->port))
    return 0;

  return 1;
}

MidiEvent getEvent(Data app) {
  MidiEvent e;

  if (strstr(app.buffer, C_CH)) {
    e.type = e_controller;
  } else if (strstr(app.buffer, N_ON)) {
    e.type = e_note;
    e.note_trigger = on_press;
  } else if (strstr(app.buffer, N_OFF)) {
    e.type = e_note;
    e.note_trigger = on_release;
  } else
    goto err_unexpected_format;

  switch (e.type) {
  case e_note:
    char *note_pos = strstr(app.buffer, "note ");
    if (!note_pos)
      goto err_unexpected_format;
    if (sscanf(note_pos, "note %3u,", &e.id) != 1)
      goto err_unexpected_format;
    break;
  case e_controller:
    char *controller_pos = strstr(app.buffer, "controller ");
    if (!controller_pos)
      goto err_unexpected_format;
    if (sscanf(controller_pos, "controller %3u,", &e.id) != 1)
      goto err_unexpected_format;

    char *value_pos = strstr(app.buffer, "value ");
    if (sscanf(value_pos, "value %3u", &e.controller_value) != 1)
      goto err_unexpected_format;
  }

  return e;

// should not trigger, unless aseqdump format changes
err_unexpected_format:
  e.id = -1;
  return e;
}

// can I turn this into a macro?
char *seekToNext(char *cur, char target) {
  while (!(*cur == target || *cur == 0))
    cur++;
  return cur;
};

void logCommands(Data app) {
  printf("Commands: \n");
  for (int i = 0; i < app.n_commands; i++) {
    UserCommand c = app.commands[i];
    printf("%d ", c.midi_id);

    if (c.type == e_controller) {
      printf("(%d) ", c.controller_value);
    } else {
      if (c.note_trigger == on_press) {
        printf("(on_press) ");
      } else {
        printf("(on_release) ");
      }
    }

    printf("%s\n", c.str);
  }
}

void waitForConnection(Data *app) {
  write(_out, _wlen("Trying to connect...\n"));
  int tmp_chan[2];
  char haystack[1024];
  char needle[24];
  sprintf(needle, "client %u:", app->port);

  if (pipe(tmp_chan) == -1) {
    write(_err, _wlen("Pipe error\n"));
    exit(1);
  }

  while (true) {
    int pid = fork();
    if (pid == -1) {
      write(_err, _wlen("fork error\n"));
      exit(1);
    }

    if (pid == 0) {
      close(tmp_chan[_in]);
      dup2(tmp_chan[_out], _out);
      dup2(tmp_chan[_out], _err);

      execlp("aconnect", "aconnect", "-i", 0);
      exit(0);
    } else {
      int bytes = read(tmp_chan[_in], haystack, sizeof(haystack) - 1);
      if (bytes == -1) {
        write(_err, _wlen("acconnect read error\n"));
        exit(1);
      }
      haystack[bytes] = 0;
      waitpid(pid, 0, 0);

      if (strstr(haystack, needle)) {
        write(_out, _wlen("Connected to port "));
        write(_out, _wlen(app->port_str));
        write(_out, _wlen("\n"));
        close(tmp_chan[_in]);
        return;
      }
      sleep(_retry_connection_secs);
    }
  }
}

// ignores any events sent before the app started
void clearChannel(Data *app) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(app->channel[_in], &fds);

  struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
  int leftover_msgs = select(app->channel[_in] + 1, &fds, 0, 0, &timeout);
  while (leftover_msgs) {
    readLine(app, _aseq_log_len);
    leftover_msgs = select(app->channel[_in] + 1, &fds, 0, 0, &timeout);
  }
}

int startAseqDump(Data *app, int last_pid) {
  if (last_pid != -1) {
    kill(last_pid, SIGKILL);
    waitpid(last_pid, NULL, 0);
  }

  int pid = fork();

  if (pid == -1) {
    write(_err, _wlen("fork error\n"));
    exit(1);
  }
  if (pid == 0) {
    close(app->channel[_in]);
    dup2(app->channel[_out], _out);
    dup2(app->channel[_out], _err);

    execlp("aseqdump", "aseqdump", "-p", app->port_str, 0);
    write(_out, _wlen("_exit\n"));
    exit(0);
  }

  return pid;
}
