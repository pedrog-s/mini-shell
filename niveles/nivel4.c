#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>     // getenv, setenv, exit
#include <string.h>
#include <unistd.h>     // chdir, getcwd, fork, execvp, pause, write
#include <sys/types.h>  // pid_t
#include <sys/wait.h>   // waitpid, macros WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG
#include <errno.h>      // errno, strerror
#include <signal.h>     // signal, SIGINT, SIGCHLD, SIG_IGN, kill

#define PROMPT '$'
#define COMMAND_LINE_SIZE 1024
#define ARGS_SIZE 64
#define GRIS  "\x1b[38;5;245m"
#define ROJO  "\x1b[31m"
#define RESET "\033[0m"
#define COLOR_PROMPT  "\x1b[36m"   // cian



#define DEBUGN1 0   //nivel 1: NO MOSTRAR
#define DEBUGN2 0   //nivel 2 : NO MOSTRAR
#define DEBUGN3 0
#define DEBUGN4 1

#define N_JOBS 64

//estructura que representa un job del shell

struct info_job {
    pid_t pid;
    char estado; // ‘N’, ’E’, ‘D’, ‘F’ (‘N’: Ninguno, ‘E’: Ejecutándose y ‘D’: Detenido, ‘F’: Finalizado) 
    char cmd[COMMAND_LINE_SIZE]; //línea de comando asociada
};

//tabla de trabajos

static struct info_job jobs_list[N_JOBS];       //tabla de trabajos
static char mi_shell[COMMAND_LINE_SIZE];        //nombre del minishell

//prototipo delcarado porque execute_line lo usa antes de definirlo
int check_internal(char **args);   

//-----nivel1 :read_line------------
//lee una linea de la entrada estandar, muestra el prompt y elimina el '\n'

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


//nivel1-----parse_args

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

//internal cd real------
int internal_cd(char **args) {
    char cwd[COMMAND_LINE_SIZE];   // buffer para guardar el directorio actual
    const char *path = args[1];    // destino al que queremos ir

    // si el usuario solo ha puesto "cd", queremos ir al HOME
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
        return 1;   //comando interno tratado aunque haya fallado
    }

    //mostramos el nuevo directorio actual 
    if (getcwd(cwd, COMMAND_LINE_SIZE) != NULL) {
        #if DEBUGN2
        fprintf(stderr, GRIS "[internal_cd()→ PWD: %s]\n" RESET, cwd);
        #endif
    } else {
        fprintf(stderr, ROJO "getcwd() error: %s\n" RESET, strerror(errno));
    }

    return 1;   
}


// nivel2: internal export---------

int internal_export(char **args){
    
    // comprobamos que hay al menos un argumento despues de "export"
    if (args[1] == NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: export NOMBRE=VALOR\n" RESET);
        return 1;   
    }

    //separamos NOMBRE y VALOR por el '='
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

    //mostramos valor antiguo
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

    return 1;   
}


// otros comandos----
int internal_jobs(void) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_jobs()→ Esta función mostrará los trabajos en segundo plano]\n" RESET);
    #endif
    return 1;
}

int internal_fg(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_fg()→ Esta función pasará un trabajo al primer plano]\n" RESET);
    #endif
    return 1;
}

int internal_bg(char **args) {
    #if DEBUGN1
    fprintf(stderr, GRIS "[internal_bg()→ Esta función reanudará un trabajo en segundo plano]\n" RESET);
    #endif
    return 1;
}




//nivel4: manejador SIGCHLD reaper
void reaper(int signum)

{

    (void)signum; //evita el warning si no se usa

    signal(SIGCHLD, reaper);

    pid_t ended;
    int status;
    char mensaje[4096];


    //enterrar todos los hijos que hayan terminado 

    while ((ended = waitpid(-1,&status, WNOHANG)) > 0 ) {
        if (WIFEXITED(status)){
            sprintf(mensaje, GRIS"[reaper()→ Proceso hijo %d (%s) finalizado con exit code %d]\n" RESET,
                    ended, jobs_list[0].cmd, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            sprintf(mensaje, GRIS"[reaper()→ Proceso hijo %d (%s) finalizado por señal %d]\n" RESET,
                    ended, jobs_list[0].cmd, WTERMSIG(status));


        } else {
            sprintf(mensaje, GRIS "[reaper()→ Proceso hijo %d (%s) finalizado]\n" RESET,
                    ended, jobs_list[0].cmd);
        }
        write(2, mensaje, strlen(mensaje));

        //si era el proceso en foreground, lo limpiamos

        if (ended == jobs_list[0].pid) {
            jobs_list[0].pid = 0;
            jobs_list[0].estado = 'F';
            memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
        }
    }
}


//nivel4: manejador SIGNINT ctrlc

void ctrlc(int signum)

{

    (void)signum; // evitamos warning

    signal(SIGINT,ctrlc);

    char mensaje[4096];

    sprintf(mensaje, GRIS
            "\n[ctrlc()→ Soy el proceso con PID %d (%s), el proceso en foreground es %d (%s)]\n" RESET,
            getpid(), mi_shell, jobs_list[0].pid, jobs_list[0].cmd);

    write(2, mensaje, strlen(mensaje));


    if (jobs_list[0].pid > 0) {
        //hay procesos en foreground

        //del propio shell??
        if (strcmp(jobs_list[0].cmd, mi_shell)== 0) {
            sprintf(mensaje, GRIS "[ctrlc()→ Señal 15 no enviada por %d (%s) debido a que su proceso en foreground es el shell]\n" RESET,
                    getpid(), mi_shell);
            write(2, mensaje, strlen(mensaje));

        } else{

            // enviamos SIGTERM al hijo en foreground
            if(kill(jobs_list[0].pid, SIGTERM)== 0){
                sprintf(mensaje, GRIS"[ctrlc()→ Señal 15 enviada a %d (%s) por %d (%s)]\n" RESET,
                        jobs_list[0].pid, jobs_list[0].cmd, getpid(), mi_shell);
                write(2, mensaje, strlen(mensaje));
            }else {
                perror("kill");
            }
        }
    } else {
        //no hay proceso en foreground
        sprintf(mensaje,GRIS "[ctrlc()→ Señal 15 no enviada por %d (%s) debido a que no hay proceso en foreground]\n" RESET,
        getpid(), mi_shell);

        write(2, mensaje, strlen(mensaje));
    }
}

// == NIVEL 4: execute_line con señales y jobs_list[0] ======
int execute_line(char *line){

    char *args[ARGS_SIZE];
    char line_copy[COMMAND_LINE_SIZE];  // copia de la linea original para jobs_list
    pid_t pid;

    //nivel 3: guardamos copia de la linea antes de parsearla 
    memset(line_copy, '\0', COMMAND_LINE_SIZE);
    strncpy(line_copy, line, COMMAND_LINE_SIZE - 1);

    int ntokens = parse_args(args, line);

    if (ntokens <= 0){
        return 0;
    }

    // nivel 1/2: primero comprobar comandos internos
    if (check_internal(args)){
        return 0;
    }

    // nivel 4 -> comando externo con señales
    #if DEBUGN4
    fprintf(stderr, GRIS "[execute_line()→ PID padre: %d (%s)]\n" RESET,
            getpid(), mi_shell);
    #endif

    pid = fork();

    if (pid == 0){
        // PROCESO HIJO

        // El hijo IGNORA SIGINT (Ctrl+C)
        signal(SIGINT, SIG_IGN);
        #if DEBUGN4
        fprintf(stderr, GRIS "[execute_line()→ PID hijo: %d (%s)]\n" RESET,
                getpid(), line_copy);
        #endif
        execvp(args[0], args);

        // si execvp falla, seguimos ejecutando aqui
        fprintf(stderr, ROJO "%s: %s\n" RESET, args[0], strerror(errno));
        exit(-1);

    } else if (pid > 0) {
        // PROCESO PADRE 

        // guardamos en jobs_list[0] el trabajo en primer plano
        jobs_list[0].pid = pid;
        jobs_list[0].estado = 'E';
        memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
        strncpy(jobs_list[0].cmd, line_copy, COMMAND_LINE_SIZE - 1);

        // NIVEL 4: NO hacemos wait() aquí.
        // Esperamos a que el hijo termine vía señales (reaper/ctrlc)
        while (jobs_list[0].pid > 0) {
            pause();   // nos dormimos hasta que llegue SIGCHLD o SIGINT
        }

    } else {
        // error en el fork 
        fprintf(stderr, ROJO "fork() error: %s\n" RESET, strerror(errno));
        return -1; 
    }

    return 0;
}
//------NIVEL 3: internal_source REAL 

int internal_source(char **args) {
    // Comprobación de sintaxis
    if (args[1] == NULL || args[2] != NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: source <nombre_fichero>\n" RESET);
        return 1;   // interno tratado
    }

    FILE *f = fopen(args[1], "r");
    if (f == NULL) {
        fprintf(stderr, ROJO "fopen() error: %s\n" RESET, strerror(errno));
        return 1;
    }

    char line[COMMAND_LINE_SIZE];

    while (fgets(line, COMMAND_LINE_SIZE, f) != NULL) {
        // Quitar salto de línea final
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        #if DEBUGN3
        fprintf(stderr, GRIS "[internal_source()→ LINE: %s]\n" RESET, line);
        #endif

        fflush(f);          // según enunciado
        execute_line(line); // ejecutamos cada línea con nuestro minishell
    }

    fclose(f);
    return 1;
}


//-------check_internal 

int check_internal(char **args){
    if (args == NULL || args[0] == NULL) {
        return 0;
    }

    if (strcmp(args[0], "cd") == 0){
        return internal_cd(args);      // en nivel 1 era “de mentira”, en nivel 2 real
    }
    if (strcmp(args[0], "export") == 0){
        return internal_export(args);  // idem
    }
    if (strcmp(args[0], "source") == 0){
        return internal_source(args);  // en nivel 3 se hace real
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

    return 0;   // no era interno
}

int main(int argc, char *argv[]){
    char line[COMMAND_LINE_SIZE];

    // Inicializar jobs_list[0]
    jobs_list[0].pid = 0;
    jobs_list[0].estado = 'N';
    memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);

    // Guardar el nombre del minishell 
    memset(mi_shell, '\0', COMMAND_LINE_SIZE);
    if (argc > 0 && argv[0] != NULL) {
        strncpy(mi_shell, argv[0], COMMAND_LINE_SIZE - 1);
    } else {
        strncpy(mi_shell, "minishell", COMMAND_LINE_SIZE - 1);
    }

    // Registrar manejadores de señal para nivel 4
    signal(SIGCHLD, reaper);
    signal(SIGINT,  ctrlc);

    while(1){
        if(read_line(line)){
            execute_line(line);
        }
    }
    return 0;
}