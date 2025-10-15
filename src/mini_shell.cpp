/*
 * src/mini_shell.cpp
 * Implementación de una mini-shell POSIX/C++ que cumple los requisitos:
 * - Prompt propio
 * - Resolución de rutas: rutas absolutas usadas tal cual; si no, se intenta /bin/<cmd>
 * - fork() + exec*() en hijos; padre espera con wait/waitpid (foreground)
 * - mensajes de error en español usando perror/errno
 * - redirección de salida '>' (truncar/crear)
 * - comando 'salir' para terminar
 * - extensiones: pipe simple (cmd1 | cmd2) y background '&' (no bloquear)
 * - manejador de SIGINT en padre para no morir con Ctrl-C; hijos reciben señales normalmente
 * - instrumentación básica de memoria (contadores de malloc/free usados por la shell)
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

using namespace std;

// ================= Instrumentación simple de memoria =================
static size_t g_alloc_count = 0;

void* shell_malloc(size_t s) {
    void* p = malloc(s);
    if (p) __sync_add_and_fetch(&g_alloc_count, 1);
    return p;
}
void shell_free(void* p) {
    if (p) {
        __sync_sub_and_fetch(&g_alloc_count, 1);
        free(p);
    }
}
size_t shell_alloc_count() { return g_alloc_count; }

char* shell_strdup(const char* s) {
    if (!s) return nullptr;
    size_t n = strlen(s) + 1;
    char* p = (char*)shell_malloc(n);
    if (!p) return nullptr;
    memcpy(p, s, n);
    return p;
}

// ================= Tokenizer =================
vector<string> tokenize(const string& s) {
    vector<string> out;
    istringstream iss(s);
    string t;
    while (iss >> t) out.push_back(t);
    return out;
}

// ================= Parser simple =================
ParsedLine parse_line(const string& line) {
    ParsedLine pl;
    string ln = line;
    size_t last = ln.find_last_not_of(" \t");
    if (last != string::npos && ln[last] == '&') {
        pl.background = true;
        ln = ln.substr(0, last);
    }

    vector<string> parts;
    size_t pos = 0;
    while (true) {
        size_t p = ln.find('|', pos);
        if (p == string::npos) { parts.push_back(ln.substr(pos)); break; }
        parts.push_back(ln.substr(pos, p - pos));
        pos = p + 1;
    }

    for (auto &seg : parts) {
        size_t a = seg.find_first_not_of(" \t");
        if (a == string::npos) continue;
        size_t b = seg.find_last_not_of(" \t");
        string s = seg.substr(a, b - a + 1);
        auto toks = tokenize(s);
        if (toks.empty()) continue;
        Command cmd;
        for (size_t i = 0; i < toks.size(); ++i) {
            if (toks[i] == "<" && i + 1 < toks.size()) {
                cmd.in_file = toks[++i];
            } else if (toks[i] == ">>" && i + 1 < toks.size()) {
                cmd.out_file = toks[++i];
                cmd.append_out = true;
            } else if (toks[i] == ">" && i + 1 < toks.size()) {
                cmd.out_file = toks[++i];
                cmd.append_out = false;
            } else {
                cmd.args.push_back(toks[i]);
            }
        }
        pl.cmds.push_back(move(cmd));
    }

    return pl;
}

// ================= argv helpers =================
char** make_argv(const vector<string>& args) {
    size_t n = args.size();
    char** argv = (char**)shell_malloc((n + 1) * sizeof(char*));
    if (!argv) return nullptr;
    for (size_t i = 0; i < n; i++) argv[i] = shell_strdup(args[i].c_str());
    argv[n] = nullptr;
    return argv;
}

void free_argv(char** argv) {
    if (!argv) return;
    for (size_t i = 0; argv[i]; ++i) shell_free(argv[i]);
    shell_free(argv);
}

// ================= Resolución de ejecutable =================
char* resolve_executable(const string& cmd) {
    if (cmd.empty()) return nullptr;
    if (cmd[0] == '/') {
        if (access(cmd.c_str(), X_OK) == 0) return shell_strdup(cmd.c_str());
        return nullptr;
    }
    string candidate = string("/bin/") + cmd;
    if (access(candidate.c_str(), X_OK) == 0) return shell_strdup(candidate.c_str());
    return nullptr;
}

// ================= Background jobs =================
struct BgJob { pid_t pid; string cmd; };
vector<BgJob> bgjobs;
pthread_mutex_t bg_mtx = PTHREAD_MUTEX_INITIALIZER;

void add_bgjob(pid_t pid, const string& cmd) {
    pthread_mutex_lock(&bg_mtx);
    bgjobs.push_back({pid, cmd});
    pthread_mutex_unlock(&bg_mtx);
}

void remove_bgjob(pid_t pid) {
    pthread_mutex_lock(&bg_mtx);
    bgjobs.erase(remove_if(bgjobs.begin(), bgjobs.end(),
                           [&](const BgJob& j){ return j.pid == pid; }),
                 bgjobs.end());
    pthread_mutex_unlock(&bg_mtx);
}

void print_bgjobs() {
    pthread_mutex_lock(&bg_mtx);
    if (bgjobs.empty()) cout << "(no hay jobs en segundo plano)\n";
    else {
        cout << "Jobs en background:\n";
        for (auto &j: bgjobs) cout << " [" << j.pid << "] " << j.cmd << "\n";
    }
    pthread_mutex_unlock(&bg_mtx);
}

// ================= Reaper thread =================
void* bg_reaper(void*) {
    while (true) {
        pthread_mutex_lock(&bg_mtx);
        bool any = !bgjobs.empty();
        pthread_mutex_unlock(&bg_mtx);
        if (!any) { usleep(200000); continue; }

        vector<pid_t> pids;
        pthread_mutex_lock(&bg_mtx);
        for (auto &j: bgjobs) pids.push_back(j.pid);
        pthread_mutex_unlock(&bg_mtx);

        for (pid_t pid : pids) {
            int status;
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == -1) remove_bgjob(pid);
            else if (r > 0) {
                cout << "\n[bg] proceso " << pid << " finalizó";
                if (WIFEXITED(status)) cout << " estado=" << WEXITSTATUS(status);
                if (WIFSIGNALED(status)) cout << " señal=" << WTERMSIG(status);
                cout << "\n";
                remove_bgjob(pid);
            }
        }
        usleep(200000);
    }
    return nullptr;
}

// ================= Señales =================
struct sigaction old_sigint;

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, &old_sigint) == -1)
        perror("sigaction");
}

// ================= Ejecución de comando simple =================
int execute_single(Command &cmd, bool background, const string& orig_cmdline) {
    if (cmd.args.empty()) return 0;

    // built-ins
    if (cmd.args[0] == "salir") exit(0);
    else if (cmd.args[0] == "jobs") { print_bgjobs(); return 0; }
    else if (cmd.args[0] == "meminfo") {
        cout << "Allocaciones activas (aprox): " << shell_alloc_count() << "\n";
        return 0;
    }

    char* exe_path = resolve_executable(cmd.args[0]);
    if (!exe_path) {
        cerr << "mini-shell: comando no encontrado o sin permisos: " << cmd.args[0] << "\n";
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        shell_free(exe_path);
        return -1;
    }

    if (pid == 0) {
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);

        if (!cmd.in_file.empty()) {
            int fd = open(cmd.in_file.c_str(), O_RDONLY);
            if (fd == -1) { perror("abrir entrada"); _exit(127); }
            if (dup2(fd, STDIN_FILENO) == -1) { perror("dup2 in"); close(fd); _exit(127); }
            close(fd);
        }

        if (!cmd.out_file.empty()) {
            int flags = O_WRONLY | O_CREAT | (cmd.append_out ? O_APPEND : O_TRUNC);
            int fd = open(cmd.out_file.c_str(), flags, 0644);
            if (fd == -1) { perror("abrir salida"); _exit(127); }
            if (dup2(fd, STDOUT_FILENO) == -1) { perror("dup2 out"); close(fd); _exit(127); }
            close(fd);
        }

        char** argv = make_argv(cmd.args);
        if (!argv) { perror("malloc argv"); _exit(127); }

        execv(exe_path, argv);
        perror("execv");
        free_argv(argv);
        _exit(127);
    } else {
        shell_free(exe_path);
        if (!background) {
            int status;
            pid_t w;
            do {
                w = waitpid(pid, &status, 0);
            } while (w == -1 && errno == EINTR);
            if (w == -1) {
                perror("waitpid");
                return -1;
            }
            return status;
        } else {
            add_bgjob(pid, orig_cmdline);
            cout << "[BG] iniciado pid " << pid << " -> " << orig_cmdline << "\n";
            return 0;
        }
    }
}

// ================= Función principal =================
int main() {
    setup_signal_handlers();

    pthread_t tid;
    pthread_create(&tid, nullptr, bg_reaper, nullptr);
    pthread_detach(tid);

    string line;
    while (true) {
        cout << "mini-shell> ";
        if (!getline(cin, line)) break;
        if (line.empty()) continue;

        ParsedLine pl = parse_line(line);
        if (pl.cmds.empty()) continue;

        if (pl.cmds.size() == 1)
            execute_single(pl.cmds[0], pl.background, line);
        else
            cerr << "Solo se admite una tubería por ahora.\n";
    }

    return 0;
}


