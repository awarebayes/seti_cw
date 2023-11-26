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

#include "mysock.h"
#include "srv.h"
#include "util.h"

int main() {
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

  size_t nthreads = 8;
  size_t nslots = 1;
  char *servedir = ".";
  char *user = "root";
  char *group = "root";
  srv.list_directories = 1;
  srv.port = "80";
  srv.host = "0.0.0.0";
  rlim.rlim_cur = rlim.rlim_max =
      3 + nthreads + nthreads * nslots + 5 * nthreads;

  in_socket = create_socket(srv.host, srv.port);
  if (unblock_socket(in_socket)) {
    return 1;
  }

  if (chdir(servedir) < 0) {
    die("chdir '%s':", servedir);
  }
  if (chroot(".") < 0) {
    if (errno == EPERM) {
      die("Only root can have sys_chroot set\n");
    } else {
      die("chroot:");
    }
  }

  /* accept incoming connections */
  init_thread_pool_for_server(in_socket, nthreads, nslots, &srv);
  return status;
}
