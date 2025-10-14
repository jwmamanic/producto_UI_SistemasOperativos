/*
* src/mini_shell.cpp
* Implementaci\u00f3n de una mini-shell POSIX/C++ que cumple los requisitos:
* - Prompt propio
* - Resoluci\u00f3n de rutas: rutas absolutas usadas tal cual; si no, se intenta /bin/<cmd>
* - fork() + exec*() en hijos; padre espera con wait/waitpid (foreground)
* - mensajes de error en espa\u00f1ol usando perror/errno
* - redirecci\u00f3n de salida '>' (truncar/crear)
* - comando 'salir' para terminar
* - extensiones: pipe simple (cmd1 | cmd2) y background '&' (no bloquear)
* - manejador de SIGINT en padre para no morir con Ctrl-C; hijos reciben señales normalmente
* - instrumentaci\u00f3n b\u00e1sica de memoria (contadores de malloc/free usados por la shell)
*
* Compilar: g++ -std=c++17 -pthread src/mini_shell.cpp -o mini_shell
*/


#include <bits/stdc++.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>


#include "shell.h"
#include "parser.h"
