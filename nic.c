#include "interface.h"
#include "ctop.h"

#define BUFSIZE 1024
#define _PATH_PROCNET_DEV  "/proc/net/dev"


static int procnetdev_version(char *buf);
static int get_dev_fields(int procnetdev_vsn, char *bp, struct interface *ife);
static char *get_name(char *name, char *p);


static int get_dev_fields(int procnetdev_vsn, char *bp, struct interface *ife)
{
    switch (procnetdev_vsn) {
    case 3:
        sscanf(bp,
               "%llu %llu %lu %lu %lu %lu %lu %lu %llu %llu %lu %lu %lu %lu %lu %lu",
               &ife->stats.rx_bytes,
               &ife->stats.rx_packets,
               &ife->stats.rx_errors,
               &ife->stats.rx_dropped,
               &ife->stats.rx_fifo_errors,
               &ife->stats.rx_frame_errors,
               &ife->stats.rx_compressed,
               &ife->stats.rx_multicast,
               &ife->stats.tx_bytes,
               &ife->stats.tx_packets,
               &ife->stats.tx_errors,
               &ife->stats.tx_dropped,
               &ife->stats.tx_fifo_errors,
               &ife->stats.collisions,
               &ife->stats.tx_carrier_errors, &ife->stats.tx_compressed);
        break;
    case 2:
        sscanf(bp,
               "%llu %llu %lu %lu %lu %lu %llu %llu %lu %lu %lu %lu %lu",
               &ife->stats.rx_bytes, &ife->stats.rx_packets,
               &ife->stats.rx_errors, &ife->stats.rx_dropped,
               &ife->stats.rx_fifo_errors, &ife->stats.rx_frame_errors,
               &ife->stats.tx_bytes, &ife->stats.tx_packets,
               &ife->stats.tx_errors, &ife->stats.tx_dropped,
               &ife->stats.tx_fifo_errors, &ife->stats.collisions,
               &ife->stats.tx_carrier_errors);
        ife->stats.rx_multicast = 0;
        break;
    case 1:
        sscanf(bp, "%llu %lu %lu %lu %lu %llu %lu %lu %lu %lu %lu",
               &ife->stats.rx_packets,
               &ife->stats.rx_errors,
               &ife->stats.rx_dropped,
               &ife->stats.rx_fifo_errors,
               &ife->stats.rx_frame_errors,
               &ife->stats.tx_packets,
               &ife->stats.tx_errors,
               &ife->stats.tx_dropped,
               &ife->stats.tx_fifo_errors,
               &ife->stats.collisions, &ife->stats.tx_carrier_errors);
        ife->stats.rx_bytes = 0;
        ife->stats.tx_bytes = 0;
        ife->stats.rx_multicast = 0;
        break;
    }

    //printf("procnetdev_vsn %d\n, %s\ntx_bytes = %llu\n\n",procnetdev_vsn, bp, ife->stats.tx_bytes );

    return 0;
}

static int procnetdev_version(char *buf)
{
    if (strstr(buf, "compressed"))
        return 3;
    if (strstr(buf, "bytes"))
        return 2;
    return 1;
}

static char *get_name(char *name, char *p)
{
    while (isspace(*p))
        p++;
    while (*p) {
        if (isspace(*p))
            break;
        if (*p == ':') {        /* could be an alias */
            char *dot = p, *dotname = name;
            *name++ = *p++;
            while (isdigit(*p))
                *name++ = *p++;
            if (*p != ':') {        /* it wasn't, backup */
                p = dot;
                name = dotname;
            }
            if (*p == '\0')
                return NULL;
            p++;
            break;
        }
        *name++ = *p++;
    }
    *name++ = '\0';
    return p;
}


/* read NIC rx and tx */
int readnic(FILE *fp, char *nic, unsigned long long *rx, unsigned long long *tx)
{
    int procnetdev_vsn;
    char *s, name[IFNAMSIZ], buf[BUFSIZE];
    struct interface iface;
    int rc = -1;

    memset(&iface, 0, sizeof(struct interface));

    /* eat one lines, expect fp been passed in after strstr find keyword
     * Transmit in /proc/net/dev */
    fgets(buf, sizeof buf, fp);

    procnetdev_vsn = procnetdev_version(buf);

    memset(buf, 0, BUFSIZE);
    while (fgets(buf, sizeof buf, fp)) {
        s = get_name(name, buf);
	//printf("debug_orig: %s", buf);
	//printf("debug_aftr: %s", s);

        if (strcmp(name, nic) == 0) {
            strncpy(iface.name, name, IFNAMSIZ);
            get_dev_fields(procnetdev_vsn, s, &iface);
            rc = 0;
            break;
        }

        memset(buf, 0, BUFSIZE);
    }

    *rx = iface.stats.rx_bytes;
    *tx = iface.stats.tx_bytes;

    return rc;
}
