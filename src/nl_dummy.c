/*
 * nl_dummy.c — crea un'interfaccia "dummy" via Netlink
 * =====================================================
 * COMPILAZIONE:  gcc -Wall -o nl_dummy nl_dummy.c
 * UTILIZZO:      sudo ./nl_dummy
 * VERIFICA:      ip link show dummy0
 * PULIZIA:       sudo ip link del dummy0
 *
 * Un'interfaccia dummy è la più semplice che esiste:
 * scarta tutto il traffico ricevuto, come /dev/null.
 * Utile per test, per binding di servizi, per routing.
 *
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>

/* Buffer del messaggio: header + ifinfomsg + spazio attributi */
static struct {
    struct nlmsghdr  hdr;
    struct ifinfomsg ifi;
    char             buf[256];
} msg;

/* Aggiunge un attributo rtattr al messaggio */
static struct rtattr *add_attr(int type, const void *data, int len)
{
    struct rtattr *a = (struct rtattr *)
        ((char *)&msg + NLMSG_ALIGN(msg.hdr.nlmsg_len));
    a->rta_type = type;
    a->rta_len  = RTA_LENGTH(len);
    if (data) memcpy(RTA_DATA(a), data, len);
    msg.hdr.nlmsg_len =
        NLMSG_ALIGN(msg.hdr.nlmsg_len) + RTA_ALIGN(a->rta_len);
    return a;
}

/* Chiude un attributo annidato aggiornando la sua rta_len */
static void close_attr(struct rtattr *a)
{
    a->rta_len = (char *)&msg + NLMSG_ALIGN(msg.hdr.nlmsg_len) - (char *)a;
}

int main(void)
{
    /* 1. Apri socket Netlink */
    int nl = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
    bind(nl, (struct sockaddr *)&sa, sizeof(sa));

    /* 2. Costruisci il messaggio  */
    memset(&msg, 0, sizeof(msg));
    msg.hdr.nlmsg_len   = NLMSG_LENGTH(sizeof(msg.ifi));
    msg.hdr.nlmsg_type  = RTM_NEWLINK;
    msg.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK
                        | NLM_F_CREATE  | NLM_F_EXCL;
    msg.ifi.ifi_family  = AF_UNSPEC;

    /* Nome dell'interfaccia */
    add_attr(IFLA_IFNAME, "dummy0", strlen("dummy0") + 1);

    /* Tipo: "dummy" — nessun dato extra, solo il kind */
    struct rtattr *info = add_attr(IFLA_LINKINFO, NULL, 0);
        add_attr(IFLA_INFO_KIND, "dummy", strlen("dummy") + 1);
    close_attr(info);

    /* 3. Invia al kernel */
    printf("Messaggio: %d byte\n", msg.hdr.nlmsg_len);
    send(nl, &msg, msg.hdr.nlmsg_len, 0);

    /* 4. Leggi la risposta */
    char risposta[256];
    recv(nl, risposta, sizeof(risposta), 0);
    struct nlmsghdr *h = (struct nlmsghdr *)risposta;
    struct nlmsgerr *e = (struct nlmsgerr *)NLMSG_DATA(h);

    if (h->nlmsg_type == NLMSG_ERROR && e->error != 0) {
        fprintf(stderr, "Errore: %d", -e->error);
        if (-e->error == 1)  fprintf(stderr, " (EPERM: serve sudo)");
        if (-e->error == 17) fprintf(stderr, " (EEXIST: dummy0 esiste gia')");
        fprintf(stderr, "\n");
        close(nl);
        return 1;
    }

    printf("dummy0 creata.\n");
    printf("Verifica: ip link show dummy0\n");
    printf("Pulizia:  sudo ip link del dummy0\n");

    close(nl);
    return 0;
}
