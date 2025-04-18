#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>
#include <limits.h>
#include <pwd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64
#define HISTORY_SIZE 100

// ANSI-цвета
#define ORANGE "\033[38;5;208m"
#define BLUE "\033[34m"
#define CYAN "\033[36m"
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

typedef struct {
    char *items[HISTORY_SIZE];
    int count;
    int position;
} History;

History cmd_history = {0};
char cwd[PATH_MAX];
struct termios orig_termios;
char current_input[MAX_INPUT];
int input_pos = 0;

// ======================== путь корнего каталога ========================

char *resolve_path(const char *path) {
    static char resolved[PATH_MAX];
    
    if (path == NULL) return NULL;
    
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = getpwuid(getuid())->pw_dir;
        
        if (path[1] == '/' || path[1] == '\0') {
            snprintf(resolved, sizeof(resolved), "%s%s", home, path + 1);
        } else {
            return NULL;
        }
    } 
    else if (path[0] != '/') {
        if (getcwd(resolved, sizeof(resolved)) == NULL) {
            return NULL;
        }
        strncat(resolved, "/", sizeof(resolved) - strlen(resolved) - 1);
        strncat(resolved, path, sizeof(resolved) - strlen(resolved) - 1);
    }
    else {
        strncpy(resolved, path, sizeof(resolved));
    }
    
    char *p;
    while ((p = strstr(resolved, "/./"))) memmove(p, p+2, strlen(p+2)+1);
    while ((p = strstr(resolved, "//"))) memmove(p, p+1, strlen(p+1)+1);
    
    return resolved;
}

// ======================== Функции терминала ========================

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void clear_line() {
    printf("\r\033[K");
    fflush(stdout);
}

// ======================== История команд ========================

void history_add(const char *cmd) {
    if (cmd == NULL || strlen(cmd) == 0) return;
    
    if (cmd_history.count > 0 && 
        strcmp(cmd_history.items[cmd_history.count-1], cmd) == 0) {
        return;
    }
    
    if (cmd_history.count < HISTORY_SIZE) {
        cmd_history.items[cmd_history.count++] = strdup(cmd);
    } else {
        free(cmd_history.items[0]);
        memmove(cmd_history.items, cmd_history.items + 1, 
               (HISTORY_SIZE - 1) * sizeof(char*));
        cmd_history.items[HISTORY_SIZE - 1] = strdup(cmd);
    }
    cmd_history.position = cmd_history.count;
}

// ======================== Режим ввода ========================

void handle_arrow_up() {
    if (cmd_history.position > 0) {
        cmd_history.position--;
        clear_line();
        print_prompt();
        strncpy(current_input, cmd_history.items[cmd_history.position], MAX_INPUT);
        input_pos = strlen(current_input);
        printf("%s", current_input);
    }
}

void handle_arrow_down() {
    if (cmd_history.position < cmd_history.count - 1) {
        cmd_history.position++;
        clear_line();
        print_prompt();
        strncpy(current_input, cmd_history.items[cmd_history.position], MAX_INPUT);
        input_pos = strlen(current_input);
        printf("%s", current_input);
    } else if (cmd_history.position == cmd_history.count - 1) {
        cmd_history.position++;
        clear_line();
        print_prompt();
        current_input[0] = '\0';
        input_pos = 0;
    }
}

char *read_input() {
    current_input[0] = '\0';
    input_pos = 0;
    
    while (1) {
        char c = getchar();
        
        if (c == '\n') {
            putchar('\n');
            return strdup(current_input);
        }
        else if (c == '\x1b') {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
            
            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A': handle_arrow_up(); break;
                    case 'B': handle_arrow_down(); break;
                }
            }
        }
        else if (c == 127 || c == '\b') {
            if (input_pos > 0) {
                current_input[--input_pos] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
        }
        else if (isprint(c)) {
            if (input_pos < MAX_INPUT - 1) {
                current_input[input_pos++] = c;
                current_input[input_pos] = '\0';
                putchar(c);
                fflush(stdout);
            }
        }
    }
}

// ======================== Обновление оболочки ========================

void update_cwd() {
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, RED "dns: getcwd: %s\n" RESET, strerror(errno));
        strcpy(cwd, "???");
    }
}

void print_banner() {
    printf(ORANGE "\n");
    printf("  ██████  ███    ██  ███████ ██   ██ ███████ ██      ██      \n");
    printf("  ██   ██ ████   ██  ██      ██   ██ ██      ██      ██      \n");
    printf("  ██   ██ ██ ██  ██  ███████ ███████ █████   ██      ██      \n");
    printf("  ██   ██ ██  ██ ██       ██ ██   ██ ██      ██      ██      \n");
    printf("  ██████  ██   ████  ███████ ██   ██ ███████ ███████ ███████ \n");
    printf("\n");
    printf(" Delusional Nonsense Shell\n");
    printf(" Version: 1.0 BETA | Разработчик Markusssss\n");
    printf(" Ссылка на оболочку: https://github.com/Markussssssu/DNShell\n");
    // Адаптивная граница
    printf("\n");
    for (int i = 0; i < 60; i++) printf("▀");
    printf(RESET "\n\n");
}

void print_prompt() {
    char hostname[256] = "unknown";
    char username[256] = "unknown";
    
    struct passwd *pw = getpwuid(getuid());
    if (pw) strncpy(username, pw->pw_name, sizeof(username));
    
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        char *dot = strchr(hostname, '.');
        if (dot) *dot = '\0';
    }

    char short_path[PATH_MAX];
    const char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(short_path, sizeof(short_path), "~%s", cwd + strlen(home));
    } else {
        strncpy(short_path, cwd, sizeof(short_path));
    }

    printf(ORANGE "%s@%s:" CYAN "%s" ORANGE "> " RESET, 
           username, hostname, short_path);
    fflush(stdout);
}

void execute_command(char **args) {
    if (args[0] == NULL) return;
    
    if (strcmp(args[0], "cd") == 0) {
        char *path = args[1] ? resolve_path(args[1]) : getenv("HOME");
        if (!path) {
            fprintf(stderr, RED "dns: cd: неверный путь\n" RESET);
            return;
        }
        
        if (chdir(path) != 0) {
            fprintf(stderr, RED "dns: cd: %s: %s\n" RESET, path, strerror(errno));
        }
        update_cwd();
    }
    else if (strcmp(args[0], "exit") == 0) {
        exit(0);
    }
    else if (strcmp(args[0], "history") == 0) {
        printf("\nИстория команд:\n");
        for (int i = 0; i < cmd_history.count; i++) {
            printf("%3d %s\n", i + 1, cmd_history.items[i]);
        }
        printf("\n");
    }
    else {
        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args);
            fprintf(stderr, RED "dns: %s: %s\n" RESET, args[0], strerror(errno));
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        } else {
            fprintf(stderr, RED "dns: fork: %s\n" RESET, strerror(errno));
        }
    }
}

// ======================== MAIN функция ========================

int main() {
    signal(SIGINT, SIG_IGN);
    enable_raw_mode();
    update_cwd();
    
    print_banner();
    
    while (1) {
        print_prompt();
        
        char *input = read_input();
        if (input == NULL) continue;
        
        if (strlen(input) > 0) {
            history_add(input);
        }
        
        char *args[MAX_ARGS] = {NULL};
        int i = 0;
        args[i] = strtok(input, " ");
        while (args[i] != NULL && i < MAX_ARGS - 1) {
            args[++i] = strtok(NULL, " ");
        }
        
        execute_command(args);
        free(input);
    }
    
    return 0;
}
