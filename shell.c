#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <pwd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64
#define HISTORY_SIZE 100

#define ORANGE "\033[38;5;208m"
#define CYAN   "\033[36m"
#define GREEN  "\033[32m"
#define RED    "\033[31m"
#define RESET  "\033[0m"

typedef struct {
    char *items[HISTORY_SIZE];
    int count;
    int position;
} History;

History cmd_history = {.count = 0, .position = 0};

char cwd[PATH_MAX];
struct termios orig_termios;
char current_input[MAX_INPUT];
int input_pos = 0;

/* ====== prototypes ====== */
void print_prompt(void);
void disable_raw_mode(void);

/* ======================== пути ======================== */

char *resolve_path(const char *path) {
    static char resolved[PATH_MAX];

    if (!path) return NULL;

    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = getpwuid(getuid())->pw_dir;

        if (path[1] == '/' || path[1] == '\0') {
            snprintf(resolved, sizeof(resolved), "%s%s", home, path + 1);
        } else {
            return NULL;
        }
    } else if (path[0] != '/') {
        if (!getcwd(resolved, sizeof(resolved))) return NULL;
        strncat(resolved, "/", sizeof(resolved) - strlen(resolved) - 1);
        strncat(resolved, path, sizeof(resolved) - strlen(resolved) - 1);
    } else {
        snprintf(resolved, sizeof(resolved), "%s", path);
    }

    char *p;
    while ((p = strstr(resolved, "/./"))) memmove(p, p + 2, strlen(p + 2) + 1);
    while ((p = strstr(resolved, "//")))  memmove(p, p + 1, strlen(p + 1) + 1);

    return resolved;
}

/* ======================== терминал ======================== */

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void clear_line() {
    printf("\r\033[K");
    fflush(stdout);
}

/* ======================== история ======================== */

void history_add(const char *cmd) {
    if (!cmd || !*cmd) return;

    if (cmd_history.count &&
        strcmp(cmd_history.items[cmd_history.count - 1], cmd) == 0)
        return;

    if (cmd_history.count < HISTORY_SIZE) {
        cmd_history.items[cmd_history.count++] = strdup(cmd);
    } else {
        free(cmd_history.items[0]);
        memmove(cmd_history.items,
                cmd_history.items + 1,
                (HISTORY_SIZE - 1) * sizeof(char *));
        cmd_history.items[HISTORY_SIZE - 1] = strdup(cmd);
    }
    cmd_history.position = cmd_history.count;
}

/* ======================== ввод ======================== */

void handle_arrow_up() {
    if (cmd_history.position > 0) {
        cmd_history.position--;
        clear_line();
        print_prompt();
        snprintf(current_input, sizeof(current_input),
                 "%s", cmd_history.items[cmd_history.position]);
        input_pos = strlen(current_input);
        printf("%s", current_input);
    }
}

void handle_arrow_down() {
    if (cmd_history.position < cmd_history.count - 1) {
        cmd_history.position++;
        clear_line();
        print_prompt();
        snprintf(current_input, sizeof(current_input),
                 "%s", cmd_history.items[cmd_history.position]);
        input_pos = strlen(current_input);
        printf("%s", current_input);
    } else {
        cmd_history.position = cmd_history.count;
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
        } else if (c == '\x1b') {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;

            if (seq[0] == '[') {
                if (seq[1] == 'A') handle_arrow_up();
                if (seq[1] == 'B') handle_arrow_down();
            }
        } else if (c == 127 || c == '\b') {
            if (input_pos > 0) {
                current_input[--input_pos] = '\0';
                printf("\b \b");
            }
        } else if (isprint(c)) {
            if (input_pos < MAX_INPUT - 1) {
                current_input[input_pos++] = c;
                current_input[input_pos] = '\0';
                putchar(c);
            }
        }
        fflush(stdout);
    }
}

/* ======================== оболочка ======================== */

void update_cwd() {
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, RED "dns: getcwd: %s\n" RESET, strerror(errno));
        strcpy(cwd, "?");
    }
}

/* ======================== НОВОГОДНИЙ BANNER ======================== */

void print_banner() {
    printf(GREEN "\n");
    printf("        *        ❄        *        ❄        *\n");
    printf("               ██████╗ ███╗   ██╗ ███████╗\n");
    printf("               ██╔══██╗████╗  ██║ ██╔════╝\n");
    printf("               ██║  ██║██╔██╗ ██║ ███████╗\n");
    printf("               ██║  ██║██║╚██╗██║ ╚════██║\n");
    printf("               ██████╔╝██║ ╚████║ ███████║\n");
    printf("\n");
    printf("        Delusional Nonsense Shell — New Year Edition\n");
    printf("        Version 1.0 | Пусть баги останутся в прошлом году\n");
    printf("        ❄  ❄  ❄\n");
    printf(RESET "\n");
}

void print_prompt() {
    char hostname[256] = "unknown";
    char username[256] = "unknown";

    struct passwd *pw = getpwuid(getuid());
    if (pw) snprintf(username, sizeof(username), "%s", pw->pw_name);

    if (gethostname(hostname, sizeof(hostname)) == 0) {
        char *dot = strchr(hostname, '.');
        if (dot) *dot = '\0';
    }

    char short_path[PATH_MAX];
    const char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(short_path, sizeof(short_path), "~%s", cwd + strlen(home));
    } else {
        snprintf(short_path, sizeof(short_path), "%s", cwd);
    }

    printf(ORANGE "%s@%s:" CYAN "%s" ORANGE "> " RESET,
           username, hostname, short_path);
    fflush(stdout);
}

void execute_command(char **args) {
    if (!args[0]) return;

    if (!strcmp(args[0], "cd")) {
        char *path = args[1] ? resolve_path(args[1]) : getenv("HOME");
        if (!path || chdir(path) != 0)
            fprintf(stderr, RED "dns: cd: %s\n" RESET, strerror(errno));
        update_cwd();
    } else if (!strcmp(args[0], "exit")) {
        exit(0);
    } else if (!strcmp(args[0], "history")) {
        for (int i = 0; i < cmd_history.count; i++)
            printf("%3d %s\n", i + 1, cmd_history.items[i]);
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            execvp(args[0], args);
            perror("dns");
            exit(1);
        } else {
            wait(NULL);
        }
    }
}

int main() {
    signal(SIGINT, SIG_IGN);
    enable_raw_mode();
    update_cwd();

    print_banner();

    while (1) {
        print_prompt();

        char *input = read_input();
        if (!input) continue;

        if (*input) history_add(input);

        char *args[MAX_ARGS] = {0};
        int i = 0;
        args[i] = strtok(input, " ");
        while (args[i] && i < MAX_ARGS - 1)
            args[++i] = strtok(NULL, " ");

        execute_command(args);
        free(input);
    }
}
