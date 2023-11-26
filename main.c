#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unistd.h>
#include <sched.h>

#include "mysock.h"
#include "srv.h"
#include "util.h"
#include <linux/sched.h>

int main(int argc, char **argv) {
  printf("Misha's webserver (re) started!\n");
  struct group *grp = NULL;
  struct passwd *pwd = NULL;
  struct rlimit rlim;
  struct server srv = {
      .doc_idx = "index.html",
  };
  size_t i;
  int in_socket, status = 0;
  const char *err;
  char *tok[4];

  if (argc != 4) {
    die("usage: sudo misha_server 8080 run_as_user ./path/to/serve/dir\n");
  }

  srv.port = argv[1];
  char *user = argv[2];
  char *group = argv[2];

  size_t nthreads = 8;
  size_t nslots = 1;
  char *servedir = argv[3];

  srv.list_directories = 1;
  srv.host = "0.0.0.0";

  rlim.rlim_cur = rlim.rlim_max =
      3 + nthreads + nthreads * nslots + 5 * nthreads;

  in_socket = create_socket(srv.host, srv.port);
  if (unblock_socket(in_socket)) {
    return 1;
  }

  errno = 0;
  if (!user || !(pwd = getpwnam(user)))
  {
    die("getpwnam '%s': %s", user ? user : "null",
        errno ? strerror(errno) : "Entry not found");
  }
  errno = 0;
  if (!group || !(grp = getgrnam(group)))
  {
    die("getgrnam '%s': %s", group ? group : "null",
        errno ? strerror(errno) : "Entry not found");
  }

  if (chdir(servedir) < 0) {
    die("chdir '%s':", servedir);
  }

  // Set a new root directory
  if (chroot(".") < 0) {
      die("chroot");
  }

  /* drop root */
  if (pwd->pw_uid == 0 || grp->gr_gid == 0)
  {
    die("Won't run under root %s for obvious reasons",
        (pwd->pw_uid == 0) ? (grp->gr_gid == 0) ? "user and group" : "user"
                            : "group");
  }

  if (setgroups(1, &(grp->gr_gid)) < 0)
  {
    if (errno == EPERM)
    {
      die("You need to run as root or have "
          "CAP_SETGID set");
    }
    else
    {
      die("setgroups:");
    }
  }
  if (setgid(grp->gr_gid) < 0)
  {
    if (errno == EPERM)
    {
      die("You need to run as root or have "
          "CAP_SETGID set");
    }
    else
    {
      die("setgid:");
    }
  }
  if (setuid(pwd->pw_uid) < 0)
  {
    if (errno == EPERM)
    {
      die("You need to run as root or have "
          "CAP_SETUID set");
    }
    else
    {
      die("setuid:");
    }
  }


  /* accept incoming connections */
  init_thread_pool_for_server(in_socket, nthreads, nslots, &srv);
  return status;
}
