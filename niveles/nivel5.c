#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>     // getenv, setenv, exit
#include <string.h>
#include <unistd.h>     // chdir, getcwd, fork, execvp, pause, write
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid, macros WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG
#include <errno.h>      // errno, strerror
#include <signal.h>     // signal, SIGINT, SIGCHLD, SIG_IGN, kill, SIGTSTP, SIGSTOP

#define PROMPT '$'
#define COMMAND_LINE_SIZE 1024
#define ARGS_SIZE 64
#define GRIS  "\x1b[38;5;245m"
#define ROJO  "\x1b[31m"
#define RESET "\033[0m"
#define COLOR_PROMPT  "\x1b[36m"   // cian


#define DEBUGN1 0  
#define DEBUGN2 0   
#define DEBUGN3 0
#define DEBUGN4 0
#define DEBUGN5 1

#define N_JOBS 64

struct info_job {
    pid_t pid;
    char estado; // ‘N’, ’E’, ‘D’, ‘F’ (‘N’: Ninguno, ‘E’: Ejecutándose y ‘D’: Detenido, ‘F’: Finalizado)
    char cmd[COMMAND_LINE_SIZE]; // línea de comando asociada
};

static struct info_job jobs_list[N_JOBS];       // tabla de trabajos
static char mi_shell[COMMAND_LINE_SIZE];        // nombre del minishell
static int n_job = 0;                           // nº de trabajos en background/detenidos (1..n_job)

//ÚNICO prototipo necesario por el ciclo execute_line <-> internal_source */
int check_internal(char **args);

//read_line (básico) 

char *read_line(char *line){
    printf(COLOR_PROMPT"%c "RESET, PROMPT);
    fflush(stdout);

    char *ptr = fgets(line, COMMAND_LINE_SIZE, stdin);

    if (ptr == NULL) {
        if (feof(stdin)) {
            printf("\nBye Bye\n");
            exit(0);
        }
        return NULL;
    }

    size_t len = strlen(ptr);
    if (len > 0 && ptr[len - 1] == '\n') {
        ptr[len - 1] = '\0';
    }

    return ptr;
}

// NIVEL 1 : parse_args 

int parse_args(char **args, char *line) {
    int ntokens = 0;
    const char *delim = " \t\n\r";

    if (line == NULL) {
        args[0] = NULL;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token 0: (null)]\n" RESET);
        #endif
        return 0;
    }

    char *tok = strtok(line, delim);

    while (tok != NULL && ntokens < ARGS_SIZE - 1) {
        args[ntokens] = tok;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token %d: %s]\n" RESET,
                ntokens, args[ntokens]);
        #endif

        if (tok[0] == '#') {
            args[ntokens] = NULL;
            #if DEBUGN1
            fprintf(stderr, GRIS "[parse_args()→ token %d corregido: (null)]\n" RESET,
                    ntokens);
            #endif
            ntokens++;
            break;
        }

        ntokens++;
        tok = strtok(NULL, delim);
    }

    if (ntokens == 0 || args[ntokens - 1] != NULL) {
        args[ntokens] = NULL;
        #if DEBUGN1
        fprintf(stderr, GRIS "[parse_args()→ token %d: (null)]\n" RESET, ntokens);
        #endif
    }

    int real_tokens = 0;
    while (real_tokens < ntokens && args[real_tokens] != NULL) {
        real_tokens++;
    }

    return real_tokens;
}

//internal_cd REAL

int internal_cd(char **args) {
    char cwd[COMMAND_LINE_SIZE];   //buffer para guardar el directorio actual
    const char *path = args[1];    //destino al que queremos ir

    //si el usuario solo ha puesto "cd", queremos ir al HOME
    if (path == NULL) {
        path = getenv("HOME");
        if (path == NULL) {
            fprintf(stderr, ROJO "cd: variable HOME no definida\n" RESET);
            return 1;
        }
    }

    //intentamos cambiar de directorio
    if (chdir(path) == -1) {
        fprintf(stderr, ROJO "chdir() error: %s\n" RESET, strerror(errno));
        return 1;
    }

    //mostramos el nuevo directorio (solo en DEBUGN2)
    if (getcwd(cwd, COMMAND_LINE_SIZE) != NULL) {
        #if DEBUGN2
        fprintf(stderr, GRIS "[internal_cd()→ PWD: %s]\n" RESET, cwd);
        #endif
    } else {
        fprintf(stderr, ROJO "getcwd() error: %s\n" RESET, strerror(errno));
    }

    return 1;   //comando interno ejecutado
}

//internal_export REAL

int internal_export(char **args){

    if (args[1] == NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: export NOMBRE=VALOR\n" RESET);
        return 1;
    }

    char *nombre = strtok(args[1], "=");
    char *valor = strtok(NULL, "");

    #if DEBUGN2
    fprintf(stderr, GRIS "[internal_export()→ nombre: %s]\n" RESET,
            nombre ? nombre : "(null)");
    fprintf(stderr, GRIS "[internal_export()→ valor: %s]\n" RESET,
            valor ? valor : "(null)");
    #endif

    if (nombre == NULL || valor == NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: export NOMBRE=VALOR\n" RESET);
        return 1;
    }

    //opcionalmente mostramos el valor antiguo
    #if DEBUGN2
    char *old = getenv(nombre);
    if (old != NULL){
        fprintf(stderr, GRIS "[internal_export()→ valor antiguo de %s: %s]\n" RESET,
                nombre, old);
    } else {
        fprintf(stderr, GRIS "[internal_export()→ valor antiguo de %s: (null)]\n" RESET,
                nombre);
    }
    #endif

    //asignar el nuevo valor
    if (setenv(nombre, valor, 1) == -1) {
        fprintf(stderr, ROJO "setenv() error: %s\n" RESET, strerror(errno));
        return 1;
    }

    //mostramos el valor nuevo
    #if DEBUGN2
    char *nuevo = getenv(nombre);
    if (nuevo != NULL) {
        fprintf(stderr, GRIS "[internal_export()→ valor nuevo de %s: %s]\n" RESET,
                nombre, nuevo);
    } else {
        fprintf(stderr, GRIS "[internal_export()→ valor nuevo de %s: (null)]\n" RESET,
                nombre);
    }
    #endif

    return 1;   //comando interno ejecutado
}

//otros comandos internos simples

int internal_jobs(void) {
    //Mostrar trabajos en background/detenidos: posiciones 1..n_job
    for (int i = 1; i <= n_job; i++) {
        if (jobs_list[i].pid > 0) {
            printf("[%d] %d %c %s\n",
                   i,
                   jobs_list[i].pid,
                   jobs_list[i].estado,
                   jobs_list[i].cmd);
        }
    }
    return 1;
}

int internal_fg(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_fg()→ Esta función pasará un trabajo al primer plano]\n" RESET);
    #endif
    (void)args;
    return 1;
}

int internal_bg(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_bg()→ Esta función reanudará un trabajo en segundo plano]\n" RESET);
    #endif
    (void)args;
    return 1;
}

//gestión jobs_list

int jobs_list_add(pid_t pid, char estado, char *cmd) {
    if (n_job >= N_JOBS - 1) {
        fprintf(stderr, ROJO "jobs_list llena, no se puede añadir más trabajos\n" RESET);
        return -1;
    }

    n_job++;
    jobs_list[n_job].pid = pid;
    jobs_list[n_job].estado = estado;
    memset(jobs_list[n_job].cmd, '\0', COMMAND_LINE_SIZE);
    if (cmd != NULL) {
        strncpy(jobs_list[n_job].cmd, cmd, COMMAND_LINE_SIZE - 1);
    }

    //mensaje como en el enunciado (salida normal, sin GRIS)
    printf("[%d] %d %c %s\n", n_job, pid, estado, jobs_list[n_job].cmd);
    fflush(stdout);

    return 0;
}

int jobs_list_find(pid_t pid) {
    for (int i = 1; i <= n_job; i++) {
        if (jobs_list[i].pid == pid) {
            return i;
        }
    }
    return -1; //no encontrado
}

int jobs_list_remove(int pos) {
    if (pos < 1 || pos > n_job) {
        return -1;
    }

    if (pos < n_job) {
        jobs_list[pos] = jobs_list[n_job];   //mover el último a la posición eliminada
    }

    jobs_list[n_job].pid = 0;
    jobs_list[n_job].estado = 'N';
    memset(jobs_list[n_job].cmd, '\0', COMMAND_LINE_SIZE);
    n_job--;

    return 0;
}

//detección de '&' (background) 
int is_background(char **args) {
    int i = 0;
    while (args[i] != NULL) {
        i++;
    }

    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        args[i - 1] = NULL; // quitamos el &
        return 1;
    }
    return 0;
}

//manejador SIGCHLD → reaper

void reaper(int signum)
{
    signal(SIGCHLD, reaper);  //rearmar manejador

    pid_t ended;
    int status;
    char mensaje[4096];

    while ((ended = waitpid(-1, &status, WNOHANG)) > 0) {

        #if DEBUGN4
        //Mensaje de recibida señal, AHORA POR CADA HIJO
        sprintf(mensaje, GRIS
                "[reaper()→ recibida señal %d (SIGCHLD)]\n" RESET,
                signum);
        write(2, mensaje, strlen(mensaje));
        #endif
        int es_foreground = 0;
        int pos = -1;

        if (ended == jobs_list[0].pid) {
            es_foreground = 1;
        } else {
            pos = jobs_list_find(ended);
        }

        #if DEBUGN4
        if (WIFEXITED(status)) {
            if (es_foreground) {
                sprintf(mensaje, GRIS
                        "[reaper()→ Proceso hijo %d en foreground (%s) finalizado con exit code %d]\n" RESET,
                        ended, jobs_list[0].cmd, WEXITSTATUS(status));
            } else {
                sprintf(mensaje, GRIS
                        "[reaper()→ Proceso hijo %d en background (%s) finalizado con exit code %d]\n" RESET,
                        ended,
                        (pos > 0 ? jobs_list[pos].cmd : ""),
                        WEXITSTATUS(status));
            }
        } else if (WIFSIGNALED(status)) {
            if (es_foreground) {
                sprintf(mensaje, GRIS
                        "[reaper()→ Proceso hijo %d en foreground (%s) finalizado por señal %d]\n" RESET,
                        ended, jobs_list[0].cmd, WTERMSIG(status));
            } else {
                sprintf(mensaje, GRIS
                        "[reaper()→ Proceso hijo %d en background (%s) finalizado por señal %d]\n" RESET,
                        ended,
                        (pos > 0 ? jobs_list[pos].cmd : ""),
                        WTERMSIG(status));
            }
        } else {
            sprintf(mensaje, GRIS
                    "[reaper()→ Proceso hijo %d finalizado]\n" RESET,
                    ended);
        }

        write(2, mensaje, strlen(mensaje));
        #endif

        if (es_foreground) {
            jobs_list[0].pid = 0;
            jobs_list[0].estado = 'F';
            memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
        } else if (pos > 0) {
            int st = WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
            sprintf(mensaje,
                    "Terminado PID %d (%s) en jobs_list[%d] con status %d\n",
                    ended, jobs_list[pos].cmd, pos, st);
            write(1, mensaje, strlen(mensaje));
            jobs_list_remove(pos);
        }
    }
}



//manejador SIGINT → ctrlc (Ctrl+C)

void ctrlc(int signum)
{
    (void)signum; //evitar warning

    signal(SIGINT,ctrlc);

    char mensaje[4096];

    #if DEBUGN4
    sprintf(mensaje, GRIS
            "\n[ctrlc()→ Soy el proceso con PID %d (%s), el proceso en foreground es %d (%s)]\n" RESET,
            getpid(), mi_shell, jobs_list[0].pid, jobs_list[0].cmd);

    write(2, mensaje, strlen(mensaje));

    sprintf(mensaje, GRIS
                "[ctrlc()→ recibida señal %d (SIGINT)]\n" RESET, signum);
    write(2, mensaje, strlen(mensaje));
    #endif
    (void)mensaje; //evitamos warning

    if (jobs_list[0].pid > 0) {

        //¿es el proceso en foreground el propio shell??
        if (strcmp(jobs_list[0].cmd, mi_shell)== 0) {
            #if DEBUGN4
            sprintf(mensaje, GRIS "[ctrlc()→ Señal 15 no enviada por %d (%s) debido a que su proceso en foreground es el shell]\n" RESET,
                    getpid(), mi_shell);
            write(2, mensaje, strlen(mensaje));
            #endif

        } else{

            //enviamos SIGTERM al hijo en foreground
            if(kill(jobs_list[0].pid, SIGTERM)== 0){
                #if DEBUGN4
                sprintf(mensaje, GRIS"[ctrlc()→ Señal 15 (SIGTERM) enviada a %d (%s) por %d (%s)]\n" RESET,
                        jobs_list[0].pid, jobs_list[0].cmd, getpid(), mi_shell);
                write(2, mensaje, strlen(mensaje));
                #endif
            }else {
                perror("kill");
            }
        }

    } else {
        #if DEBUGN4
        sprintf(mensaje,GRIS "[ctrlc()→ Señal 15 no enviada por %d (%s) debido a que no hay proceso en foreground]\n" RESET,
                getpid(), mi_shell);
        write(2, mensaje, strlen(mensaje));
        #endif
    }
}

//manejador SIGTSTP → ctrlz (Ctrl+Z)

void ctrlz(int signum)
{
    (void)signum;

    signal(SIGTSTP, ctrlz);

    char mensaje[4096];

    #if DEBUGN5
    sprintf(mensaje, GRIS
            "\n[ctrlz()→ Soy el proceso con PID %d, el proceso en foreground es %d (%s)]\n" RESET,
            getpid(), jobs_list[0].pid, jobs_list[0].cmd);
    write(2, mensaje, strlen(mensaje));

    sprintf(mensaje, GRIS"[ctrlz()→ recibida señal %d (SIGTSTP)]\n" RESET, SIGTSTP);
    write(2, mensaje, strlen(mensaje));
    #endif
    (void)mensaje;

    if (jobs_list[0].pid > 0) {
        //¿el proceso en foreground es el shell?
        if (strcmp(jobs_list[0].cmd, mi_shell) == 0) {
            #if DEBUGN5
            sprintf(mensaje, GRIS"[ctrlz()→ Señal %d (SIGSTOP) no enviada debido a que el proceso en foreground es el shell]\n" RESET,
                    SIGSTOP);
            write(2, mensaje, strlen(mensaje));
            #endif
        } else {
            //enviar SIGSTOP al hijo en foreground
            if (kill(jobs_list[0].pid, SIGSTOP) == 0) {
                #if DEBUGN5
                sprintf(mensaje, GRIS"[ctrlz()→ Señal %d (SIGSTOP) enviada a %d (%s) por %d (%s)]\n" RESET,
                        SIGSTOP, jobs_list[0].pid, jobs_list[0].cmd, getpid(), mi_shell);
                write(2, mensaje, strlen(mensaje));
                #endif

                //cambiar estado a detenido y añadir a jobs_list por el final
                jobs_list[0].estado = 'D';
                jobs_list_add(jobs_list[0].pid, 'D', jobs_list[0].cmd);

                //resetear foreground
                jobs_list[0].pid = 0;
                jobs_list[0].estado = 'N';
                memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
            } else {
                perror("kill");
            }
        }
    } else {
        #if DEBUGN5
        sprintf(mensaje, GRIS"[ctrlz()→ Señal %d (SIGSTOP) no enviada debido a que no hay proceso en foreground]\n" RESET,
                SIGSTOP);
        write(2, mensaje, strlen(mensaje));
        #endif
    }
}

//execute_line con background 

int execute_line(char *line){

    char *args[ARGS_SIZE];
    char line_copy[COMMAND_LINE_SIZE];  //copia de la línea original
    pid_t pid;

    //guardar copia antes de parsear
    memset(line_copy, '\0', COMMAND_LINE_SIZE);
    strncpy(line_copy, line, COMMAND_LINE_SIZE - 1);

    int ntokens = parse_args(args, line);
    if (ntokens <= 0){
        return 0;
    }

    //comandos internos
    if (check_internal(args)) {
        return 0;
    }

    //¿foreground o background?
    int bg = is_background(args);

    //mensaje PID padre
    #if DEBUGN4
    fprintf(stderr, GRIS "[execute_line()→ PID padre: %d (%s)]\n" RESET,
            getpid(), mi_shell);
    #endif

    pid = fork();

    if (pid == 0){
        //PROCESO HIJO

        //ignora Ctrl+C y Ctrl+Z
        signal(SIGINT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);

        //AQUÍ YA NO IMPRIMIMOS NADA DEL HIJO

        execvp(args[0], args);

        fprintf(stderr, ROJO "%s: %s\n" RESET, args[0], strerror(errno));
        exit(-1);

    } else if (pid > 0) {
        //PROCESO PADRE 

        //AQUÍ imprimimos el mensaje del "hijo"
        #if DEBUGN4
        fprintf(stderr, GRIS "[execute_line()→ PID hijo: %d (%s)]\n" RESET,
                pid, line_copy);
        #endif

        if (!bg) {
            //FOREGROUND
            jobs_list[0].pid = pid;
            jobs_list[0].estado = 'E';
            memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
            strncpy(jobs_list[0].cmd, line_copy, COMMAND_LINE_SIZE - 1);

            while (jobs_list[0].pid > 0) {
                pause();
            }
        } else {
            //BACKGROUND → añadir a lista y mostrar [1] PID E cmd
            jobs_list_add(pid, 'E', line_copy);
        }

    } else {
        fprintf(stderr, ROJO "fork() error: %s\n" RESET, strerror(errno));
        return -1;
    }

    return 0;
}


// internal_source REAL 

int internal_source(char **args) {
    //Comprobación de sintaxis
    if (args[1] == NULL || args[2] != NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: source <nombre_fichero>\n" RESET);
        return 1;   //interno tratado
    }

    FILE *f = fopen(args[1], "r");
    if (f == NULL) {
        fprintf(stderr, ROJO "fopen() error: %s\n" RESET, strerror(errno));
        return 1;
    }

    char line[COMMAND_LINE_SIZE];

    while (fgets(line, COMMAND_LINE_SIZE, f) != NULL) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        #if DEBUGN3
        fprintf(stderr, GRIS "[internal_source()→ LINE: %s]\n" RESET, line);
        #endif
        fflush(f);
        execute_line(line);
    }

    fclose(f);
    return 1;
}

// check_internal

int check_internal(char **args){
    if (args == NULL || args[0] == NULL) {
        return 0;
    }

    if (strcmp(args[0], "cd") == 0){
        return internal_cd(args);      //en nivel 1 era “de mentira”, en nivel 2 real
    }
    if (strcmp(args[0], "export") == 0){
        return internal_export(args);  //idem
    }
    if (strcmp(args[0], "source") == 0){
        return internal_source(args);  //en nivel 3 se hace real
    }
    if (strcmp(args[0], "jobs")== 0){
        return internal_jobs();
    }
    if (strcmp(args[0], "fg") == 0) {
        return internal_fg(args);
    }
    if (strcmp(args[0], "bg") == 0) {
        return internal_bg(args);
    }
    if (strcmp(args[0], "exit") == 0) {
        printf("Bye Bye\n");
        exit(0);
    }

    return 0;
}

// main 

int main(int argc, char *argv[]){
    char line[COMMAND_LINE_SIZE];

    //Inicializar toda la jobs_list
    for (int i = 0; i < N_JOBS; i++) {
        jobs_list[i].pid = 0;
        jobs_list[i].estado = 'N';
        memset(jobs_list[i].cmd, '\0', COMMAND_LINE_SIZE);
    }
    n_job = 0;

    //Guardar el nombre del minishell (./nivel5, ./my_shell, etc.)
    memset(mi_shell, '\0', COMMAND_LINE_SIZE);
    if (argc > 0 && argv[0] != NULL) {
        strncpy(mi_shell, argv[0], COMMAND_LINE_SIZE - 1);
    } else {
        strncpy(mi_shell, "minishell", COMMAND_LINE_SIZE - 1);
    }

    //Registrar manejadores de señal para nivel 4/5
    signal(SIGCHLD, reaper);
    signal(SIGINT,  ctrlc);
    signal(SIGTSTP, ctrlz);

    while(1){
        if(read_line(line)){
            execute_line(line);
        }
    }
    return 0;
}