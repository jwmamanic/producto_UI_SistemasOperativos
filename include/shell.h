#ifndef SHELL_H
#define SHELL_H

#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <cstring>
#include <cstdlib>
#include <errno.h>

using namespace std;

// -------------------- Estructura de un comando --------------------
struct Command {
    vector<string> args;      // argumentos del comando (argv)
    string in_file;           // redirección de entrada (<)
    string out_file;          // redirección de salida (>, >>)
    bool append_out = false;  // si es >> en vez de >
};

// -------------------- Resultado del parser --------------------
struct ParsedLine {
    vector<Command> cmds;     // lista de comandos (por si hay pipes)
    bool background = false;  // true si hay "&"
};

// -------------------- Declaraciones de funciones --------------------
ParsedLine parse_line(const string &line);
int execute_single(Command &cmd, bool background, const string &orig_cmdline);
void print_prompt();
void sigint_handler(int sig);

#endif // SHELL_H

