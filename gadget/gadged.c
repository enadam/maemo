/*
 * gadged.c -- bring up usb0 when the gadget appears on the bus
 *
 * Run this program as a daemon and forget about udev and ifup usb0 --force
 * and enjoy instant usb networking with your gadget.
 */

/* Include files */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <sys/inotify.h>

//#define CONFIG_DEBUG

/* Path to the logfile to read usb events from. */
#ifndef CONFIG_DEBUG
# define KLOG		"/var/log/kern.log"
#else /* for testing */
# define KLOG		"kern.log"
#endif

/* Program code */
static void __attribute__((noreturn))
die(char const *fun)
{
	syslog(LOG_ERR, "fatal: %s: %m", fun);
	exit(1);
}

/* Watch $klog until it's renamed or deleted which indicates that
 * there's a new klog. */
static void watch(int ifd, FILE *klog)
{
	syslog(LOG_INFO, "watching");
	for (;;)
	{
		char line[256];
		struct inotify_event evt;

		/* Wait until something happens to $klog. */
		while (read(ifd, &evt, sizeof(evt)) < 0)
			if (errno != EINTR)
				die("read(inotify)");
		if (evt.mask & (IN_MOVE_SELF|IN_DELETE_SELF))
			/* Moved or deleted. */
			break;
		if ((evt.mask & IN_ATTRIB)
				&& access(KLOG, F_OK) < 0
				&& errno == ENOENT)
			/* Deleted but we didn't get DELETE_SELF
			 * for some reason. */
			break;
		if (!(evt.mask & IN_MODIFY))
			/* Touched, nevermind that. */
			continue;

		/* grep for "Dec 15 11:14:07 nightfish kernel: usb0: register
		 * 'cdc_ether' at usb-0000:00:1d.7-3, CDC Ethernet Device,
		 * 62:46:a0:67:2b:e8" and execute ifup when we see it. */
		while (fgets(line, sizeof(line), klog))
		{
			static char *ifup[] = { "ifup", "--force", NULL, NULL };
			char c, *l;
			unsigned n;
			int n1, n2;

			/* Poor man's m//. */
                        l = line;
			if (sscanf(l, "%*s %*u %*u:%*u:%*u %*s kernel%c%n",
                                   &c, &n1) < 1 || c != ':')
                            continue;
                        l += n1;
			if (sscanf(l, " cdc_ether %*u-%*u:%*u.%*u%c%n",
                                   &c, &n1) >= 1 && c == ':')
                          /* For newer kernels */
                          l += n1;
			if (sscanf(l, " %nusb%u%n: register 'cdc_ether%c",
					&n1, &n, &n2, &c) < 2 || c != '\'')
				continue;
                        l += n1;
			switch (fork())
			{
			case -1:
				die("fork");
			default:
				continue;
			case 0:
				l[n2-n1] = '\0';
				ifup[2] = l;
                                signal(SIGCHLD, SIG_DFL);
				execv("/sbin/ifup", ifup);
				die("exec(/sbin/ifup)");
			}
		} /* while there are lines to read */
	} /* until $klog is no more */
}

int main(void)
{
	int ifd;

	/* Init */
#ifndef CONFIG_DEBUG
	openlog("gadged", LOG_CONS | LOG_NDELAY | LOG_PID, LOG_DAEMON);
#else
	openlog("gadged", LOG_PERROR, LOG_DAEMON);
#endif
	if ((ifd = inotify_init()) < 0)
		die("inotify_init()");
        signal(SIGCHLD, SIG_IGN);

	/* Run */
	for (;;)
	{
		int wd;
		FILE *klog;

		/* Try until we can open $klog. */
		while (!(klog = fopen(KLOG, "r")))
		{
			syslog(LOG_WARNING, KLOG": %m");
			sleep(10);
		}

		if (fseek(klog, 0, SEEK_END) < 0)
			die("seek("KLOG")");
		else if ((wd = inotify_add_watch(ifd, KLOG,
				IN_MODIFY|IN_MOVE_SELF|IN_DELETE_SELF|IN_ATTRIB))
					< 0)
			die("inotify_add_watch("KLOG")");
		watch(ifd, klog);
		inotify_rm_watch(ifd, wd);
		fclose(klog);
	} /* for ever */

	return 0;
}

/* End of gadged.c */
