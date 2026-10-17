/*
 * (C) 2012 Stefan Seyfried
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#define TARGETRES "1280x720"

/* ffmpeg -y -f mjpeg -i <src> -s TARGETRES <dst> </dev/null, without a shell */
static int convert(const char *src, const char *dst)
{
	char *const args[] = { "ffmpeg", "-y", "-f", "mjpeg", "-i", (char *)src, "-s", TARGETRES, (char *)dst, NULL };
	int status = 0;
	pid_t pid = fork();
	if (pid < 0)
	{
		fprintf(stderr, "pic2m2v: fork: %m\n");
		return -1;
	}
	if (pid == 0)
	{
		int fd = open("/dev/null", O_RDONLY);
		if (fd >= 0)
		{
			dup2(fd, 0);
			close(fd);
		}
		execvp(args[0], args);
		fprintf(stderr, "pic2m2v: execvp ffmpeg: %m\n");
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	return status;
}

int main(int argc, char **argv)
{
	char destname[448];
	char *p;
	char *fname;
	struct stat st, st2;

	if (argc < 2)
	{
		fprintf(stderr, "usage: pic2m2v /path/pic1.jpg [/path/pic2.jpg...]\n\n");
		return 1;
	}
	strcpy(destname, "/var/cache");
	mkdir(destname, 0755);
	argv++;
	for (fname = *argv; fname != NULL; fname = *++argv)
	{
		if (stat(fname, &st2))
		{
			fprintf(stderr, "pic2m2v: could not stat '%s' (%m)\n", fname);
			continue;
		}
		/* the cache filename is (example for /share/tuxbox/neutrino/icons/radiomode.jpg):
		   /var/cache/share.tuxbox.neutrino.icons.radiomode.jpg.m2v
		   build that filename first...
		   TODO: this could cause name clashes, use a hashing function instead... */
		if (snprintf(destname, sizeof(destname), "/var/cache%s.m2v", fname) >= (int)sizeof(destname))
		{
			fprintf(stderr, "pic2m2v: name too long '%s'\n", fname);
			continue;
		}
		p = &destname[strlen("/var/cache/")];
		while ((p = strchr(p, '/')) != NULL)
			* p = '.';
		/* ...then check if it exists already... */
		if (stat(destname, &st) || (st.st_mtime != st2.st_mtime) || (st.st_size == 0))
		{
			struct utimbuf u;
			u.actime = time(NULL);
			u.modtime = st2.st_mtime;
			printf("converting %s -> %s\n", fname, destname);
			/* it does not exist or has a different date, so call ffmpeg... */
			convert(fname, destname); /* TODO: use libavcodec to directly convert it */
			utime(destname, &u);
		}
		else
			printf("cache file %s already current\n", destname);
	}

	return 0;
}
