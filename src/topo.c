/*
 * topo.c — Costruisce la topologia:
 *
 *   [ns_client] veth0 <---> veth1 [ns_router] veth2 <---> veth3 [ns_server]
 *   10.0.1.2/24              10.0.1.1/24  10.0.2.1/24              10.0.2.2/24
 *
 * Tutto via socket Netlink (NETLINK_ROUTE), senza chiamate a system().
 * Richiede: sudo o CAP_NET_ADMIN + CAP_SYS_ADMIN
 *
 * Compile: gcc -Wall -o topo topo.c
 * Run:     sudo ./topo
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

/* Syscall */
#include <sched.h>          /* unshare, clone, CLONE_NEWNET */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

/* Netlink / rtnetlink */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/if_addr.h>
#include <linux/veth.h>
#include <linux/sockios.h>

/* Arpa per inet_pton */
#include <arpa/inet.h>

/* Costanti */
#define NETNS_RUN_DIR   "/run/netns"
#define NS_CLIENT       "/run/netns/ns_client"
#define NS_ROUTER       "/run/netns/ns_router"
#define NS_SERVER       "/run/netns/ns_server"

#define NL_BUFSIZE      8192

/* Helpers Netlink */

/*
 * nl_open() — Apre un socket Netlink NETLINK_ROUTE e lo lega al processo.
 * Restituisce il file descriptor o termina con errore.
 */
static int nl_open(void) {
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) { perror("socket(NETLINK_ROUTE)"); exit(1); }

    struct sockaddr_nl sa = {
        .nl_family = AF_NETLINK,
        .nl_pid    = getpid(),
    };
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind(netlink)"); exit(1);
    }
    return fd;
}

/*
 * nl_send() — Invia un messaggio Netlink e attende la risposta NLMSG_ERROR.
 * Stampa il nome dell'operazione in caso di errore.
 */
static int nl_send(int fd, struct nlmsghdr *nlh, const char *op) {
    /* Invia */
    struct iovec iov  = { nlh, nlh->nlmsg_len };
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
    if (sendmsg(fd, &msg, 0) < 0) {
        fprintf(stderr, "[%s] sendmsg: %s\n", op, strerror(errno));
        return -1;
    }

    /* Ricevi la risposta (ACK o errore) */
    char buf[NL_BUFSIZE];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n < 0) {
        fprintf(stderr, "[%s] recv: %s\n", op, strerror(errno));
        return -1;
    }

    struct nlmsghdr *rep = (struct nlmsghdr *)buf;
    if (rep->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(rep);
        if (err->error != 0) {
            fprintf(stderr, "[%s] Netlink error: %s\n", op, strerror(-err->error));
            return -1;
        }
    }
    return 0;
}

/*
 * nl_addattr() — Aggiunge un attributo rtattr in coda al messaggio Netlink.
 * buf_end indica la fine del buffer allocato (per controllo overflow).
 */
static struct rtattr *nl_addattr(struct nlmsghdr *nlh, int type,
                                  const void *data, int len) {
    int alen = RTA_LENGTH(len);
    struct rtattr *rta = (struct rtattr *)((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len  = alen;
    if (data) memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(alen);
    return rta;
}

/* nl_addattr_nested_open / close — per attributi annidati (IFLA_LINKINFO ecc.) */
static struct rtattr *nl_nest_open(struct nlmsghdr *nlh, int type) {
    return nl_addattr(nlh, type, NULL, 0);
}
static void nl_nest_close(struct nlmsghdr *nlh, struct rtattr *start) {
    start->rta_len = (char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len) - (char *)start;
}

/* Gestione dei namespace */

/*
 * netns_create() — Crea un named network namespace in /run/netns/<name>.
 *
 * Meccanismo:
 *   1. Crea un file vuoto in /run/netns/<name>  (serve come mount point)
 *   2. Con unshare(CLONE_NEWNET) crea un nuovo network namespace nel processo
 *      corrente (il processo "entra" nel nuovo ns)
 *   3. Bind-mount /proc/self/ns/net sul file creato al passo 1:
 *      questo "ancora" il namespace al filesystem anche se nessun processo
 *      ci vive dentro (altrimenti verrebbe distrutto all'uscita del processo)
 *   4. Rientra nel root namespace aprendo /proc/1/ns/net e chiamando setns()
 */
static void netns_create(const char *path) {
    /* 1. Crea il file mount-point */
    int fd = open(path, O_RDONLY | O_CREAT | O_EXCL, 0);
    if (fd < 0 && errno != EEXIST) {
        fprintf(stderr, "open(%s): %s\n", path, strerror(errno)); exit(1);
    }
    if (fd >= 0) close(fd);

    /* 2. Entra in un nuovo network namespace */
    if (unshare(CLONE_NEWNET) < 0) {
        perror("unshare(CLONE_NEWNET)"); exit(1);
    }

    /* 3. Bind-mount: ancora il ns al filesystem */
    if (mount("/proc/self/ns/net", path, "none", MS_BIND, NULL) < 0) {
        fprintf(stderr, "mount(%s): %s\n", path, strerror(errno)); exit(1);
    }

    /* 4. Torna al root namespace */
    int root_ns = open("/proc/1/ns/net", O_RDONLY);
    if (root_ns < 0) { perror("open(/proc/1/ns/net)"); exit(1); }
    if (setns(root_ns, CLONE_NEWNET) < 0) { perror("setns root"); exit(1); }
    close(root_ns);

    printf("  [OK] namespace creato: %s\n", path);
}

/*
 * netns_enter() — Entra in un namespace aprendo il file bind-mounted.
 * Restituisce il fd del root namespace (per poter tornare indietro).
 */
static int netns_enter(const char *path) {
    int root_fd = open("/proc/self/ns/net", O_RDONLY);
    if (root_fd < 0) { perror("open self ns"); exit(1); }

    int ns_fd = open(path, O_RDONLY);
    if (ns_fd < 0) {
        fprintf(stderr, "open(%s): %s\n", path, strerror(errno)); exit(1);
    }
    if (setns(ns_fd, CLONE_NEWNET) < 0) {
        fprintf(stderr, "setns(%s): %s\n", path, strerror(errno)); exit(1);
    }
    close(ns_fd);
    return root_fd;   /* il chiamante lo userà per tornare indietro */
}

static void netns_restore(int root_fd) {
    if (setns(root_fd, CLONE_NEWNET) < 0) { perror("setns restore"); exit(1); }
    close(root_fd);
}

/* Operazioni Netlink */

/*
 * nl_create_veth() - Crea una coppia veth nel namespace corrente.
 *
 * Messaggio RTM_NEWLINK con:
 *   IFLA_IFNAME     = nome interfaccia "locale"
 *   IFLA_LINKINFO
 *     IFLA_INFO_KIND = "veth"
 *     IFLA_INFO_DATA
 *       VETH_INFO_PEER
 *         ifinfomsg  (header per il peer)
 *         IFLA_IFNAME = nome del peer
 */
static void nl_create_veth(int nlfd, const char *name, const char *peer_name) {
    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlh->nlmsg_seq   = 1;

    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;

    nl_addattr(nlh, IFLA_IFNAME, name, strlen(name) + 1);

    struct rtattr *linkinfo = nl_nest_open(nlh, IFLA_LINKINFO);
    nl_addattr(nlh, IFLA_INFO_KIND, "veth", 5);

    struct rtattr *data = nl_nest_open(nlh, IFLA_INFO_DATA);

    /* Il peer è descritto come un sotto-messaggio ifinfomsg */
    struct rtattr *peer_rta = nl_nest_open(nlh, VETH_INFO_PEER);
    /* Riserva spazio per ifinfomsg del peer */
    struct ifinfomsg *peer_ifi = (struct ifinfomsg *)
        ((char *)nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    memset(peer_ifi, 0, sizeof(*peer_ifi));
    nlh->nlmsg_len += sizeof(struct ifinfomsg);
    nl_addattr(nlh, IFLA_IFNAME, peer_name, strlen(peer_name) + 1);
    nl_nest_close(nlh, peer_rta);

    nl_nest_close(nlh, data);
    nl_nest_close(nlh, linkinfo);

    char desc[64];
    snprintf(desc, sizeof(desc), "create veth %s<->%s", name, peer_name);
    if (nl_send(nlfd, nlh, desc) == 0)
        printf("  [OK] %s\n", desc);
}

/*
 * nl_iface_index() - Recupera l'ifindex di un'interfaccia tramite ioctl.
 * (Più semplice di un RTM_GETLINK per questo scopo.)
 */
static int nl_iface_index(const char *name) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket DGRAM"); exit(1); }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "SIOCGIFINDEX(%s): %s\n", name, strerror(errno));
        close(sock); return -1;
    }
    close(sock);
    return ifr.ifr_ifindex;
}

/*
 * nl_move_to_ns() — Sposta un'interfaccia in un altro namespace.
 *
 * RTM_NEWLINK con IFLA_NET_NS_FD: il kernel apre il namespace referenziato
 * dal file descriptor e vi sposta l'interfaccia.
 */
static void nl_move_to_ns(int nlfd, const char *ifname, const char *ns_path) {
    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    int ifindex = nl_iface_index(ifname);
    if (ifindex < 0) return;

    int ns_fd = open(ns_path, O_RDONLY);
    if (ns_fd < 0) {
        fprintf(stderr, "open(%s): %s\n", ns_path, strerror(errno)); return;
    }

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq   = 2;

    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index  = ifindex;

    nl_addattr(nlh, IFLA_NET_NS_FD, &ns_fd, sizeof(ns_fd));

    char desc[64];
    snprintf(desc, sizeof(desc), "move %s -> %s", ifname, ns_path);
    if (nl_send(nlfd, nlh, desc) == 0)
        printf("  [OK] %s\n", desc);

    close(ns_fd);
}

/*
 * nl_set_addr() — Assegna un indirizzo IPv4 a un'interfaccia.
 *
 * RTM_NEWADDR con:
 *   ifa_prefixlen = lunghezza del prefisso (es. 24)
 *   IFA_LOCAL     = indirizzo IP (in_addr)
 *   IFA_ADDRESS   = stesso valore (per IPv4 sono uguali)
 */
static void nl_set_addr(int nlfd, const char *ifname,
                         const char *ip, int prefix) {
    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    int ifindex = nl_iface_index(ifname);
    if (ifindex < 0) return;

    struct in_addr addr;
    inet_pton(AF_INET, ip, &addr);

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    nlh->nlmsg_type  = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
    nlh->nlmsg_seq   = 3;

    struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
    ifa->ifa_family    = AF_INET;
    ifa->ifa_prefixlen = prefix;
    ifa->ifa_scope     = RT_SCOPE_UNIVERSE;
    ifa->ifa_index     = ifindex;

    nl_addattr(nlh, IFA_LOCAL,   &addr, sizeof(addr));
    nl_addattr(nlh, IFA_ADDRESS, &addr, sizeof(addr));

    char desc[64];
    snprintf(desc, sizeof(desc), "addr %s/%d on %s", ip, prefix, ifname);
    if (nl_send(nlfd, nlh, desc) == 0)
        printf("  [OK] %s\n", desc);
}

/*
 * nl_set_up() — Porta un'interfaccia nello stato UP (equivale a "ip link set X up").
 *
 * RTM_NEWLINK con ifi_flags |= IFF_UP e ifi_change = IFF_UP:
 * ifi_change è una maschera che dice al kernel quali bit di ifi_flags
 * considerare. Senza ifi_change corretto il kernel ignora la richiesta.
 */
static void nl_set_up(int nlfd, const char *ifname) {
    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    int ifindex = nl_iface_index(ifname);
    if (ifindex < 0) return;

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq   = 4;

    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index  = ifindex;
    ifi->ifi_flags  = IFF_UP;
    ifi->ifi_change = IFF_UP;   /* maschera: solo IFF_UP viene modificato */

    char desc[64];
    snprintf(desc, sizeof(desc), "set UP: %s", ifname);
    if (nl_send(nlfd, nlh, desc) == 0)
        printf("  [OK] %s\n", desc);
}

/*
 * nl_add_route() — Aggiunge una route alla tabella di routing.
 *
 * RTM_NEWROUTE con:
 *   rtm_dst_len  = prefisso destinazione (0 = default route)
 *   RTA_GATEWAY  = indirizzo del next-hop
 *   RTA_OIF      = ifindex dell'interfaccia di uscita
 *
 * Per la default route (0.0.0.0/0) dst è NULL e rtm_dst_len = 0.
 */
static void nl_add_route(int nlfd, const char *dst_ip, int dst_prefix,
                          const char *gw_ip, const char *ifname) {
    char buf[NL_BUFSIZE];
    memset(buf, 0, sizeof(buf));

    int ifindex = nl_iface_index(ifname);
    if (ifindex < 0) return;

    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlh->nlmsg_type  = RTM_NEWROUTE;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
    nlh->nlmsg_seq   = 5;

    struct rtmsg *rtm = NLMSG_DATA(nlh);
    rtm->rtm_family   = AF_INET;
    rtm->rtm_dst_len  = dst_prefix;
    rtm->rtm_table    = RT_TABLE_MAIN;
    rtm->rtm_protocol = RTPROT_STATIC;
    rtm->rtm_scope    = RT_SCOPE_UNIVERSE;
    rtm->rtm_type     = RTN_UNICAST;

    if (dst_ip) {
        struct in_addr dst;
        inet_pton(AF_INET, dst_ip, &dst);
        nl_addattr(nlh, RTA_DST, &dst, sizeof(dst));
    }

    struct in_addr gw;
    inet_pton(AF_INET, gw_ip, &gw);
    nl_addattr(nlh, RTA_GATEWAY, &gw, sizeof(gw));
    nl_addattr(nlh, RTA_OIF, &ifindex, sizeof(ifindex));

    char desc[80];
    snprintf(desc, sizeof(desc), "route %s/%d via %s dev %s",
             dst_ip ? dst_ip : "default", dst_prefix, gw_ip, ifname);
    if (nl_send(nlfd, nlh, desc) == 0)
        printf("  [OK] %s\n", desc);
}

/*
 * nl_ip_forward() — Abilita ip_forward nel namespace corrente scrivendo
 * direttamente in /proc/sys/net/ipv4/ip_forward.
 * (Non esiste un'interfaccia Netlink per questo parametro sysctl.)
 */
static void nl_ip_forward(void) {
    int fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY);
    if (fd < 0) { perror("open ip_forward"); return; }
    if (write(fd, "1\n", 2) < 0) { perror("write ip_forward"); }
    else printf("  [OK] ip_forward = 1\n");
    close(fd);
}

/* Main */

int main(void) {
    /* Assicura che /run/netns esista */
    mkdir(NETNS_RUN_DIR, 0755);

    /* 1. Crea i tre namespace */
    printf("\n=== Creazione namespace ===\n");
    netns_create(NS_CLIENT);
    netns_create(NS_ROUTER);
    netns_create(NS_SERVER);

    /* 2. Nel root namespace: crea le coppie veth */
    printf("\n=== Creazione coppie veth (root ns) ===\n");
    int nlfd = nl_open();
    nl_create_veth(nlfd, "veth0", "veth1");   /* coppia A */
    nl_create_veth(nlfd, "veth2", "veth3");   /* coppia B */

    /* 3. Sposta le interfacce nei namespace giusti */
    printf("\n=== Spostamento interfacce ===\n");
    nl_move_to_ns(nlfd, "veth0", NS_CLIENT);
    nl_move_to_ns(nlfd, "veth1", NS_ROUTER);
    nl_move_to_ns(nlfd, "veth2", NS_ROUTER);
    nl_move_to_ns(nlfd, "veth3", NS_SERVER);
    close(nlfd);

    /* 4. Configura ns_client */
    printf("\n=== Configurazione ns_client ===\n");
    int root_fd = netns_enter(NS_CLIENT);
    nlfd = nl_open();
    nl_set_addr(nlfd, "veth0", "10.0.1.2", 24);
    nl_set_up(nlfd, "veth0");
    nl_set_up(nlfd, "lo");
    nl_add_route(nlfd, NULL, 0, "10.0.1.1", "veth0");  /* default gw */
    close(nlfd);
    netns_restore(root_fd);

    /* 5. Configura ns_router */
    printf("\n=== Configurazione ns_router ===\n");
    root_fd = netns_enter(NS_ROUTER);
    nlfd = nl_open();
    nl_set_addr(nlfd, "veth1", "10.0.1.1", 24);
    nl_set_addr(nlfd, "veth2", "10.0.2.1", 24);
    nl_set_up(nlfd, "veth1");
    nl_set_up(nlfd, "veth2");
    nl_set_up(nlfd, "lo");
    nl_ip_forward();          /* abilita routing tra le interfacce */
    close(nlfd);
    netns_restore(root_fd);

    /* 6. Configura ns_server */
    printf("\n=== Configurazione ns_server ===\n");
    root_fd = netns_enter(NS_SERVER);
    nlfd = nl_open();
    nl_set_addr(nlfd, "veth3", "10.0.2.2", 24);
    nl_set_up(nlfd, "veth3");
    nl_set_up(nlfd, "lo");
    nl_add_route(nlfd, NULL, 0, "10.0.2.1", "veth3");  /* default gw */
    close(nlfd);
    netns_restore(root_fd);

    printf("\n=== Topologia pronta ===\n");
    printf("Apri un altro terminale e prova:\n");
    printf("  ip netns exec ns_client ping -c3 10.0.2.2\n");
    printf("  ip netns exec ns_client ip route show\n");
    printf("  ip netns exec ns_router ip route show table local\n");
    printf("\nPremi INVIO per smontare la topologia...\n");
    getchar();

    /* -- Teardown: smonta i namespace in ordine inverso ----------------
     * umount() rimuove il bind-mount; se nessun processo vive nel
     * namespace, il kernel lo distrugge automaticamente insieme a
     * tutte le interfacce che ci vivono dentro.
     * unlink() rimuove il file vuoto rimasto in /run/netns/. */
    printf("\n=== Smontaggio topologia ===\n");

    if (umount(NS_CLIENT) == 0) printf("  [OK] umount %s\n", NS_CLIENT);
    else fprintf(stderr, "  [!!] umount %s: %s\n", NS_CLIENT, strerror(errno));
    if (unlink(NS_CLIENT) == 0) printf("  [OK] unlink %s\n", NS_CLIENT);

    if (umount(NS_ROUTER) == 0) printf("  [OK] umount %s\n", NS_ROUTER);
    else fprintf(stderr, "  [!!] umount %s: %s\n", NS_ROUTER, strerror(errno));
    if (unlink(NS_ROUTER) == 0) printf("  [OK] unlink %s\n", NS_ROUTER);

    if (umount(NS_SERVER) == 0) printf("  [OK] umount %s\n", NS_SERVER);
    else fprintf(stderr, "  [!!] umount %s: %s\n", NS_SERVER, strerror(errno));
    if (unlink(NS_SERVER) == 0) printf("  [OK] unlink %s\n", NS_SERVER);

    printf("=== Fatto. ===\n");
    return 0;
}
