#include <alsa/asoundlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

#include "pianoterm.h"

int main(int argc, char **argv) {
  Data app;
  memset(&app, 0, sizeof(app));

  if (parseArguments(&app, argc, argv) == -1) {
    write(_err, _wlen("Error parsing cli arguments\n"));
    return 1;
  }
  if (loadConfig(&app) == -1) {
    write(_err, _wlen("Error loading config\n"));
    return 1;
  }
  if (initAlsa(&app) == -1) {
    write(_err, _wlen("Error initializing alsa connection\n"));
    return 1;
  }

  waitForConnection(&app);
  clearChannel(&app);
  while (1) {
    snd_seq_event_t *seq_evt = NULL;
    snd_seq_event_input(app.h_alsa, &seq_evt);

    MidiEvent e;
    switch (seq_evt->type) {
    case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
      resetSubscription(&app);
      waitForConnection(&app);
      clearChannel(&app);
      continue;

    case SND_SEQ_EVENT_NOTEON:
      e.type = e_note;
      e.note_trigger = on_press;
      e.id = seq_evt->data.note.note;
      break;
    case SND_SEQ_EVENT_NOTEOFF:
      e.type = e_note;
      e.note_trigger = on_release;
      e.id = seq_evt->data.note.note;
      break;
    case SND_SEQ_EVENT_CONTROLLER:
      e.type = e_controller;
      e.id = seq_evt->data.control.param;
      e.controller_value = seq_evt->data.control.value;
      break;

    default:
      continue;
    }

    runCommand(&app, e);
  }

  return 0;
}

// return value:
//  0 on success
// -1 on error
int parseArguments(Data *app, int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: %s [-p <port> | -n <name>] [-c <config>]\n", argv[0]);
    return -1;
  }

  // assume port if no flags
  if (argc == 2) {
    parseOption(app, f_port, argv[1]);
  } else {
    // process flags
    for (int i = 1; i < argc; i += 2) {
      if (strlen(argv[i]) < 2 || argv[i][0] != '-') {
        printf("Unknown flag: %s", argv[i]);
        return -1;
      }

      if (i + 1 >= argc) {
        printf("No value for flag: %s", argv[i]);
        return -1;
      }

      if (parseOption(app, argv[i][1], argv[i + 1]) == -1) {
        printf("Could not parse flag: %s", argv[i]);
        return -1;
      }
    }
  }

  if (app->port == 0 && app->name == NULL) {
    printf("You must provide either the port or the name of the channel\n");
    return -1;
  }

  return 0;
}

// return value:
//  0 on success
// -1 on error
int initAlsa(Data *app) {
  if (snd_seq_open(&app->h_alsa, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
    write_err("error opening alsa sequencer\n");
    return -1;
  }
  snd_seq_set_client_name(app->h_alsa, "Pianoterm");
  app->client_id = snd_seq_client_id(app->h_alsa);
  if (app->client_id <= 0) {
    write_err("bad client id\n");
    return -1;
  }
  snd_seq_port_subscribe_malloc(&app->sub_handle);

  // open a port
  snd_seq_port_info_t *p_info = NULL;
  snd_seq_port_info_malloc(&p_info);
  snd_seq_port_info_set_capability(p_info, SND_SEQ_PORT_CAP_WRITE |
                                               SND_SEQ_PORT_CAP_SUBS_WRITE);
  snd_seq_port_info_set_type(p_info, SND_SEQ_PORT_TYPE_APPLICATION |
                                         SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                                         SND_SEQ_PORT_TYPE_MIDI_GM);
  snd_seq_port_info_set_midi_channels(p_info, 16); // prob dont need all 16
  snd_seq_port_info_set_port_specified(p_info, 0);
  snd_seq_port_info_set_port(p_info, 0);
  snd_seq_port_info_set_name(p_info, "input");

  if (snd_seq_create_port(app->h_alsa, p_info) < 0) {
    write_err("error creating alsa port");
    snd_seq_port_info_free(p_info);
    return -1;
  }

  snd_seq_port_info_free(p_info);
  return 0;
}

int loadConfig(Data *app) {
  const char *home = getenv("HOME");
  if (!home) {
    write(_err, _wlen("$HOME variable not set\n"));
    return -1;
  }

  if (!app->config) {
    const int bytes = sizeof(char) * (strlen(home) + strlen(_conf_path) + 1);
    app->config = (char *)malloc(bytes);
    snprintf(app->config, bytes, "%s%s", home, _conf_path);
  }

  // rdwr so it fails if file is a directory, even though we don't write to it
  int fd = open(app->config, O_RDWR);
  if (fd == -1) {
    write(_err, _wlen("Could not open config file\n"));
    return -1;
  }
  printf("Loading config: %s\n", app->config);

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

  return 0;
}

int parseOption(Data *app, char flag, char *value) {
  if (!value)
    return -1;

  switch (flag) {
  case f_port:
    long int port = strtol(value, NULL, 10);
    if (port <= 0 || port >= UINT16_MAX) {
      printf("Invalid port\n");
      return -1;
    }
    app->port = (uint)port;
    break;

  case f_name:
    app->port = 0;
    app->name = value;
    break;

  case f_config:
    app->config = value;
    break;

  default:
    printf("Uknown flag\n");
    return -1;
  }

  return 0;
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
      if (t == e.note_trigger &&
          (e.note_trigger == on_press || e.note_trigger == on_release)) {
        int pid = fork();
        if (pid == -1) {
          write(_err, _wlen("Fork error\n"));
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
          int pid = fork();
          if (pid == 0) {
            // this should prob be a thread instead
            while (true) {
              int c_pid = fork();
              if (c_pid == -1)
                break;
              if (c_pid == 0) {
                execvp(c->path, c->argv);
                exit(0);
              } else {
                waitpid(c_pid, 0, 0);
              }
              usleep(_on_hold_repeat_delay_ms * 1000);
            }
            exit(0);
          }
          app->commands[i].pid = pid;
        }

        if (e.note_trigger == on_release) {
          if (app->commands[i].pid > 0) {
            kill(app->commands[i].pid, SIGKILL);
            waitpid(app->commands[i].pid, 0, 0);
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

// returns the client id by app->[port|name]
// returns -1 if client is not found
int tryFindClient(Data *app) {
  int found = -1;
  snd_seq_client_info_t *c_info = NULL;
  snd_seq_client_info_malloc(&c_info);

  snd_seq_client_info_set_client(c_info, -1);
  while (snd_seq_query_next_client(app->h_alsa, c_info) >= 0) {
    int cli_id = snd_seq_client_info_get_client(c_info);
    const char *cli_name = snd_seq_client_info_get_name(c_info);

    if (app->name) {
      if (strstr(cli_name, app->name)) {
        found = cli_id;
        break;
      }
    } else {
      if (cli_id == app->port) {
        found = cli_id;
        break;
      }
    }
  }

  snd_seq_client_info_free(c_info);
  return found;
}

void subscribeToClient(Data *app, int client_id, int client_port) {
  snd_seq_addr_t dest;
  dest.client = app->client_id;
  dest.port = 0;

  snd_seq_addr_t sender;
  sender.client = client_id;
  sender.port = client_port;

  snd_seq_port_subscribe_set_sender(app->sub_handle, &sender);
  snd_seq_port_subscribe_set_dest(app->sub_handle, &dest);

  if (snd_seq_subscribe_port(app->h_alsa, app->sub_handle) < 0) {
    printf("Could not subscribe port\n"); // TODO: handle this, return an error
  }
}

void resetSubscription(Data *app) {
  snd_seq_unsubscribe_port(app->h_alsa, app->sub_handle);
}

void waitForConnection(Data *app) {
  int client_id = tryFindClient(app);

  if (client_id > 0) {
    subscribeToClient(app, client_id, 0); // TODO: dont hardcode port 0
    printf("Connection established\n");
    return;
  }
  printf("Could not find device the device disconnected, waiting for "
         "connection...\n");

  subscribeToClient(app, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  while (1) {
    snd_seq_event_t *seq_evt = NULL;
    snd_seq_event_input(app->h_alsa, &seq_evt);

    if (seq_evt->type != SND_SEQ_EVENT_PORT_START) {
      continue;
    }

    client_id = tryFindClient(app);
    if (client_id <= 0) {
      continue;
    }

    resetSubscription(app);
    subscribeToClient(app, client_id, 0); // dont hardcode port
    printf("Connection established\n");
    break;
  }
}

// ignores any events sent before the app started
void clearChannel(Data *app) {
  usleep(10000);
  snd_seq_drop_input(app->h_alsa);
}
