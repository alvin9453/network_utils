#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <errno.h>

#define _PATH_PROCNET_DEV "/proc/net/dev"
#define _PATH_PROCNET_IFINET6 "/proc/net/if_inet6"
#define _PATH_PROCNET_ROUTE "/proc/net/route"

#define MAX_IF_COUNT 10
#define MAX_HW_ADDR_STR_LENGTH 18

#define IPV6_ADDR_ANY 0x0000U
#define IPV6_ADDR_UNICAST 0x0001U
#define IPV6_ADDR_MULTICAST 0x0002U
#define IPV6_ADDR_ANYCAST 0x0004U
#define IPV6_ADDR_LOOPBACK 0x0010U
#define IPV6_ADDR_LINKLOCAL 0x0020U
#define IPV6_ADDR_SITELOCAL 0x0040U
#define IPV6_ADDR_COMPATv4 0x0080U
#define IPV6_ADDR_SCOPE_MASK 0x00f0U
#define IPV6_ADDR_MAPPED 0x1000U
#define IPV6_ADDR_RESERVED 0x2000U /* reserved address space */

/**
*@fn get_if_hw_addr
*@brief Get MAC address with particular interface name
*@param if_name Ethernet interface name
*@param hwaddr_ptr Pointer to MAC address buffer 
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_hw_addr(const char *if_name, char *hwaddr_ptr)
{
    struct ifreq ifr;
    int sockfd;
    unsigned char hwaddr[MAX_HW_ADDR_STR_LENGTH] = {0};
    unsigned char *sa_data;
    bzero(&ifr, sizeof(ifr));
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) < 0)
    {
        printf("%s: SIOCGIFHWADDR ioctl: %s.\n", if_name, strerror(errno));
        return EXIT_FAILURE;
    }
    sa_data = ifr.ifr_hwaddr.sa_data;
    snprintf(hwaddr, MAX_HW_ADDR_STR_LENGTH, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X", sa_data[0], sa_data[1], sa_data[2], sa_data[3], sa_data[4], sa_data[5]);
    strncpy(hwaddr_ptr, hwaddr, MAX_HW_ADDR_STR_LENGTH - 1);
    close(sockfd);

    return EXIT_SUCCESS;
}

/**
*@fn get_if_ipv4_addr
*@brief Get IPv4 address with particular interface name
*@param if_name Ethernet interface name
*@param addr Pointer to IPv4 address buffer 
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_ipv4_addr(const char *if_name, char *addr)
{
    struct ifreq ifr;
    int sockfd;
    unsigned char ip_addr[INET_ADDRSTRLEN] = {0};

    bzero(&ifr, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (ioctl(sockfd, SIOCGIFADDR, &ifr) < 0)
    {
        printf("%s: SIOCGIFADDR ioctl: %s.\n", if_name, strerror(errno));
        return EXIT_FAILURE;
    }
    inet_ntop(AF_INET, &(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), ip_addr, INET_ADDRSTRLEN);
    strncpy(addr, ip_addr, INET_ADDRSTRLEN - 1);
    close(sockfd);

    return EXIT_SUCCESS;
}

static char *skip_whitespace(char *s)
{
    while (*s == ' ' || *s == '\t')
        ++s;

    return s;
}

static char *get_name(char name[IFNAMSIZ], char *p)
{
    /* Extract NAME from nul-terminated p of the form "<whitespace>NAME:"
	 * If match is not made, set NAME to "" and return unchanged p.
	 */
    char *nameend;
    char *namestart;

    nameend = namestart = skip_whitespace(p);

    for (;;)
    {
        if ((nameend - namestart) >= IFNAMSIZ)
            break; /* interface name too large - return "" */
        if (*nameend == ':')
        {
            memcpy(name, namestart, nameend - namestart);
            name[nameend - namestart] = '\0';
            return nameend + 1;
        }
        nameend++;
        /* isspace, NUL, any control char? */
        if ((unsigned char)*nameend <= (unsigned char)' ')
            break; /* trailing ':' not found - return "" */
    }
    name[0] = '\0';
    return p;
}

/**
*@fn get_if_readlist
*@brief Get all available interface name
*@param if_names Ethernet interface names buffer 
*@param if_counts Pointer to the number of available interfaces
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_readlist(char if_names[][IFNAMSIZ], int *if_counts)
{
    FILE *fh;
    char buf[512] = {0};
    int index = 0;

    fh = fopen(_PATH_PROCNET_DEV, "r");
    if (!fh)
    {
        return EXIT_FAILURE; /* "not found" */
    }
    fgets(buf, sizeof buf, fh); /* eat line */
    fgets(buf, sizeof buf, fh);

    while (fgets(buf, sizeof buf, fh))
    {
        char name[IFNAMSIZ] = {0};
        get_name(name, buf);
        strncpy(if_names[index++], name, IFNAMSIZ - 1);
    }
    *if_counts = index;
    fclose(fh);

    return EXIT_SUCCESS;
}

/**
*@fn get_if_ipv6_info
*@brief Get IPv6 address, prefix and scope information
*@param if_name Ethernet interface name
*@param addr6_text Readable text format of IPv6 address
*@param prefix_len IPv6 address prefix
*@param addr_scope IPv6 address scope
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_ipv6_info(const char *if_name, char *addr6_text, int *prefix_len, char *addr_scope)
{
    FILE *f;
    char addr6[INET6_ADDRSTRLEN] = {0};
    char devname[IFNAMSIZ] = {0};
    char ipv6_str[INET6_ADDRSTRLEN] = {0};
    unsigned char buf[sizeof(struct in6_addr)] = {0};
    int plen, scope, dad_status, if_idx;
    char addr6p[8][5];
    int s;
    f = fopen(_PATH_PROCNET_IFINET6, "r");
    if (f == NULL)
        return EXIT_FAILURE;

    while (fscanf(f, "%4s%4s%4s%4s%4s%4s%4s%4s %08x %02x %02x %02x %20s\n",
                  addr6p[0], addr6p[1], addr6p[2], addr6p[3], addr6p[4],
                  addr6p[5], addr6p[6], addr6p[7], &if_idx, &plen, &scope,
                  &dad_status, devname) != EOF)
    {
        if (strcmp(devname, if_name) == 0)
        {
            snprintf(addr6, INET6_ADDRSTRLEN, "%s:%s:%s:%s:%s:%s:%s:%s",
                     addr6p[0], addr6p[1], addr6p[2], addr6p[3],
                     addr6p[4], addr6p[5], addr6p[6], addr6p[7]);
            s = inet_pton(AF_INET6, addr6, buf);
            if (s <= 0)
            {
                printf("inet_pton AF_INET6 error. %s.\n", strerror(errno));
                return EXIT_FAILURE;
            }
            if (inet_ntop(AF_INET6, buf, ipv6_str, INET6_ADDRSTRLEN) == NULL)
            {
                printf("inet_pton AF_INET6 error. %s.\n", strerror(errno));
                return EXIT_FAILURE;
            }

            strncpy(addr6_text, ipv6_str, INET6_ADDRSTRLEN - 1);
            *prefix_len = plen;

            switch (scope & IPV6_ADDR_SCOPE_MASK)
            {
            case 0:
                strncpy(addr_scope, "Global", 9);
                break;
            case IPV6_ADDR_LINKLOCAL:
                strncpy(addr_scope, "Link", 9);
                break;
            case IPV6_ADDR_SITELOCAL:
                strncpy(addr_scope, "Site", 9);
                break;
            case IPV6_ADDR_COMPATv4:
                strncpy(addr_scope, "Compat", 9);
                break;
            case IPV6_ADDR_LOOPBACK:
                strncpy(addr_scope, "Host", 9);
                break;
            default:
                strncpy(addr_scope, "Unknown", 9);
            }

            break;
        }
    }
    return EXIT_SUCCESS;
}

/**
*@fn get_if_mtu
*@brief Get MTU size value with particular interface name
*@param if_name Ethernet interface name
*@param mtu MTU size value
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_mtu(const char *if_name, int *mtu)
{
    struct ifreq ifr;
    int sockfd;

    bzero(&ifr, sizeof(ifr));
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_addr.sa_family = AF_INET;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (ioctl(sockfd, SIOCGIFMTU, &ifr) < 0)
    {
        printf("%s: SIOCGIFMTU ioctl: %s.\n", if_name, strerror(errno));
        return EXIT_FAILURE;
    }

    *mtu = ifr.ifr_mtu;

    close(sockfd);

    return EXIT_SUCCESS;
}

/**
*@fn get_if_mask
*@brief Get Mask address with particular interface name
*@param if_name Ethernet interface name
*@param mask Mask address value ( e.g. 255.255.255.0 )
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_mask(const char *if_name, char *mask)
{
    struct ifreq ifr;
    int sockfd;

    bzero(&ifr, sizeof(ifr));
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_addr.sa_family = AF_INET;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (ioctl(sockfd, SIOCGIFNETMASK, &ifr) < 0)
    {
        printf("%s: SIOCGIFNETMASK ioctl: %s.\n", if_name, strerror(errno));
        return EXIT_FAILURE;
    }

    strncpy(mask, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), sizeof(struct in_addr) - 1);

    close(sockfd);

    return EXIT_SUCCESS;
}

/**
*@fn get_if_speed
*@brief Get Speed value with particular interface name
*@param if_name Ethernet interface name
*@param speed Spped value in Mb/s
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_speed(const char *if_name, int *speed)
{
    struct ifreq ifr;
    struct ethtool_cmd cmd;
    int sockfd;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    cmd.cmd = ETHTOOL_GSET;
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_data = (void *)&cmd;
    if (ioctl(sockfd, SIOCETHTOOL, &ifr) < 0)
    {
        printf("%s: SIOCETHTOOL ioctl: %s.\n", if_name, strerror(errno));
        return EXIT_FAILURE;
    }

    *speed = ethtool_cmd_speed(&cmd);

    close(sockfd);

    return EXIT_SUCCESS;
}

/**
*@fn get_if_duplex
*@brief Get Duplex value with particular interface name
*@param if_name Ethernet interface name
*@param duplex Duplex flag. '0' means 'Half', '1' means 'Full'
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_duplex(const char *if_name, int *duplex)
{
    struct ifreq ifr;
    struct ethtool_cmd cmd;
    int sockfd;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    cmd.cmd = ETHTOOL_GSET;
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_data = (void *)&cmd;
    if (ioctl(sockfd, SIOCETHTOOL, &ifr) < 0)
    {
        printf("%s: SIOCETHTOOL ioctl: %s.\n", if_name, strerror(errno));
        return EXIT_FAILURE;
    }

    switch (cmd.duplex)
    {
    case DUPLEX_HALF:
        *duplex = 0;
        break;
    case DUPLEX_FULL:
        *duplex = 1;
        break;
    default:
        printf("%s: Unknown mode (0x%x).\n", if_name, cmd.duplex);
        *duplex = -1;
    }

    close(sockfd);

    return EXIT_SUCCESS;
}

/**
*@fn get_if_autoneg
*@brief Get Auto-negotiation information with particular interface name
*@param if_name Ethernet interface name
*@param autoneg Auto-negotiation flag. '0' means 'Off', '1' means 'On'
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_autoneg(const char *if_name, int *autoneg)
{
    struct ifreq ifr;
    struct ethtool_cmd cmd;
    int sockfd;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    cmd.cmd = ETHTOOL_GSET;
    strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_data = (void *)&cmd;
    if (ioctl(sockfd, SIOCETHTOOL, &ifr) < 0)
    {
        printf("%s: SIOCETHTOOL ioctl: %s.\n", if_name, strerror(errno));
        return EXIT_FAILURE;
    }
    *autoneg = cmd.autoneg;

    close(sockfd);

    return EXIT_SUCCESS;
}

/**
*@fn get_if_gateway
*@brief Get IPv4 Gateway address with particular interface name
*@param if_name Ethernet interface name
*@param gateway Gateway address in text
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_gateway(const char *if_name, char *gateway)
{
    FILE *fp;
    char buf[128];
    char iface[IFNAMSIZ] = {0};
    struct in_addr gateway_in_addr;
    unsigned long dest_addr, gate_addr;
    fp = fopen(_PATH_PROCNET_ROUTE, "r");
    if (fp == NULL)
        return EXIT_FAILURE;
    /* Skip title line */
    fgets(buf, sizeof(buf), fp);
    while (fgets(buf, sizeof(buf), fp))
    {
        if (sscanf(buf, "%s\t%lX\t%lX", iface, &dest_addr, &gate_addr) != 3 || dest_addr != 0)
            continue;

        if (strcmp(if_name, iface) == 0)
        {
            gateway_in_addr.s_addr = gate_addr;
            strncpy(gateway, inet_ntoa(gateway_in_addr), INET_ADDRSTRLEN - 1);
            break;
        }
    }

    return EXIT_SUCCESS;
}

/**
*@fn get_hostname
*@brief Get system hostname
*@param hostname Hostname
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_hostname(char *hostname)
{
    char hostname_buf[HOST_NAME_MAX + 1] = {0};
    if (gethostname(hostname_buf, HOST_NAME_MAX + 1) == 0)
    {
        strncpy(hostname, hostname_buf, HOST_NAME_MAX - 1);
        return EXIT_SUCCESS;
    }
    else
    {
        return EXIT_FAILURE;
    }
}

/**
*@fn get_if_ipv6_default_gateway
*@brief Get IPv6 default gateway with particular interface name
*@param if_name Ethernet interface name
*@param default_gateway default gateway in IPv6 text format
*@return Returns '0' on success,
*        Returns '1' on failure
*/
static int get_if_ipv6_default_gateway(const char *if_name, char *default_gateway)
{
    struct in6_addr gw_addr;
    FILE *fp;
    int src_prefix, dst_prefix;
    int ret = EXIT_FAILURE;
    unsigned int metrix, ref_cnt, usr_cnt, flag;
    char devname[IFNAMSIZ] = {0};
    char dst[INET6_ADDRSTRLEN] = {0};
    char src[INET6_ADDRSTRLEN] = {0};
    char gw[INET6_ADDRSTRLEN] = {0};

    fp = fopen("/proc/net/ipv6_route", "r");
    if (fp == NULL)
        return EXIT_FAILURE;

    while (fscanf(fp, "%s %x %s %x %s %x %x %x %x %s",
                  dst, &dst_prefix, src, &src_prefix, gw,
                  &metrix, &ref_cnt, &usr_cnt, &flag, devname) != EOF)
    {
        /*default route*/
        if (!strncmp(gw, "fe80", 4) && !strcmp(dst, "00000000000000000000000000000000") && dst_prefix == 0 && !strcmp(if_name, devname))
        {
            snprintf(gw, sizeof(gw), "%c%c%c%c:%c%c%c%c:%c%c%c%c:%c%c%c%c:%c%c%c%c:%c%c%c%c:%c%c%c%c:%c%c%c%c",
                     gw[0], gw[1], gw[2], gw[3], gw[4], gw[5], gw[6], gw[7], gw[8], gw[9],
                     gw[10], gw[11], gw[12], gw[13], gw[14], gw[15], gw[16], gw[17], gw[18], gw[19],
                     gw[20], gw[21], gw[22], gw[23], gw[24], gw[25], gw[26], gw[27], gw[28], gw[29],
                     gw[30], gw[31]);

            inet_pton(PF_INET6, gw, &gw_addr);
            inet_ntop(PF_INET6, &gw_addr, gw, sizeof(gw));

            snprintf(default_gateway, sizeof(gw), "%s", gw);
            ret = EXIT_SUCCESS;
        }
    }

    fclose(fp);
    return ret;
}

int main(int argc, char *argv[])
{
     if(argc != 2){
        printf(" [Input Error]\n");
        exit(1);
    }

    char if_name[IFNAMSIZ] = {'\0'};
    strncpy(if_name, argv[1], IFNAMSIZ - 1);
    int ret;

    unsigned char gateway[INET6_ADDRSTRLEN];
    if (get_if_gateway(if_name, gateway) == 0)
        printf(" gateway : %s \n", gateway);
    if (get_if_ipv6_default_gateway(if_name, gateway) == 0)
        printf("IPV6 gateway = %s\n", gateway);

    // GET augong
    int autoneg;
    if (get_if_autoneg(if_name, &autoneg) == 0)
        printf(" autoneg = %s\n ", (autoneg == AUTONEG_DISABLE) ? "off" : "on");

    // GET duplex
    int duplex = 0;
    if (get_if_duplex(if_name, &duplex) == 0)
        printf(" duplex = %s\n", (duplex == DUPLEX_FULL) ? "Full" : "Half");

    // GET speed
    int speed;
    if (get_if_speed(if_name, &speed) == 0)
        printf(" speed = %dMb/s\n", speed);

    // GET MASK
    char mask[INET_ADDRSTRLEN];
    if (get_if_mask(if_name, mask) == 0)
        printf(" mask = %s \n", mask);

    // GET MTU
    int mtu = 0;
    if (get_if_mtu(if_name, &mtu) == 0)
        printf(" MTU = %d \n", mtu);

    // // GET MAC Address
    unsigned char hwaddr[18] = {0};
    if (get_if_hw_addr(if_name, hwaddr) == 0)
        printf(" hwaddr = %s \n", hwaddr);

    // // GET IPv4 Address
    unsigned char ip_addr[INET_ADDRSTRLEN];
    if (get_if_ipv4_addr(if_name, ip_addr) == 0)
        printf(" ip_addr = %s\n", ip_addr);

    //     // // GET IPv6 Address
    char ipv6_addr[INET6_ADDRSTRLEN] = {0};
    char scope[10];
    int prefix_len;
    ret = get_if_ipv6_info(if_name, ipv6_addr, &prefix_len, scope);
    if (strlen(ipv6_addr) != 0 && prefix_len != 0)
        printf("IPv6 addr %s : %s/%d, Scope : %s\n", if_name, ipv6_addr, prefix_len, scope);

    // GET all interface
    char if_names[MAX_IF_COUNT][IFNAMSIZ] = {0};
    int if_counts;
    ret = 0;
    ret = get_if_readlist(if_names, &if_counts);
    printf(" --------- all interfaces ------ \n");
    if (ret == 0)
    {
        for (int i = 0; i < if_counts; i++)
        {
            if (*(if_names + i) != 0)
                printf("[ %s ]", *(if_names + i));
        }
    }
    printf("\n --------------------------- \n");

    return 0;
}
