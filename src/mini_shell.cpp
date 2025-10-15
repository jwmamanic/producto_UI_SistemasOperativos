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

using namespace std;


// Instrumentaci\u00f3n simple memoria
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


// helper para strdup usando shell_malloc
char* shell_strdup(const char* s) {
	if (!s) return nullptr;
	size_t n = strlen(s)+1;
	char* p = (char*)shell_malloc(n);
	if (!p) return nullptr;
	memcpy(p, s, n);
	return p;
}
// -------------------- Tokenizer simple (espacios separan tokens) --------------------
vector<string> tokenize(const string& s) {
	vector<string> out;
	istringstream iss(s);
	string t;
	while (iss >> t) out.push_back(t);
	return out;
}


// -------------------- Parser (soporta una tuberia max y redirecciones) --------------------
ParsedLine parse_line(const string& line) {
	ParsedLine pl;
	string ln = line;
	// detectar & al final
	size_t last = ln.find_last_not_of(" \t");
	if (last != string::npos && ln[last] == '&') {
	pl.background = true;
	ln = ln.substr(0, last);
	}


	// dividir por '|', solo una tuberia permitida (dos comandos)
	vector<string> parts;
	size_t pos = 0;
	while (true) {
		size_t p = ln.find('|', pos);
		if (p == string::npos) { parts.push_back(ln.substr(pos)); break; }
		parts.push_back(ln.substr(pos, p-pos));
		pos = p+1;
	}


	for (auto &seg : parts) {
		// trim
		size_t a = seg.find_first_not_of(" \t");
		if (a==string::npos) continue;
		size_t b = seg.find_last_not_of(" \t");
		string s = seg.substr(a, b-a+1);
		auto toks = tokenize(s);
		if (toks.empty()) continue;
		Command cmd;
		for (size_t i=0;i<toks.size();++i) {
			if (toks[i] == "<" && i+1 < toks.size()) {
				cmd.in_file = toks[++i];
			} else if (toks[i] == ">>" && i+1 < toks.size()) {
				cmd.out_file = toks[++i];
				cmd.append_out = true;
			} else if (toks[i] == ">" && i+1 < toks.size()) {
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
// -------------------- Util: construir argv con malloc (liberarlo luego) --------------------
char** make_argv(const vector<string>& args) {
	size_t n = args.size();
	char** argv = (char**)shell_malloc((n+1)*sizeof(char*));
	if (!argv) return nullptr;
	for (size_t i=0;i<n;i++) argv[i] = shell_strdup(args[i].c_str());
	argv[n] = nullptr;
	return argv;
}

void free_argv(char** argv) {
	if (!argv) return;
	for (size_t i=0; argv[i]; ++i) shell_free(argv[i]);
	shell_free(argv);
}

// -------------------- Resoluci\u00f3n de ruta del ejecutable --------------------
// Si cmd es absoluta (empieza por '/'), se usa tal cual. Si no, se intenta /bin/<cmd>
// retorna ruta completa en vector<char> (heap) o nullptr si no existe o no ejecutable
char* resolve_executable(const string& cmd) {
	if (cmd.empty()) return nullptr;
	if (cmd[0] == '/') {
		if (access(cmd.c_str(), X_OK) == 0) return shell_strdup(cmd.c_str());
		return nullptr; // no existe o no ejecutable
	}
	string candidate = string("/bin/") + cmd;
	if (access(candidate.c_str(), X_OK) == 0) return shell_strdup(candidate.c_str());
	return nullptr;
}

// -------------------- Background job tracking (simple, thread-safe) --------------------
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
	bgjobs.erase(remove_if(bgjobs.begin(), bgjobs.end(), [&](const BgJob& j){ return j.pid==pid; }), bgjobs.end());
	pthread_mutex_unlock(&bg_mtx);
}

void print_bgjobs() {
	pthread_mutex_lock(&bg_mtx);
	if (bgjobs.empty()) cout << "(no hay jobs en segundo plano)\n";
	else {
		cout << "Jobs en background:\n";
		for (auto &j: bgjobs) cout << " ["<< j.pid << "] " << j.cmd << "\n";
	}
	pthread_mutex_unlock(&bg_mtx);
}

// Reaper thread para reap background con waitpid(WNOHANG)
void* bg_reaper(void*) {
	while (true) {
		pthread_mutex_lock(&bg_mtx);
		bool any = !bgjobs.empty();
		pthread_mutex_unlock(&bg_mtx);
		if (!any) {
			usleep(200000);
			continue;
		}
		// revisar pids
		vector<pid_t> pids;
		pthread_mutex_lock(&bg_mtx);
		for (auto &j: bgjobs) pids.push_back(j.pid);
		pthread_mutex_unlock(&bg_mtx);

		for (pid_t pid : pids) {
			int status;
			pid_t r = waitpid(pid, &status, WNOHANG);
			if (r == -1) {
				// posible error
				remove_bgjob(pid);
			} else if (r > 0) {
				cout << "\n[bg] proceso "<< pid << " finalizo";
				if (WIFEXITED(status)) cout << " estado="<<WEXITSTATUS(status);
				if (WIFSIGNALED(status)) cout << " signal="<<WTERMSIG(status);
				cout << "\n";
				remove_bgjob(pid);
			}
		}
		usleep(200000);
	}
	return nullptr;
}

// -------------------- Señales: padre ignora SIGINT; hijos restauran default --------------------
struct sigaction old_sigint;


void setup_signal_handlers() {
	struct sigaction sa;
	sa.sa_handler = SIG_IGN; // ignorar Ctrl-C en el padre
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGINT, &sa, &old_sigint) == -1) perror("sigaction");
}

void restore_signal_handlers_parent() {
	sigaction(SIGINT, &old_sigint, nullptr);
}


// -------------------- Ejecutar un comando simple (sin pipe) --------------------
// maneja redireccion > y <, y background (si background==true, no espera)

int execute_single(Command &cmd, bool background, const string& orig_cmdline) {
    if (cmd.args.empty()) return 0;

    // built-ins
    if (cmd.args[0] == "salir") exit(0);
    else if (cmd.args[0] == "jobs") { print_bgjobs(); return 0; }
    else if (cmd.args[0] == "meminfo") {
        cout << "Allocaciones activas (aprox): " << shell_alloc_count() << "\n";
        return 0;
    }

    // resolver ejecutable
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
        // hijo: restaurar SIGINT
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGINT, &sa, nullptr);

        // redirecciones
        if (!cmd.in_file.empty()) {
            int fd = open(cmd.in_file.c_str(), O_RDONLY);
            if (fd == -1) { perror("abrir entrada"); _exit(127); }
            if (dup2(fd, STDIN_FILENO) == -1) { perror("dup2 in"); _exit(127); }
            close(fd);
        }
        if (!cmd.out_file.empty()) {
            int flags = O_WRONLY | O_CREAT | (cmd.append_out ? O_APPEND : O_TRUNC);
            int fd = open(cmd.out_file.c_str(), flags, 0644);
            if (fd == -1) { perror("abrir salida"); _exit(127); }
            if (dup2(fd, STDOUT_FILENO) == -1) { perror("dup2 out"); _exit(127); }
            close(fd);
        }

        // preparar argv
        char** argv = make_argv(cmd.args);

        // ejecutar
        execvp(exe_path, argv);
        perror("execvp");
        _exit(127);
    } else {
        shell_free(exe_path);
        if (!background) {
            waitpid(pid, nullptr, 0);
        } else {
            register_bgjobs(pid, orig_cmdline);
            cout << "[BG] proceso " << pid << " ejecutando: " << orig_cmdline << "\n";
        }
    }
    return 0;
}

