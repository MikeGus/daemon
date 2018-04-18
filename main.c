#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/file.h>


#define LOCKFILE "/var/run/daemon.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)


int already_running(void) {
    int fd;
    char buf[16];

    fd = open(LOCKFILE, O_RDWR|O_CREAT, LOCKMODE);
    if (fd < 0) {
        syslog(LOG_ERR, "Can't open %s: %s", LOCKFILE, strerror(errno));
        exit(1);
    }

    flock(fd, LOCK_EX|LOCK_NB);
    if (errno == EWOULDBLOCK) {
        close(fd);
        syslog(LOG_ERR, "Can't block %s: %s", LOCKFILE, strerror(errno));
        exit(1);
    }
    ftruncate(fd, 0);
    sprintf(buf, "%ld", (long) getpid());
    write(fd, buf, strlen(buf) + 1);

    return 0;
}


void daemonize(const char* cmd) {
    int fd0, fd1, fd2;
    unsigned i;
    pid_t pid;
    struct rlimit rl;
    struct sigaction sa;

//    Сбросить маску режима создания файла
    umask(0);

//    Получить максимально возможный номер дескриптора файла
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        fprintf(stderr, "%s: Can't get max descriptor number", cmd);
        exit(1);
    }

//    Стать лидером сессии, чтобы устратить управляющий терминал
    if ((pid = fork()) < 0) {
        perror("Can't fork!");
        exit(1);
    } else if (pid != 0) { /*родительский процесс*/
        exit(0);
    }

    setsid();

//    Обеспечить невозможность обретения управляющим терминалом в будущем
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        fprintf(stderr, "%s: Can't ignore signal SIGHUP", cmd);
        exit(1);
    }

//    Назначить корневой каталог текущим рабочим каталогом,
//    чтобы впоследствии можно было отмонтировать файловую систему
    if (chdir("/") < 0) {
        perror("Can't make / current working directory");
        exit(1);
    }

//    Закрыть все открытые файловые дескрипторы
    if (rl.rlim_max == RLIM_INFINITY) {
        rl.rlim_max = 1024;
    }
    for (i = 0; i < rl.rlim_max; ++i) {
        close(i);
    }

//    Присоединить файловые дескрипторы 0, 1 и 2 к /dev/null
    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(0);
    fd2 = dup(0);

//    Инициализировать файл журнала
    openlog(cmd, LOG_CONS, LOG_DAEMON);
    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        syslog(LOG_ERR, "Wrong file descriptors %d %d %d", fd0, fd1, fd2);
        exit(1);
    }
}


int main(int argc, char* argv[]) {
    time_t rawtime;
    char* cmd = NULL;

    if (argc > 1) {
        cmd = argv[1];
    }

    daemonize(cmd);

    if (already_running()) {
        syslog(LOG_ERR, "Daemon already running");
        exit(1);
    }

    while(1) {
        rawtime = time(NULL);
        syslog(LOG_INFO, "[%d] current time: %s", getpid(), ctime(&rawtime));
        sleep(3);
    }

    return 0;
}
