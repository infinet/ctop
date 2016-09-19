/*
 * ctop, or Cluster top
 * for monitor seismic cluster CPU and memory usage
 *
 * Chen Wei, <weichen302@gmail.com>
 * BGP Prospector, 2016-08-29
 */

#include <pthread.h>
#include <assert.h>
#include <ncurses.h>
#include <netdb.h>
#include <sys/wait.h>
#include "ctop.h"

#define Red 1
#define Green 2
#define Yellow 3

#define MAXLINE 1000
#define CMDLEN 100
#define NODES 48
#define THREADS 8
#define BARLEN 30
#define COL_WIDTH 35
#define MAXHOST 100
#define TIMEOUT 3
#define DEFAULT_NIC "eth0"
#define MAX_NIC_SPEED 134217728.0 /* 128MB/s, in bytes / second */
#define ONEM 1048576.0

struct cpumem_stat {
    char host[MAXHOST];
    int  node_id;
    int    valid;
    double cpu_util;
    double cpu_user;
    double cpu_nice;
    double cpu_sys;
    double cpu_idle;
    int    cpu_count;
    unsigned long long memtotal;
    unsigned long long memfree;
    unsigned long long buffers;
    unsigned long long cached;
    unsigned long long rx;
    unsigned long long tx;
    double rx_speed;
    double tx_speed;
    time_t stat_time;
};

struct cpumem_stat promax_nodes_st[NODES];

struct worker_param {
    int tid;
    int elm_num;
    int start;
    int end;
};

//FILE *debug_fp;

/* prototypes */
static void *host_cpumem(void *args);
static void rsh_worker(struct cpumem_stat *st);
static void format_print_bar(struct cpumem_stat *s, int row, int col);
static void read_nodes(void);


static void *host_cpumem(void *args)
{
    int i, start, end;
    struct cpumem_stat *p;

    start = ((struct worker_param *) args)->start;
    end = ((struct worker_param *) args)->end;
    for (i = start; i < end; i++){
        p = &(promax_nodes_st[i]);
        rsh_worker(p);
    }

    return NULL;
}


static void rsh_worker(struct cpumem_stat *st)
{
    FILE *fp;
    char line[MAXLINE];
    char cmdbuf[CMDLEN];
    int rc;
    double user, nice, sys, idle, used, total;

    memset(cmdbuf, 0, sizeof(cmdbuf));

    snprintf(cmdbuf, CMDLEN,
             "/usr/bin/rsh %s cat /proc/stat /proc/meminfo /proc/net/dev 2>/dev/null",
             st->host);

    /* debug */
    fp = popen(cmdbuf, "r");
    if (fp == NULL) {
        fprintf(stderr, "popen() failed\n");
        exit(2);
    }

    user = -1.0;
    memset(line, 0, sizeof(line));
    while (fgets(line, MAXLINE, fp) != NULL) {
        if (user < 0) {
            sscanf(line, "%*s %lf %lf %lf %lf", &user, &nice, &sys, &idle);

        } else if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %llu kB", &(st->memtotal));

        } else if (strncmp(line, "MemFree:", 8) == 0) {
            sscanf(line, "MemFree: %llu kB", &(st->memfree));

        } else if (strncmp(line, "Buffers:", 8) == 0) {
            sscanf(line, "Buffers: %llu kB", &(st->buffers));

        } else if (strncmp(line, "Cached:", 7) == 0) {
            sscanf(line, "Cached: %llu kB", &(st->cached));

            break;
        }

        memset(line, 0, sizeof(line));
    }

    used = user + nice + sys - st->cpu_user - st->cpu_nice - st->cpu_sys;
    total = used + idle - st->cpu_idle;

    /* advance to /proc/net/dev */
    int valid = -1;
    memset(line, 0, sizeof(line));
    while (fgets(line, MAXLINE, fp) != NULL) {
        if (strstr(line, "Transmit")) {
            /* use the word "Transmit" as a hint for the rsh reaches
             * /proc/meminfo */

            valid = 1;
            st->cpu_util = used / total;
            st->cpu_user = user;
            st->cpu_nice = nice;
            st->cpu_sys = sys;
            st->cpu_idle = idle;
            break;
        }

        memset(line, 0, sizeof(line));
    }

    time_t now;
    double t;
    unsigned long long rx, tx;
    rx = 0;
    tx = 0;
    int rc_nic;
    if ((rc_nic = readnic(fp, DEFAULT_NIC, &rx, &tx)) == 0) {
        now = time(NULL);
        t = (double) now - st->stat_time;
        if (0.0 == t) {
            st->rx_speed = 0.0;
            st->tx_speed = 0.0;
        } else {
            st->rx_speed = (double) (rx - st->rx) / t;
            st->tx_speed = (double) (tx - st->tx) / t;
        }

        st->rx = rx;
        st->tx = tx;
        st->stat_time = now;
    }

    //fprintf(debug_fp, "%s: rc_nid = %d\n", st->host, rc_nic);
    //fflush(debug_fp);

    /* eat all rsh output, hopefully remote will notice it and close rsh */
    while (fgets(line, MAXLINE, fp) != NULL)
        ;

    st->valid = 0;
    if ((rc = pclose(fp)) == -1 || valid == -1)
        st->valid = -1;

}


/* print CPU/MEM/NIC speed at location (row, col) */
static void format_print_bar(struct cpumem_stat *s, int row, int col)
{
    int used, left;
    int i, j, n, barlen;
    int r, c;
    int bar_color;
    double perc;

    r = row;
    c = col;
    mvprintw(r, c, "%2d: ", s->node_id);

    c = col + 4;

    /* clear previous drawing */
    int last_col = col + COL_WIDTH;
    int last_row = row + 4;
    for (i = row; i < last_row; i++)
        for (j = c; j < last_col; j++)
            mvprintw(i, j, " ");

    if (s->valid != 0) {
        attron(COLOR_PAIR(Red));
        mvprintw(r, c,     "[ error: no data ]CPU");
        mvprintw(r + 1, c, "[ error: no data ]MEM");
        mvprintw(r + 2, c, "[ error: no data ]RX");
        mvprintw(r + 3, c, "[ error: no data ]TX");
        attroff(COLOR_PAIR(Red));
        return;
    }

    perc = s->cpu_util;

    if (perc < 0.5)
        bar_color = Green;
    else if (perc < 0.7)
        bar_color = Yellow;
    else
        bar_color = Red;

    barlen = BARLEN - 6 - 6;
    used = (int) barlen * perc;
    left = barlen - used;

    attron(A_STANDOUT);
    mvprintw(r, c++, "[");
    attron(COLOR_PAIR(bar_color));
    for (n = 0 ; n < used; n++)
        mvprintw(r, c++, "|");

    for (n = 0 ; n < left; n++)
        mvprintw(r, c++, " ");

    mvprintw(r, c, "%5.1f%%", perc * 100.0);
    attroff(COLOR_PAIR(bar_color));

    mvprintw(r++, c + 6, "]CPU");
    attroff(A_STANDOUT);

    /* memory */
    c = col + 4;
    unsigned long long mem_used;
    mem_used = s->memtotal - s->memfree - s->buffers - s->cached;
    perc = (double) mem_used / s->memtotal;

    if (perc < 0.5)
        bar_color = Green;
    else if (perc < 0.7)
        bar_color = Yellow;
    else
        bar_color = Red;

    used = (int) barlen * perc;
    left = barlen - used;

    mvprintw(r, c++, "[");
    attron(COLOR_PAIR(bar_color));
    for (n = 0 ; n < used; n++) {
        mvprintw(r, c++, "|");
    }

    for (n = 0 ; n < left; n++, i++) {
        mvprintw(r, c++, " ");
    }

    mvprintw(r, c, "%5.1f%%", perc * 100.0);
    attroff(COLOR_PAIR(bar_color));
    mvprintw(r++, c + 6, "]MEM");

    /* NIC RX */
    c = col + 4;
    perc = (s->rx_speed > MAX_NIC_SPEED)? 1.0: s->rx_speed / MAX_NIC_SPEED;
    if (perc < 0.5)
        bar_color = Green;
    else if (perc < 0.7)
        bar_color = Yellow;
    else
        bar_color = Red;

    barlen = BARLEN - 6 - 6 - 3;
    used = (int) barlen * perc;
    left = barlen - used;

    mvprintw(r, c++, "[");
    attron(COLOR_PAIR(bar_color));
    for (n = 0 ; n < used; n++) {
        mvprintw(r, c++, "|");
    }

    for (n = 0 ; n < left; n++, i++) {
        mvprintw(r, c++, " ");
    }

    attroff(COLOR_PAIR(bar_color));
    mvprintw(r, c, "]RX");
    mvprintw(r++, c + 3, "%5.1f MB/s", s->rx_speed / ONEM);

    /* NIC TX */
    c = col + 4;
    perc = (s->tx_speed > MAX_NIC_SPEED)? 1.0: s->tx_speed / MAX_NIC_SPEED;
    if (perc < 0.5)
        bar_color = Green;
    else if (perc < 0.7)
        bar_color = Yellow;
    else
        bar_color = Red;

    used = (int) barlen * perc;
    left = barlen - used;

    mvprintw(r, c++, "[");
    attron(COLOR_PAIR(bar_color));
    for (n = 0 ; n < used; n++) {
        mvprintw(r, c++, "|");
    }

    for (n = 0 ; n < left; n++, i++) {
        mvprintw(r, c++, " ");
    }

    attroff(COLOR_PAIR(bar_color));
    mvprintw(r, c, "]TX");
    mvprintw(r++, c + 3, "%5.1f MB/s", s->tx_speed / ONEM);

}


static void read_nodes(void)
{
    int i, rc;
    struct tm *tmp;
    time_t t;
    char buf_t[40];
    int elm_per_thread;
    pthread_t threads[THREADS];
    struct worker_param thread_args[THREADS];
    struct worker_param *p;

    elm_per_thread = ((NODES / THREADS) == 0) ? 1 : NODES / THREADS;

    t = time(NULL);
    tmp = localtime(&t);
    memset(buf_t, 0, 40);
    strftime(buf_t, 40, "%Y-%m-%d %H:%M:%S", tmp);

    memset(thread_args,  0, THREADS * sizeof(struct worker_param));

    for (i = 0; i < THREADS; i++) {
        p = &(thread_args[i]);
        p->tid = i;
        p->start = i * elm_per_thread;
        p->end = ( ((p->start + elm_per_thread) < NODES)?
                      p->start + elm_per_thread : NODES);
        //fprintf(debug_fp, "p->start = %d, end = %d, elmperthread= %d\n", p->start, p->end, elm_per_thread);
        //fflush(debug_fp);
    }

    for (i = 0; i < THREADS; i++) {
        rc = pthread_create(&threads[i], NULL, host_cpumem, &(thread_args[i]));
        assert(0 == rc);
    }

    for (i = 0; i < THREADS; i++) {
        rc = pthread_join(threads[i], NULL);
        assert(0 == rc);
    }

    struct cpumem_stat *s;
    int row, col;
    row = 0;
    mvprintw(row++, 0, "Cluster top  %s, type q to quit", buf_t);

    for (i = 0; i < NODES; ) {
        s = &(promax_nodes_st[i++]);
        col = 0;
        format_print_bar(s, row, 0);
        if (i >= NODES)
            break;

        s = &(promax_nodes_st[i++]);
        col += COL_WIDTH;;
        format_print_bar(s, row, col);
        if (i >= NODES)
            break;

        s = &(promax_nodes_st[i++]);
        col += COL_WIDTH;;
        format_print_bar(s, row, col);
        if (i >= NODES)
            break;

        s = &(promax_nodes_st[i++]);
        col += COL_WIDTH;;
        format_print_bar(s, row, col);
        if (i >= NODES)
            break;

        row += 4;
    }
}


int main(int argc, char *argv[])
{
    int i, row, col, min_rows, min_cols;
    int ret;
    time_t now, next;

    //debug_fp = fopen("debug.log", "a");

    memset(promax_nodes_st, 0, NODES * sizeof(struct cpumem_stat));
    for (i = 0; i < NODES; i++) {
        promax_nodes_st[i].node_id = i + 1;
        snprintf(promax_nodes_st[i].host, MAXHOST, "node%02d", i + 1);
    }

    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();

    min_rows = 4 + NODES / 2;
    min_cols = COL_WIDTH * 4;
    getmaxyx(stdscr, row, col);
    if (row < min_rows || col < min_cols) {
        printw("This screen has %d rows and %d columns\n", row, col);
        printw("Need at least %d rows and %d columns\n", min_rows, min_cols);
        printw("Resizing your windows and then try again");
        refresh();
        getch();
        endwin();
        exit(2);
    }

    halfdelay(1);
    start_color();
    init_pair(Green, COLOR_GREEN, COLOR_BLACK);
    init_pair(Yellow, COLOR_YELLOW, COLOR_BLACK);
    init_pair(Red, COLOR_RED, COLOR_BLACK);

    next = time(NULL) - 1;
    while (1) {
        now = time(NULL);
        if (now > next) {
            read_nodes();
            refresh();
            next = time(NULL) + TIMEOUT;
        }

        ret = getch();
        if (getch() == 'q'){
            break;
        }
    }

    nocbreak();
    endwin();
    return 0;
}
