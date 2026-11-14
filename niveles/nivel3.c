#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>     
#include <string.h>
#include <unistd.h>     
#include <sys/types.h>  
#include <sys/wait.h>   
#include <errno.h>      

//Constantes esteticas y de tamaño
#define PROMPT '$'
#define COMMAND_LINE_SIZE 1024
#define ARGS_SIZE 64
#define GRIS  "\x1b[38;5;245m"
#define ROJO  "\x1b[31m"
#define RESET "\033[0m"
#define COLOR_PROMPT  "\x1b[36m"   

//Niveles de depuracion
#define DEBUGN1 0   
#define DEBUGN2 0   
#define DEBUGN3 1


//tabla de jobs y nombre de shell

#define N_JOBS 64

struct info_job {
   pid_t pid;
   char estado; // ‘N’, ’E’, ‘D’, ‘F’ (‘N’: Ninguno, ‘E’: Ejecutándose y ‘D’: Detenido, ‘F’: Finalizado) 
   char cmd[COMMAND_LINE_SIZE]; // línea de comando asociada
};

static struct info_job jobs_list[N_JOBS];       // tabla de trabajos
static char mi_shell[COMMAND_LINE_SIZE];        // nombre del minishell

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


// parse_args

int parse_args(char **args, char *line) {
    int ntokens = 0;
    const char *delim = " \t\n\r";  // según enunciado

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

    // intentamos cambiar de directorio
    if (chdir(path) == -1) {
        fprintf(stderr, ROJO "chdir() error: %s\n" RESET, strerror(errno));
        return 1;   // comando interno tratado aunque haya fallado
    }

    // mostramos el nuevo directorio actual 
    if (getcwd(cwd, COMMAND_LINE_SIZE) != NULL) {
        #if DEBUGN2
        fprintf(stderr, GRIS "[internal_cd()→ PWD: %s]\n" RESET, cwd);
        #endif
    } else {
        fprintf(stderr, ROJO "getcwd() error: %s\n" RESET, strerror(errno));
    }

    return 1;   // era un comando interno
}


// internal_export REAL

int internal_export(char **args){
    
    // comprobamos que hay al menos un argumento despues de "export"
    if (args[1] == NULL) {
        fprintf(stderr, ROJO "Error de sintaxis. Uso: export NOMBRE=VALOR\n" RESET);
        return 1;   // sigue siendo comando interno 
    }

    // separamos NOMBRE y VALOR por el '='
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

    // mostramos valor antiguo
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
    
    // mostramos el valor nuevo
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

    return 1;   // comando interno ejecutado
}


//otros comandos internos simples

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


//execute_line con externos y jobs_list[0] 

int execute_line(char *line){

    char *args[ARGS_SIZE];
    char line_copy[COMMAND_LINE_SIZE];  // copia de la linea original para jobs_list
    pid_t pid;
    int status;

    //guardamos copia de la linea antes de parsearla 
    memset(line_copy, '\0', COMMAND_LINE_SIZE);
    strncpy(line_copy, line, COMMAND_LINE_SIZE - 1);

    int ntokens = parse_args(args, line);

    if (ntokens <= 0){
        return 0;
    }

    //primero comprobar comandos internos
    if (/* declaración previa no existe */ check_internal(args)){
        return 0;
    }

    //comando externo
    #if DEBUGN3
    fprintf(stderr, GRIS "[execute_line()→ PID padre: %d (%s)]\n" RESET,
        getpid(), mi_shell);
    #endif
    pid = fork();

    if (pid == 0){
        //PROCESO HIJO
        #if DEBUGN3
        fprintf(stderr, GRIS "[execute_line()→ PID hijo: %d (%s)]\n" RESET,
            getpid(), line_copy);
        #endif
        execvp(args[0], args);

        //si execvp falla, seguimos ejecutando aqui
        fprintf(stderr, ROJO "%s: %s\n" RESET, args[0], strerror(errno));
        exit(-1);

    } else if (pid > 0) {
        //PROCESO PADRE 

        //guardamos en jobs_list[0] el trabajo en primer plano
        jobs_list[0].pid = pid;
        jobs_list[0].estado = 'E';
        memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);
        strncpy(jobs_list[0].cmd, line_copy, COMMAND_LINE_SIZE - 1);

        //esperamos a que el hijo termine
        if(wait(&status) > 0 ) {

            int exit_status;
            if (WIFEXITED(status)){
                exit_status = WEXITSTATUS(status);
            } else {
                exit_status = status;
            }
            #if DEBUGN3 
            fprintf(stderr, GRIS "[execute_line()→ Proceso hijo %d (%s) finalizado con exit(), status: %d]\n" RESET,
                pid, jobs_list[0].cmd, exit_status);
            #endif
        }

        //reseteamos jobs_list[0]
        jobs_list[0].pid = 0;
        jobs_list[0].estado = 'N';
        memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);

    } else {
        //error en el fork 
        fprintf(stderr, ROJO "fork() error: %s\n" RESET, strerror(errno));
        return -1; 
    }

    return 0;
}


//internal_source REAL

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
        //Quitar salto de línea final
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        #if DEBUGN3
        fprintf(stderr, GRIS "[internal_source()→ LINE: %s]\n" RESET, line);
        #endif
        fflush(f);          //según enunciado
        execute_line(line); //ejecutamos cada línea con nuestro minishell
    }

    fclose(f);
    return 1;
}


//check_internal 

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

    return 0;   //no era interno
}


//main

int main(int argc, char *argv[]){
    char line[COMMAND_LINE_SIZE];

    //Inicializar jobs_list[0]
    jobs_list[0].pid = 0;
    jobs_list[0].estado = 'N';
    memset(jobs_list[0].cmd, '\0', COMMAND_LINE_SIZE);

    //Guardar el nombre del minishell (./nivel3, ./my_shell, etc.)
    memset(mi_shell, '\0', COMMAND_LINE_SIZE);
    if (argc > 0 && argv[0] != NULL) {
        strncpy(mi_shell, argv[0], COMMAND_LINE_SIZE - 1);
    } else {
        strncpy(mi_shell, "minishell", COMMAND_LINE_SIZE - 1);
    }

    while(1){
        if(read_line(line)){
            execute_line(line);
        }
    }
    return 0;
}