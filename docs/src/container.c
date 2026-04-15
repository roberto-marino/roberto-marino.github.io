/*
 * container.c — Container from scratch in C
 *
 * Usa: Linux namespaces, cgroups v2, pivot_root, mount
 *
 * Compila: gcc -o container container.c
 * Esegui:  sudo ./container <rootfs_path> <cmd> [args...]
 *
 * Esempio:
 *   # scarica un rootfs Alpine minimale
 *   mkdir rootfs && cd rootfs
 *   curl -sL https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.0-x86_64.tar.gz | tar xz
 *   cd ..
 *   sudo ./container ./rootfs /bin/sh
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>          /* clone(), CLONE_NEW*              */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/socket.h>     /* socket(), AF_NETLINK             */
#include <sys/ioctl.h>      /* SIOCGIFINDEX, SIOCSIFFLAGS       */
#include <net/if.h>         /* struct ifreq, IFF_UP             */
#include <linux/rtnetlink.h>/* RTM_NEWLINK, RTM_NEWADDR, IFLA_* */
#include <linux/if_link.h>  /* IFLA_INFO_KIND, IFLA_INFO_DATA   */
#include <linux/if_addr.h>  /* IFA_LOCAL, IFA_ADDRESS           */
#include <linux/if_ether.h> /* ETH_ALEN                         */
#include <linux/veth.h>     /* VETH_INFO_PEER                   */
#include <sys/sysmacros.h>  /* makedev()                        */
#include <arpa/inet.h>      /* inet_pton()                      */

/* ------------------------------------------------------------------ */
/* Parametri container                                                  */
/* ------------------------------------------------------------------ */
#define STACK_SIZE      (1024 * 1024)   /* 1 MB stack per il child    */
#define CGROUP_BASE     "/sys/fs/cgroup/container_demo"
#define HOSTNAME        "container"
#define MEM_LIMIT       "67108864"      /* 64 MB                       */
#define PIDS_MAX        "32"

/* Nomi e indirizzi delle interfacce veth */
#define VETH_HOST       "veth0"         /* lato host                   */
#define VETH_CONT       "veth1"         /* lato container              */
#define IP_HOST         "172.17.0.1"    /* IP assegnato a veth0        */
#define IP_CONT         "172.17.0.2"    /* IP assegnato a veth1        */
#define IP_PREFIX       24              /* /24 = 255.255.255.0         */

/* ------------------------------------------------------------------ */
/* Struttura passata al processo figlio tramite clone()                 */
/* ------------------------------------------------------------------ */
typedef struct {
    char *rootfs;       /* path del rootfs                             */
    char **argv;        /* comando da eseguire dentro il container     */
    int   sync_pipe[2]; /* pipe di sincronizzazione parent -> child    */
                        /* [0] = read end (child), [1] = write end (parent) */
} child_args_t;

/* ================================================================== */
/* NETLINK — veth pair                                                  */
/*                                                                      */
/* Netlink è il canale kernel↔userspace per configurare la rete.       */
/* Si apre un socket AF_NETLINK e si inviano messaggi strutturati       */
/* (struct nlmsghdr + payload) invece di chiamare `ip link`.           */
/* ================================================================== */

/*
 * Buffer per messaggi Netlink. Dimensione generosa per evitare
 * troncature sui payload con molti attributi annidati.
 */
#define NL_BUFSIZE 4096

/*
 * nl_open() — apre un socket Netlink ROUTE e lo lega al kernel.
 * NETLINK_ROUTE gestisce interfacce, indirizzi, route.
 */
static int nl_open(void) {
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) {
        perror("socket(AF_NETLINK)");
        return -1;
    }
    struct sockaddr_nl sa = {
        .nl_family = AF_NETLINK,
        .nl_pid    = 0,     /* 0 = kernel */
        .nl_groups = 0,
    };
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind(AF_NETLINK)");
        close(fd);
        return -1;
    }
    return fd;
}

/*
 * nl_send() — invia un messaggio Netlink e aspetta l'ACK del kernel.
 *
 * Il kernel risponde sempre con NLMSG_ERROR: se nlmsg_error.error == 0
 * l'operazione è riuscita; altrimenti è il negativo di errno.
 */
static int nl_send(int fd, struct nlmsghdr *nlh) {
    /* Invia il messaggio al kernel */
    if (send(fd, nlh, nlh->nlmsg_len, 0) < 0) {
        perror("send(netlink)");
        return -1;
    }

    /* Ricevi la risposta (ACK o errore) */
    char buf[NL_BUFSIZE];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n < 0) {
        perror("recv(netlink)");
        return -1;
    }

    struct nlmsghdr *resp = (struct nlmsghdr *)buf;
    if (resp->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = NLMSG_DATA(resp);
        if (err->error != 0) {
            errno = -err->error;
            return -1;
        }
    }
    return 0;
}

/*
 * nl_addattr() — aggiunge un attributo RTA (rtattr) al messaggio.
 *
 * Gli attributi Netlink sono TLV (Type, Length, Value) appesi in coda
 * all'header principale. Questa funzione scrive:
 *   [ rta_len | rta_type | data ]
 * e aggiorna nlmsg_len.
 */
static struct rtattr *nl_addattr(struct nlmsghdr *nlh, int type,
                                  const void *data, int data_len) {
    /* L'attributo va allineato a 4 byte (RTA_ALIGN) */
    int attr_len = RTA_LENGTH(data_len);
    struct rtattr *rta = (struct rtattr *)
        ((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len  = attr_len;
    if (data && data_len > 0)
        memcpy(RTA_DATA(rta), data, data_len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(attr_len);
    return rta;
}

/* ------------------------------------------------------------------ */
/* veth_create() — crea la coppia veth0 <--> veth1 via Netlink        */
/*                                                                      */
/* Struttura del messaggio RTM_NEWLINK per creare una veth pair:        */
/*                                                                      */
/*  nlmsghdr                                                            */
/*  ifinfomsg  (IFLA_IFNAME = "veth0")                                 */
/*  └── IFLA_LINKINFO                                                   */
/*      ├── IFLA_INFO_KIND = "veth"                                     */
/*      └── IFLA_INFO_DATA                                              */
/*          └── VETH_INFO_PEER                                          */
/*              ├── ifinfomsg                                            */
/*              └── IFLA_IFNAME = "veth1"                               */
/* ------------------------------------------------------------------ */
static int veth_create(int nl_fd) {
    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlh->nlmsg_seq   = 1;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));

    /* ifinfomsg: informazioni base sull'interfaccia (AF_UNSPEC = generica) */
    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;

    /* IFLA_IFNAME: nome del lato host ("veth0") */
    nl_addattr(nlh, IFLA_IFNAME, VETH_HOST, strlen(VETH_HOST) + 1);

    /* IFLA_LINKINFO: contenitore per il tipo di link e i suoi parametri */
    struct rtattr *linkinfo = nl_addattr(nlh, IFLA_LINKINFO, NULL, 0);

    /* IFLA_INFO_KIND: tipo = "veth" */
    nl_addattr(nlh, IFLA_INFO_KIND, "veth", strlen("veth") + 1);

    /* IFLA_INFO_DATA: parametri specifici del tipo veth */
    struct rtattr *info_data = nl_addattr(nlh, IFLA_INFO_DATA, NULL, 0);

    /*
     * VETH_INFO_PEER: descrive l'altra estremità della coppia.
     * Il payload è un ifinfomsg seguito dagli attributi del peer.
     */
    struct rtattr *peer_rta = nl_addattr(nlh, VETH_INFO_PEER, NULL, 0);
    /* ifinfomsg del peer (obbligatorio ma può essere tutto zero) */
    struct ifinfomsg peer_ifi = { .ifi_family = AF_UNSPEC };
    int peer_ifi_start = nlh->nlmsg_len;
    nlh->nlmsg_len += NLMSG_ALIGN(sizeof(peer_ifi));
    memcpy((char *)nlh + peer_ifi_start, &peer_ifi, sizeof(peer_ifi));

    /* IFLA_IFNAME del peer: "veth1" */
    nl_addattr(nlh, IFLA_IFNAME, VETH_CONT, strlen(VETH_CONT) + 1);

    /* Chiudi gli rtattr annidati aggiornando le loro lunghezze */
    peer_rta->rta_len  = (char *)nlh + nlh->nlmsg_len - (char *)peer_rta;
    info_data->rta_len = (char *)nlh + nlh->nlmsg_len - (char *)info_data;
    linkinfo->rta_len  = (char *)nlh + nlh->nlmsg_len - (char *)linkinfo;

    if (nl_send(nl_fd, nlh) < 0) {
        fprintf(stderr, "[-] veth_create: %s\n", strerror(errno));
        return -1;
    }
    printf("[+] veth pair creata: %s <--> %s\n", VETH_HOST, VETH_CONT);
    return 0;
}

/* ------------------------------------------------------------------ */
/* veth_move_to_ns() — sposta veth1 nel net namespace del container   */
/*                                                                      */
/* RTM_NEWLINK con IFLA_NET_NS_PID dice al kernel:                     */
/* "sposta questa interfaccia nel net ns del processo <pid>"           */
/* ------------------------------------------------------------------ */
static int veth_move_to_ns(int nl_fd, pid_t child_pid) {
    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq   = 2;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));

    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;

    /* Identifica l'interfaccia da spostare per nome */
    nl_addattr(nlh, IFLA_IFNAME, VETH_CONT, strlen(VETH_CONT) + 1);

    /* IFLA_NET_NS_PID: PID del processo nel cui net ns spostare l'iface */
    uint32_t pid = (uint32_t)child_pid;
    nl_addattr(nlh, IFLA_NET_NS_PID, &pid, sizeof(pid));

    if (nl_send(nl_fd, nlh) < 0) {
        fprintf(stderr, "[-] veth_move_to_ns: %s\n", strerror(errno));
        return -1;
    }
    printf("[+] %s spostata nel net ns del container (PID %d)\n",
           VETH_CONT, child_pid);
    return 0;
}

/* ------------------------------------------------------------------ */
/* if_set_up() — porta un'interfaccia nello stato UP via ioctl         */
/*                                                                      */
/* Usiamo ioctl(SIOCSIFFLAGS) invece di Netlink perché è più semplice  */
/* per questa operazione atomica (è ciò che fa "ip link set X up").    */
/* ------------------------------------------------------------------ */
static int if_set_up(const char *ifname) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket(ioctl)"); return -1; }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    /* Leggi i flag attuali */
    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        fprintf(stderr, "[-] SIOCGIFFLAGS(%s): %s\n", ifname, strerror(errno));
        close(sock);
        return -1;
    }
    /* Aggiunge il flag IFF_UP e riscrivi */
    ifr.ifr_flags |= IFF_UP;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        fprintf(stderr, "[-] SIOCSIFFLAGS(%s): %s\n", ifname, strerror(errno));
        close(sock);
        return -1;
    }
    close(sock);
    printf("[+] interfaccia %s: UP\n", ifname);
    return 0;
}

/* ------------------------------------------------------------------ */
/* if_add_addr() — assegna un indirizzo IPv4 a un'interfaccia          */
/*                                                                      */
/* RTM_NEWADDR con IFA_LOCAL (indirizzo locale) e IFA_ADDRESS          */
/* (indirizzo di broadcast/peer per point-to-point).                   */
/* ------------------------------------------------------------------ */
static int if_add_addr(int nl_fd, const char *ifname,
                        const char *ip_str, int prefix) {
    /* Risolvi il nome dell'interfaccia in indice numerico */
    unsigned int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        fprintf(stderr, "[-] if_nametoindex(%s): %s\n", ifname, strerror(errno));
        return -1;
    }

    /* Converti l'IP stringa in binario */
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str, &addr) != 1) {
        fprintf(stderr, "[-] inet_pton(%s): formato non valido\n", ip_str);
        return -1;
    }

    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type  = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
    nlh->nlmsg_seq   = 3;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));

    /*
     * ifaddrmsg: header degli indirizzi.
     * ifa_prefixlen = lunghezza del prefisso (es. 24 per /24)
     * ifa_index     = indice dell'interfaccia
     */
    struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
    ifa->ifa_family    = AF_INET;
    ifa->ifa_prefixlen = prefix;
    ifa->ifa_flags     = 0;
    ifa->ifa_scope     = RT_SCOPE_UNIVERSE;
    ifa->ifa_index     = ifindex;

    /* IFA_LOCAL: indirizzo locale dell'interfaccia */
    nl_addattr(nlh, IFA_LOCAL,   &addr.s_addr, sizeof(addr.s_addr));
    /* IFA_ADDRESS: su punto-punto coincide con IFA_LOCAL */
    nl_addattr(nlh, IFA_ADDRESS, &addr.s_addr, sizeof(addr.s_addr));

    if (nl_send(nl_fd, nlh) < 0) {
        fprintf(stderr, "[-] if_add_addr(%s, %s): %s\n",
                ifname, ip_str, strerror(errno));
        return -1;
    }
    printf("[+] %s: indirizzo %s/%d assegnato\n", ifname, ip_str, prefix);
    return 0;
}

/* ------------------------------------------------------------------ */
/* setup_veth() — orchestrazione completa della rete lato host        */
/*                                                                      */
/* Chiamata dal parent DOPO clone() e DOPO che il child è in esecuzione*/
/* (ma prima di waitpid). Il child configura il suo lato in            */
/* setup_container_net(), chiamata dentro child_fn().                  */
/* ------------------------------------------------------------------ */
static int setup_veth(pid_t child_pid) {
    int nl_fd = nl_open();
    if (nl_fd < 0) return -1;

    /*
     * Passo 1: crea la coppia veth0 <--> veth1 nel net ns dell'host.
     * Entrambe le interfacce nascono nell'host ns.
     */
    if (veth_create(nl_fd) < 0)   { close(nl_fd); return -1; }

    /*
     * Passo 2: sposta veth1 nel net ns del container.
     * Da questo momento veth1 è invisibile all'host e visibile
     * solo dentro il container.
     */
    if (veth_move_to_ns(nl_fd, child_pid) < 0) { close(nl_fd); return -1; }

    /*
     * Passo 3: porta veth0 (lato host) nello stato UP.
     * Solo dopo UP l'interfaccia può trasmettere/ricevere frame.
     */
    if (if_set_up(VETH_HOST) < 0) { close(nl_fd); return -1; }

    /*
     * Passo 4: assegna l'IP 172.17.0.1/24 a veth0.
     * Il kernel crea automaticamente la route 172.17.0.0/24 via veth0.
     */
    if (if_add_addr(nl_fd, VETH_HOST, IP_HOST, IP_PREFIX) < 0) {
        close(nl_fd); return -1;
    }

    close(nl_fd);
    printf("[+] rete host configurata: %s=%s/%d\n",
           VETH_HOST, IP_HOST, IP_PREFIX);
    return 0;
}

/* ------------------------------------------------------------------ */
/* setup_container_net() — configura la rete DENTRO il container      */
/*                                                                      */
/* Chiamata da child_fn() DOPO pivot_root, quando siamo già dentro     */
/* il net namespace del container. veth1 è già presente qui (spostata  */
/* da veth_move_to_ns). Dobbiamo solo alzarla e darle un IP.           */
/* ------------------------------------------------------------------ */
static int setup_container_net(void) {
    int nl_fd = nl_open();
    if (nl_fd < 0) return -1;

    /* Porta il loopback UP (necessario per molte applicazioni) */
    if (if_set_up("lo") < 0) {
        close(nl_fd); return -1;
    }

    /* Porta veth1 UP */
    if (if_set_up(VETH_CONT) < 0) {
        close(nl_fd); return -1;
    }

    /* Assegna 172.17.0.2/24 a veth1 */
    if (if_add_addr(nl_fd, VETH_CONT, IP_CONT, IP_PREFIX) < 0) {
        close(nl_fd); return -1;
    }

    close(nl_fd);
    printf("[+] rete container: %s=%s/%d\n", VETH_CONT, IP_CONT, IP_PREFIX);
    return 0;
}

/* ------------------------------------------------------------------ */
/* cleanup_veth() — rimuove veth0 (e quindi anche veth1) alla fine    */
/* ------------------------------------------------------------------ */
static void cleanup_veth(void) {
    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    int nl_fd = nl_open();
    if (nl_fd < 0) return;

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_type  = RTM_DELLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq   = 99;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));

    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;
    nl_addattr(nlh, IFLA_IFNAME, VETH_HOST, strlen(VETH_HOST) + 1);

    nl_send(nl_fd, nlh);   /* ignora errori in cleanup */
    close(nl_fd);
    printf("[*] veth pair rimossa\n");
}

/* ------------------------------------------------------------------ */
/* Utility: scrive una stringa in un file sysfs/cgroup                 */
/* ------------------------------------------------------------------ */
static int write_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        fprintf(stderr, "[-] open(%s): %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t n = write(fd, value, strlen(value));
    close(fd);
    if (n < 0) {
        fprintf(stderr, "[-] write(%s, %s): %s\n", path, value, strerror(errno));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Configura cgroup v2 per limitare memoria, CPU e PID                 */
/* ------------------------------------------------------------------ */
static int setup_cgroup(pid_t child_pid) {
    char path[256];
    char pid_str[32];

    /* Crea la directory del cgroup */
    if (mkdir(CGROUP_BASE, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "[-] mkdir cgroup: %s\n", strerror(errno));
        return -1;
    }

    /* Abilita i controller nel parent cgroup.
     * In cgroups v2 i controller vanno abilitati nel cgroup padre. */
    if (write_file("/sys/fs/cgroup/cgroup.subtree_control",
                   "+memory +pids +cpu") < 0) {
        /* Ignora errori: alcuni kernel o configurazioni non lo richiedono */
        fprintf(stderr, "[!] cgroup.subtree_control: warning (potrebbe essere ok)\n");
    }

    /* Limite memoria: 64 MB */
    snprintf(path, sizeof(path), CGROUP_BASE "/memory.max");
    write_file(path, MEM_LIMIT);

    /* Limite swap: 0 */
    snprintf(path, sizeof(path), CGROUP_BASE "/memory.swap.max");
    write_file(path, "0");

    /* Limite processi: 32 */
    snprintf(path, sizeof(path), CGROUP_BASE "/pids.max");
    write_file(path, PIDS_MAX);

    /* Scrivi il PID del figlio nel cgroup */
    snprintf(path, sizeof(path), CGROUP_BASE "/cgroup.procs");
    snprintf(pid_str, sizeof(pid_str), "%d", child_pid);
    if (write_file(path, pid_str) < 0)
        return -1;

    printf("[+] cgroup configurato: mem=%s bytes, pids.max=%s\n",
           MEM_LIMIT, PIDS_MAX);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Rimuove il cgroup dopo la fine del container                        */
/* ------------------------------------------------------------------ */
static void cleanup_cgroup(void) {
    if (rmdir(CGROUP_BASE) < 0 && errno != ENOENT)
        fprintf(stderr, "[!] rmdir cgroup: %s\n", strerror(errno));
}

/* ------------------------------------------------------------------ */
/* pivot_root: cambia il root filesystem del container                 */
/*                                                                     */
/* pivot_root(new_root, put_old) sposta il vecchio root in put_old     */
/* e monta new_root come nuovo /. Poi smonta il vecchio root.          */
/* Molto più robusto di chroot() perché non è aggirabile.              */
/* ------------------------------------------------------------------ */
static int pivot_root_to(const char *new_root) {
    char old_root[256];
    snprintf(old_root, sizeof(old_root), "%s/.pivot_old", new_root);

    /* Il new_root deve essere un mount point */
    if (mount(new_root, new_root, NULL, MS_BIND | MS_REC, NULL) < 0) {
        fprintf(stderr, "[-] mount bind rootfs: %s\n", strerror(errno));
        return -1;
    }

    if (mkdir(old_root, 0700) < 0 && errno != EEXIST) {
        fprintf(stderr, "[-] mkdir .pivot_old: %s\n", strerror(errno));
        return -1;
    }

    /* syscall pivot_root non ha wrapper in glibc — usiamo syscall() */
    if (syscall(SYS_pivot_root, new_root, old_root) < 0) {
        fprintf(stderr, "[-] pivot_root: %s\n", strerror(errno));
        return -1;
    }

    /* Cambia la directory corrente nel nuovo root */
    if (chdir("/") < 0) {
        fprintf(stderr, "[-] chdir /: %s\n", strerror(errno));
        return -1;
    }

    /* Smonta il vecchio root (è ora in /.pivot_old) */
    if (umount2("/.pivot_old", MNT_DETACH) < 0) {
        fprintf(stderr, "[-] umount old root: %s\n", strerror(errno));
        return -1;
    }

    if (rmdir("/.pivot_old") < 0)
        fprintf(stderr, "[!] rmdir .pivot_old: %s\n", strerror(errno));

    return 0;
}

/* ------------------------------------------------------------------ */
/* Monta i filesystem virtuali necessari dentro il container           */
/* ------------------------------------------------------------------ */
static int setup_mounts(const char *rootfs) {
    char path[256];

    /* proc: necessario per ps, /proc/self, ecc. */
    snprintf(path, sizeof(path), "%s/proc", rootfs);
    mkdir(path, 0555);
    if (mount("proc", path, "proc",
              MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0) {
        fprintf(stderr, "[-] mount proc: %s\n", strerror(errno));
        return -1;
    }

    /* sys: read-only, utile per vari tool */
    snprintf(path, sizeof(path), "%s/sys", rootfs);
    mkdir(path, 0555);
    if (mount("sysfs", path, "sysfs",
              MS_NOSUID | MS_NOEXEC | MS_NODEV | MS_RDONLY, NULL) < 0) {
        /* Non fatale su tutti i kernel */
        fprintf(stderr, "[!] mount sys: %s (non fatale)\n", strerror(errno));
    }

    /* dev/pts: pseudo-terminali */
    snprintf(path, sizeof(path), "%s/dev", rootfs);
    mkdir(path, 0755);
    if (mount("tmpfs", path, "tmpfs",
              MS_NOSUID | MS_STRICTATIME,
              "mode=755,size=65536k") < 0) {
        fprintf(stderr, "[-] mount devtmpfs: %s\n", strerror(errno));
        return -1;
    }

    /* /dev/null, /dev/zero, /dev/urandom — crea i device node */
    snprintf(path, sizeof(path), "%s/dev/null", rootfs);
    mknod(path, S_IFCHR | 0666, makedev(1, 3));
    snprintf(path, sizeof(path), "%s/dev/zero", rootfs);
    mknod(path, S_IFCHR | 0666, makedev(1, 5));
    snprintf(path, sizeof(path), "%s/dev/random", rootfs);
    mknod(path, S_IFCHR | 0666, makedev(1, 8));
    snprintf(path, sizeof(path), "%s/dev/urandom", rootfs);
    mknod(path, S_IFCHR | 0666, makedev(1, 9));
    snprintf(path, sizeof(path), "%s/dev/tty", rootfs);
    mknod(path, S_IFCHR | 0666, makedev(5, 0));

    return 0;
}

/* ------------------------------------------------------------------ */
/* Funzione eseguita dal processo figlio (dentro il container)         */
/* ------------------------------------------------------------------ */
static int child_fn(void *arg) {
    child_args_t *args = (child_args_t *)arg;

    /* Il figlio muore quando il parent termina */
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    /* ----- Imposta hostname nel nuovo UTS namespace ----- */
    if (sethostname(HOSTNAME, strlen(HOSTNAME)) < 0) {
        fprintf(stderr, "[-] sethostname: %s\n", strerror(errno));
        return 1;
    }
    printf("[+] hostname impostato: %s\n", HOSTNAME);

    /* ----- Configura i mount nel nuovo mount namespace ----- */
    /*
     * Rendi il mount namespace privato: le modifiche fatte qui
     * non si propagano all'host e viceversa.
     */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        fprintf(stderr, "[-] mount private /: %s\n", strerror(errno));
        return 1;
    }

    if (setup_mounts(args->rootfs) < 0)
        return 1;

    /* ----- pivot_root: il container vede solo il suo rootfs ----- */
    if (pivot_root_to(args->rootfs) < 0)
        return 1;

    printf("[+] filesystem isolato in: %s\n", args->rootfs);

    /* ----- Monta /proc nel nuovo root (dopo pivot) ----- */
    if (mount("proc", "/proc", "proc",
              MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) < 0) {
        fprintf(stderr, "[!] mount /proc post-pivot: %s\n", strerror(errno));
    }

    /* ----- Configura la rete nel net namespace del container ----- */
    /*
     * Aspetta che il parent abbia completato setup_veth().
     *
     * Il parent scrive un byte sulla pipe dopo aver creato la veth
     * pair e spostato veth1 in questo namespace. Solo a quel punto
     * veth1 e' visibile qui dentro e possiamo alzarla.
     *
     * Senza questa sincronizzazione c'e' una race condition: il child
     * puo' arrivare qui prima che il parent abbia eseguito
     * veth_move_to_ns(), trovare veth1 inesistente e fallire.
     */
    {
        char byte;
        if (read(args->sync_pipe[0], &byte, 1) != 1) {
            fprintf(stderr, "[-] sync_pipe read: %s\n", strerror(errno));
            return 1;
        }
        close(args->sync_pipe[0]);
    }

    if (setup_container_net() < 0) {
        fprintf(stderr, "[!] rete container non configurata (continuo)\n");
    }

    /* ----- Esegui il comando richiesto ----- */
    printf("[+] avvio: %s\n\n", args->argv[0]);
    fflush(stdout);

    execvp(args->argv[0], args->argv);

    /* Se arriviamo qui, execvp è fallito */
    fprintf(stderr, "[-] execvp(%s): %s\n", args->argv[0], strerror(errno));
    return 1;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <rootfs> <cmd> [args...]\n", argv[0]);
        fprintf(stderr, "  es: sudo %s ./rootfs /bin/sh\n", argv[0]);
        return 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "[-] Richiede root (o CAP_SYS_ADMIN)\n");
        return 1;
    }

    char *rootfs = argv[1];
    char **cmd   = &argv[2];

    printf("[*] Avvio container\n");
    printf("[*] rootfs : %s\n", rootfs);
    printf("[*] comando: %s\n", cmd[0]);

    /* ---------------------------------------------------------------- */
    /* Prepara il stack e gli argomenti per il processo figlio           */
    /* ---------------------------------------------------------------- */
    /* ---------------------------------------------------------------- */
    /* Crea la pipe di sincronizzazione parent -> child                 */
    /*                                                                   */
    /* Il child attende su pipe[0] prima di configurare la rete.        */
    /* Il parent scrive su pipe[1] dopo aver completato setup_veth().   */
    /* ---------------------------------------------------------------- */
    int sync_pipe[2];
    if (pipe(sync_pipe) < 0) {
        perror("pipe");
        return 1;
    }

    child_args_t args = {
        .rootfs      = rootfs,
        .argv        = cmd,
        .sync_pipe   = { sync_pipe[0], sync_pipe[1] },
    };

    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc stack");
        return 1;
    }
    /* clone() usa il top dello stack (stack cresce verso il basso) */
    char *stack_top = stack + STACK_SIZE;

    /* ---------------------------------------------------------------- */
    /* clone(): crea il processo figlio in nuovi namespace              */
    /*                                                                   */
    /* CLONE_NEWPID  — nuovo namespace PID (il figlio è PID 1 nel ns)   */
    /* CLONE_NEWUTS  — nuovo namespace UTS (hostname indipendente)       */
    /* CLONE_NEWNS   — nuovo namespace mount                             */
    /* CLONE_NEWNET  — nuovo namespace di rete                           */
    /* CLONE_NEWIPC  — nuovo namespace IPC (semaphore, shm, ecc.)       */
    /* SIGCHLD       — segnale inviato al parent quando il figlio muore  */
    /* ---------------------------------------------------------------- */
    int flags = CLONE_NEWPID  |
                CLONE_NEWUTS  |
                CLONE_NEWNS   |
                CLONE_NEWNET  |
                CLONE_NEWIPC  |
                SIGCHLD;

    pid_t child_pid = clone(child_fn, stack_top, flags, &args);
    if (child_pid < 0) {
        fprintf(stderr, "[-] clone(): %s\n", strerror(errno));
        free(stack);
        return 1;
    }

    printf("[+] container avviato --- PID host: %d\n", child_pid);

    /* Il parent non legge dalla pipe: chiude il read end */
    close(sync_pipe[0]);

    /* ---------------------------------------------------------------- */
    /* Configura cgroup nel parent (dopo clone, prima che il figlio     */
    /* esegua execvp) per applicare i limiti di risorse                 */
    /* ---------------------------------------------------------------- */
    if (setup_cgroup(child_pid) < 0) {
        fprintf(stderr, "[!] cgroup setup fallito (continuo senza limiti)\n");
    }

    /* ---------------------------------------------------------------- */
    /* Configura la veth pair e la rete lato host                       */
    /* ---------------------------------------------------------------- */
    if (setup_veth(child_pid) < 0) {
        fprintf(stderr, "[!] veth setup fallito (rete non disponibile)\n");
    } else {
        printf("[*] Per abilitare Internet nel container:\n");
        printf("[*]   echo 1 > /proc/sys/net/ipv4/ip_forward\n");
        printf("[*]   iptables -t nat -A POSTROUTING -s 172.17.0.0/24 -o <iface> -j MASQUERADE\n");
    }

    /* ---------------------------------------------------------------- */
    /* Sblocca il child: la veth pair e' pronta                         */
    /*                                                                   */
    /* Scriviamo un byte sulla pipe. Il child e' bloccato su read()     */
    /* e si sveglia non appena riceve questo byte. Da questo momento     */
    /* veth1 e' nel suo net namespace e puo' configurarla.              */
    /* ---------------------------------------------------------------- */
    if (write(sync_pipe[1], "\x01", 1) != 1) {
        fprintf(stderr, "[-] sync_pipe write: %s\n", strerror(errno));
    }
    close(sync_pipe[1]);

    /* ---------------------------------------------------------------- */
    /* Attendi la fine del container                                     */
    /* ---------------------------------------------------------------- */
    int status;
    waitpid(child_pid, &status, 0);

    if (WIFEXITED(status))
        printf("\n[*] Container terminato — exit code: %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("\n[*] Container terminato da segnale: %d\n", WTERMSIG(status));

    /* Pulizia */
    cleanup_veth();
    cleanup_cgroup();
    free(stack);

    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
