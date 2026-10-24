// FLX OS PID 1. Mounts the virtual filesystems and the persistent home disk,
// starts flx-netd and flx-compositor, reaps orphans, restarts the compositor if it
// crashes, and performs shutdown/restart when the compositor asks (exit code 42/43).
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>

#define EXIT_RESTART 42
#define EXIT_SHUTDOWN 43

static void say(const char *fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b - 1, fmt, ap);
    va_end(ap);
    b[n++] = '\n';
    write(2, b, n);
}

static pid_t spawn(const char *path) {
    pid_t p = fork();
    if (p == 0) {
        setsid();
        char *argv[] = { (char *)path, 0 };
        char *envp[] = { "PATH=/bin:/sbin", "HOME=/home/user", "USER=user", "TERM=xterm-256color", 0 };
        execve(path, argv, envp);
        say("init: exec %s failed: %s", path, strerror(errno));
        _exit(127);
    }
    return p;
}

static void own_user_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st)) return;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) { struct dirent *e; while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char child[512]; snprintf(child, sizeof child, "%s/%s", path, e->d_name); own_user_tree(child);
        } closedir(d); }
    }
    chown(path, 1000, 1000);
}

static void power(int restart) {
    say("W12:POWER %s", restart ? "restart" : "shutdown");
    kill(-1, SIGTERM);
    for (int i = 0; i < 20 && waitpid(-1, 0, WNOHANG) >= 0; i++) usleep(50000);
    kill(-1, SIGKILL);
    while (waitpid(-1, 0, WNOHANG) > 0) {}
    sync();
    umount2("/home", MNT_DETACH);
    sync();
    say("init: %s now", restart ? "restarting" : "powering off");
    reboot(restart ? RB_AUTOBOOT : RB_POWER_OFF);
    for (;;) pause();
}

int main(void) {
    if (getpid() != 1) { say("init: not PID 1"); return 1; }
    mount("proc", "/proc", "proc", 0, 0);
    mount("sysfs", "/sys", "sysfs", 0, 0);
    mount("devtmpfs", "/dev", "devtmpfs", 0, 0);
    mkdir("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, "mode=620,ptmxmode=666");
    mount("tmpfs", "/run", "tmpfs", 0, "mode=755");
    mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");
    int fd = open("/dev/console", O_RDWR);
    if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
    say("W12:BOOT init");
    sethostname("flx-os", 9);
    // persistent home disk (virtio-blk, ext2); fall back to RAM if missing
    int ok = -1;
    for (int i = 0; i < 40 && ok; i++) {
        ok = mount("/dev/vda", "/home", "ext2", MS_NOATIME, 0);
        if (ok) usleep(50000);
    }
    if (ok) { say("init: no home disk (%s), using tmpfs", strerror(errno)); mount("tmpfs", "/home", "tmpfs", 0, 0); }
    else say("init: /home mounted from /dev/vda (ext2)");
    mkdir("/home/users", 0755);
    chmod("/home/users", 0755);
    chown("/home/users", 0, 0);
    struct stat home_st;
    if (!lstat("/home/user", &home_st)) {
        if (S_ISDIR(home_st.st_mode) && !S_ISLNK(home_st.st_mode)) {
            struct stat flx_st;
            if (lstat("/home/users/flx", &flx_st)) rename("/home/user", "/home/users/flx");
            else if (lstat("/home/users/legacy", &flx_st)) rename("/home/user", "/home/users/legacy");
        } else unlink("/home/user");
    }
    if (lstat("/home/users/flx", &home_st)) mkdir("/home/users/flx", 0700);
    chmod("/home/users/flx", 0700);
    unlink("/home/user");
    if (symlink("/home/users/flx", "/home/user")) say("init: cannot link active home (%s)", strerror(errno));
    mkdir("/home/user/.config", 0700);
    mkdir("/home/user/Documents", 0755);
    mkdir("/home/user/Pictures", 0755);
    mkdir("/home/user/Desktop", 0755);
    mkdir("/home/user/Downloads", 0755);
    mkdir("/home/user/Music", 0755);
    mkdir("/home/.config", 0755);
    mkdir("/home/.flx-system", 0700);
    chmod("/home/.flx-system", 0700);
    chown("/home/.flx-system", 0, 0);
    own_user_tree("/home/users/flx");
    signal(SIGINT, SIG_IGN);
    pid_t net = spawn("/bin/flx-netd");
    pid_t dev = spawn("/bin/flx-deviced");
    pid_t auth = spawn("/bin/flx-authd");
    pid_t comp = spawn("/bin/flx-compositor");
    int crashes = 0;
    for (;;) {
        int st;
        pid_t p = wait(&st);
        if (p < 0) { if (errno == ECHILD) sleep(1); continue; }
        if (p == comp) {
            if (WIFEXITED(st) && WEXITSTATUS(st) == EXIT_RESTART) power(1);
            if (WIFEXITED(st) && WEXITSTATUS(st) == EXIT_SHUTDOWN) power(0);
            say("W12:ERROR compositor exited (status %d), restarting", st);
            if (++crashes > 5) sleep(2);
            comp = spawn("/bin/flx-compositor");
        } else if (p == net) {
            say("init: netd exited (%d), restarting", st);
            sleep(1);
            net = spawn("/bin/flx-netd");
        } else if (p == dev) {
            say("init: deviced exited (%d), restarting", st);
            sleep(1);
            dev = spawn("/bin/flx-deviced");
        } else if (p == auth) {
            say("init: authd exited (%d), restarting", st);
            sleep(1);
            auth = spawn("/bin/flx-authd");
        }
    }
}
