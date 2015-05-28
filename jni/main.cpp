#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/limits.h>
#include <sys/poll.h>
#include <linux/input.h> // this does not compile
#include <errno.h>

static struct pollfd *ufds;
static char **device_names;
static int nfds;

enum {
    PRINT_DEVICE_ERRORS     = 1U << 0,
    PRINT_DEVICE            = 1U << 1,
    PRINT_DEVICE_NAME       = 1U << 2,
    PRINT_DEVICE_INFO       = 1U << 3,
    PRINT_VERSION           = 1U << 4,
    PRINT_POSSIBLE_EVENTS   = 1U << 5,
};

static int open_device(const char *device, int print_flags)
{
    printf("open_device start %s,%d\n",device,print_flags);

    int fd;
    struct pollfd *new_ufds;
    char **new_device_names;

    fd = open(device, O_RDWR);
    if(fd < 0) {
        if(print_flags & PRINT_DEVICE_ERRORS)
            fprintf(stderr, "could not open %s, %s\n", device, strerror(errno));
        return -1;
    }
    printf("%s opened\n",device);

    new_ufds = (pollfd*)realloc(ufds, sizeof(ufds[0]) * (nfds + 1));
    if(new_ufds == NULL) {
        fprintf(stderr, "out of memory\n");
        return -1;
    }
    ufds = new_ufds;
    printf("mem alloc for ufds(sizeof(ufds[0]):%d x %d)\n",sizeof(ufds[0]),nfds+1);

    new_device_names = (char**)realloc(device_names, sizeof(device_names[0]) * (nfds + 1));
    if(new_device_names == NULL) {
        fprintf(stderr, "out of memory\n");
        return -1;
    }
    device_names = new_device_names;
    printf("mem alloc for dev names(sizeof(device_names[0]):%d x %d)\n",sizeof(device_names[0]),nfds+1);

    ufds[nfds].fd = fd;
    ufds[nfds].events = POLLIN;
    device_names[nfds] = strdup(device);
    nfds++;
    printf("open_device end nfds:%d fd:%d device:%s\n",nfds,fd,device);
    return 0;
}

int close_device(const char *device, int print_flags)
{
    int i;
    for(i = 1; i < nfds; i++) {
        if(strcmp(device_names[i], device) == 0) {
            int count = nfds - i - 1;
            if(print_flags & PRINT_DEVICE)
                printf("remove device %d: %s\n", i, device);
            free(device_names[i]);
            memmove(device_names + i, device_names + i + 1, sizeof(device_names[0]) * count);
            memmove(ufds + i, ufds + i + 1, sizeof(ufds[0]) * count);
            nfds--;
            return 0;
        }
    }
    if(print_flags & PRINT_DEVICE_ERRORS)
        fprintf(stderr, "remote device: %s not found\n", device);
    return -1;
}

static int scan_dir(const char *dirname, int print_flags)
{
    char devname[PATH_MAX];
    char *filename;
    DIR *dir;
    struct dirent *de;

    printf("scan_dir start %s,%d\n",dirname,print_flags);

    printf("dir(%s) opening...\n",dirname);
    dir = opendir(dirname);
    if(dir == NULL)
        return -1;
    printf("dir(%s) opened\n",dirname);

    strcpy(devname, dirname);
    filename = devname + strlen(devname);
    *filename++ = '/';

    printf("filename=%s\n",filename);
    while((de = readdir(dir))) {
        if(de->d_name[0] == '.' &&
           (de->d_name[1] == '\0' ||
            (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
	    printf("continue,d_name:%d,%d,%d\n",de->d_name[0],de->d_name[1],de->d_name[1],de->d_name[2]);
            continue;
	}

        strcpy(filename, de->d_name);
        printf("device(%s) (%d)opening...\n",devname,print_flags);
        open_device(devname, print_flags);
    }
    closedir(dir);
    printf("dir(%s) closed\n",dirname);

    printf("scan_dir end %s,%d\n",dirname,print_flags);
    return 0;
}

static void usage(int argc, char *argv[])
{
    fprintf(stderr, "Usage: %s [-t] [-n] [-s switchmask] [-S] [-v [mask]] [-q] [-c count] [-r] [device]\n", argv[0]);
}

int main(int argc, char *argv[])
{
    int c;
    int i;
    int res;
    int pollres;
    int get_time = 0;
    int print_device = 0;
    char *newline = "\n";
    uint16_t get_switch = 0;
    struct input_event event;
    int version;
    int print_flags = PRINT_DEVICE_ERRORS | PRINT_DEVICE | PRINT_DEVICE_NAME;
    int print_flags_set = 0;
    int dont_block = -1;
    int event_count = 0;
    int sync_rate = 0;
    int64_t last_sync_time = 0;
    const char *device = NULL;
    const char *device_path = "/dev/input";

    opterr = 0;
    do {
        c = getopt(argc, argv, "tns:Sv::qc:rh");
        if (c == EOF)
            break;
        switch (c) {
        case 't':
            get_time = 1;
            break;
        case 'n':
            newline = "";
            break;
        case 's':
            get_switch = strtoul(optarg, NULL, 0);
            if(dont_block == -1)
                dont_block = 1;
            break;
        case 'S':
            get_switch = ~0;
            if(dont_block == -1)
                dont_block = 1;
            break;
        case 'v':
            if(optarg)
                print_flags =  strtoul(optarg, NULL, 0);
            else
                print_flags |= PRINT_DEVICE | PRINT_DEVICE_NAME | PRINT_DEVICE_INFO | PRINT_VERSION;
            print_flags_set = 1;
            break;
        case 'q':
            print_flags = 0;
            print_flags_set = 1;
            break;
        case 'c':
            event_count = atoi(optarg);
            dont_block = 0;
            break;
        case 'r':
            sync_rate = 1;
            break;
        case '?':
            fprintf(stderr, "%s: invalid option -%c\n",
                argv[0], optopt);
        case 'h':
            usage(argc, argv);
            exit(1);
        }
    } while (1);
    if(dont_block == -1)
        dont_block = 0;

    if (optind + 1 == argc) {
        device = argv[optind];
        optind++;
    }

    nfds = 1;
    ufds = (pollfd*)calloc(1, sizeof(ufds[0]));
    ufds[0].fd = inotify_init();
    ufds[0].events = POLLIN;

    printf("device is NULL, add watch %s\n",device_path);
    print_device = 1;
    res = inotify_add_watch(ufds[0].fd, device_path, IN_DELETE | IN_CREATE);
    if(res < 0) {
        fprintf(stderr, "could not add watch for %s, %s\n", device_path, strerror(errno));
        return 1;
    }
    printf("scanning dir %s\n",device_path);
    res = scan_dir(device_path, print_flags);
    if(res < 0) {
        fprintf(stderr, "scan dir failed for %s\n", device_path);
        return 1;
    }

    if(get_switch) {
        printf("get_switch\n",device_path);
        for(i = 1; i < nfds; i++) {
            uint16_t sw;
            res = ioctl(ufds[i].fd, EVIOCGSW(1), &sw);
            if(res < 0) {
                fprintf(stderr, "could not get switch state, %s\n", strerror(errno));
                return 1;
            }
            sw &= get_switch;
            printf("%04x%s", sw, newline);
        }
    }

    if(dont_block)
        return 0;

    printf("entering while loop...\n");
    while(1) {
        pollres = poll(ufds, nfds, -1);
        printf("poll %d, returned %d\n", nfds, pollres);
        for(i = 1; i < nfds; i++) {
            if(ufds[i].revents) {
                if(ufds[i].revents & POLLIN) {
		    printf("ufds[%d].revents=%d, POLLIN\n",i,ufds[i].revents);
                    res = read(ufds[i].fd, &event, sizeof(event));
                    if(res < (int)sizeof(event)) {
                        fprintf(stderr, "could not get event\n");
                        return 1;
                    }
		    printf("read %d bytes ",res);

                    if(get_time) {
                        printf("%ld-%ld: ", event.time.tv_sec, event.time.tv_usec);
                    }
                    if(print_device)
                        printf("%s: ", device_names[i]);
                    //printf("%04x %04x %08x", event.type, event.code, event.value);
                    if(sync_rate && event.type == 0 && event.code == 0) {
                        int64_t now = event.time.tv_sec * 1000000LL + event.time.tv_usec;
                        if(last_sync_time)
                            printf(" rate %lld", 1000000LL / (now - last_sync_time));
                        last_sync_time = now;
                    }
                    printf("%s", newline);
                    if(event_count && --event_count == 0)
                        return 0;
                }
            }
        }
    }

    return 0;
}
