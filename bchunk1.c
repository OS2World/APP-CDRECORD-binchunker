
 /*
  *	binchunker for Unix
  *	Copyright (C) 1998  Heikki Hannikainen <hessu@pspt.fi>
  *
  *  This program is free software; you can redistribute it and/or modify
  *  it under the terms of the GNU General Public License as published by
  *  the Free Software Foundation; either version 2 of the License, or
  *  (at your option) any later version.
  *
  *  This program is distributed in the hope that it will be useful,
  *  but WITHOUT ANY WARRANTY; without even the implied warranty of
  *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *  GNU General Public License for more details.
  *
  *  You should have received a copy of the GNU General Public License
  *  along with this program; if not, write to the Free Software
  *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define VERSION "1.0.0"
#define USAGE "Usage: bchunk [-v] <image.bin> <image.toc> <basename>\nExample: bchunk foo.bin foo.toc foo\n"
#define VERSTR	"binchunker for Unix, version " VERSION " by Heikki Hannikainen <hessu@pspt.fi>\n\tCreated with the kind help of Bob Marietta <marietrg@SLU.EDU>,\n\tpartly based on his Pascal (Delphi) implementation.\n\tReleased under the GNU GPL, version 2 or later (at your option).\n\tModified for use with CdrDao images by Nick Lepehin\n\tCompiled for OS/2 by Nick Lepehin <nickk9@nettaxi.com>\n\n"

#define CUELLEN 1024
#define SECTLEN 2352

static int
strcasecmp(s1, s2)
	const char     *s1;
	const char     *s2;
{
	while (tolower(*s1) == tolower(*s2)) {
		if (*s1 == 0)
			return (0);
		s1++;
		s2++;
	}
	return (tolower(*s1) - tolower(*s2));
}

struct track {
	int num;
	int mode;
	char *modes;
	char *extension;
	int bstart;
	int bsize;
	long startsect;
	long stopsect;
	long start;
	long stop;
	struct track *next;
};

char *basefile = NULL;
char *binfile = NULL;
char *cuefile = NULL;
int verbose = 0;

/*
 *	Parse arguments
 */

void parse_args(int argc, char *argv[])
{
	int s;
	
	while ((s = getopt(argc, argv, "v?h")) != -1) {
		switch (s) {
			case 'v':
				verbose = 1;
				break;
			case '?':
			case 'h':
				fprintf(stderr, "%s", USAGE);
				exit(0);
		}
	}

	if (argc - optind != 3) {
		fprintf(stderr, "%s", USAGE);
		exit(1);
	}
	
	while (optind < argc) {
		switch (argc - optind) {
			case 3:
				binfile = strdup(argv[optind]);
				break;
			case 2:
				cuefile = strdup(argv[optind]);
				break;
			case 1:
				basefile = strdup(argv[optind]);
				break;
			default:
				fprintf(stderr, "%s", USAGE);
				exit(1);
		}
		optind++;
	}
}

/*
 *	Convert a mins:secs:frames format to plain frames
 */

long time2frames(char *s)
{
	int mins = 0, secs = 0, frames = 0;
	char *p, *t;
	
	if (!(p = strchr(s, ':')))
		return -1;
	*p = '\0';
	mins = atoi(s);
	
	p++;
	if (!(t = strchr(p, ':')))
		return -1;
	*t = '\0';
	secs = atoi(p);
	
	t++;
	frames = atoi(t);
	
	return 75 * (mins * 60 + secs) + frames;
}

/*
 *	Parse the mode string
 */

void gettrackmode(struct track *track, char *modes, int towav)
{
	static char ext_iso[] = "iso";
	static char ext_cdr[] = "cdr";
	static char ext_wav[] = "wav";
	
	if (!strcasecmp(modes, "MODE1/2352")) {
		track->bstart = 16;
		track->bsize = 2048;
		// 2352 is not recognized in WinImage
		track->extension = ext_iso;
		
	} else if (!strcasecmp(modes, "MODE2/2352")) {
		track->bstart = 0;
		// If this would happen to be a playstation data track
		if (0)	// leave at 2352
			track->bsize = 2352;
		else	// PSX
			track->bsize = 2336;
		track->extension = ext_iso;
		
	} else if (!strcasecmp(modes, "MODE2/2336")) {
		// WAS 2352 in V1.361B still work?
		// what if MODE2/2336 single track bin, still 2352 sectors?
		track->bstart = 16;
		track->bsize = 2336;
		track->extension = ext_iso;
		
	} else if (!strcasecmp(modes, "AUDIO")) {
		track->bstart = 0;
		track->bsize = 2352;
		if (towav)
			track->extension = ext_wav;
		else
			track->extension = ext_cdr;
	} else {
		printf("(?) ");
		track->bstart = 0;
		track->bsize = 2352;
		track->extension = ext_wav;
	}
}

/*
 *	Write a track
 */

int writetrack(FILE *bf, struct track *track, char *bname, int towav)
{
	char *fname;
	FILE *f;
	char buf[SECTLEN+10];
	long sz, sect, realsz, reallen;
	
	fname = malloc(strlen(bname) + 8);
	sprintf(fname, "%s%2.2d.%s", bname, track->num, track->extension);
	
	printf("Track %2d: %s ", track->num, fname);
	
	if (!(f = fopen(fname, "w"))) {
		fprintf(stderr, " Could not fopen track file: %s\n", strerror(errno));
		exit(4);
	}
	
	if (fseek(bf, track->start, SEEK_SET)) {
		fprintf(stderr, " Could not fseek to track location: %s\n", strerror(errno));
		exit(4);
	}
	
	reallen = (track->stopsect - track->startsect + 1) * track->bsize;
	if (verbose) {
		printf("\n mmc sectors %ld->%ld (%ld)", track->startsect, track->stopsect, track->stopsect - track->startsect + 1);
		printf("\n mmc bytes %ld->%ld (%ld)", track->start, track->stop, track->stop - track->start + 1);
		printf("\n sector data at %d, %d bytes per sector", track->bstart, track->bsize);
		printf("\n real data %ld bytes", (track->stopsect - track->startsect + 1) * track->bsize);
		printf("\n");
	}
	printf("                  ");
	
	realsz = 0;
	sz = track->start;
	sect = track->startsect;
	while ((sect <= track->stopsect) && (fread(buf, SECTLEN, 1, bf) > 0)) {
		if (fwrite(&buf[track->bstart], track->bsize, 1, f) < 1) {
			fprintf(stderr, " Could not write to track: %s\n", strerror(errno));
			exit(4);
		}
		sect++;
		sz += SECTLEN;
		realsz += track->bsize;
		if (((sz / SECTLEN) % 500) == 0) {
			printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\%4ld/%-4ld MB  %3.0f %%", realsz/1024/1024, reallen/1024/1024, (float)realsz / (float)reallen * 100);
			fflush(stdout);
		}
	}
	
	printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\%4ld/%-4ld MB  %3.0f %%  Done - %ld bytes", realsz/1024/1024, reallen/1024/1024, (float)realsz / (float)reallen * 100, reallen);
	fflush(stdout);
	
	if (fclose(f)) {
		fprintf(stderr, " Could not fclose track file: %s\n", strerror(errno));
		exit(4);
	}
	
	printf("\n");
	return 0;
}

/*
 *	Main
 */

int main(int argc, char **argv)
{
	char s[CUELLEN+1];
	char *p, *t, *tt;
	int idx,tracknum;
	struct track *tracks = NULL;
	struct track *track = NULL;
	struct track *prevtrack = NULL;
	struct track **prevp = &tracks;
	
	FILE *binf, *cuef;
	
	printf("%s", VERSTR);
	
	parse_args(argc, argv);
	
	if (!((binf = fopen(binfile, "r")))) {
		fprintf(stderr, "Could not open BIN %s: %s\n", binfile, strerror(errno));
		return 2;
	}
	
	if (!((cuef = fopen(cuefile, "r")))) {
		fprintf(stderr, "Could not open CUE %s: %s\n", cuefile, strerror(errno));
		return 2;
	}
	
	printf("Reading the CUE file:\n");
	
	/* We don't really care about the first line. */
	if (!fgets(s, CUELLEN, cuef)) {
		fprintf(stderr, "Could not read first line from %s: %s\n", cuefile, strerror(errno));
		return 3;
	}
	
        tracknum=0;
	while (fgets(s, CUELLEN, cuef)) {
		while ((p = strchr(s, '\r')) || (p = strchr(s, '\n')))
			*p = '\0';
			
		if ((p = strstr(s, "TRACK"))) {
                        tracknum++;
			printf("\nTrack ");
			if (!(t = strchr(p, ' '))) {
				fprintf(stderr, "... ouch, no space after TRACK.\n");
				continue;
			}
			*t = '\0';
			
			prevtrack = track;
			track = malloc(sizeof(struct track));
			*prevp = track;
			prevp = &track->next;
			track->next = NULL;
			track->num = tracknum;
			
			p = t + 1;
			printf("%2d: %-12.12s ", track->num, p);
			track->modes = strdup(p);
			track->extension = NULL;
			track->mode = 0;
			track->bsize = track->bstart = -1;
			track->bsize = -1;
			track->startsect = track->stopsect = -1;
			
			gettrackmode(track, p, 0);
			
		} else if ((p = strstr(s, "FILE"))) {
			if (!(p = strchr(p, ' '))) {
				printf("... ouch, no space after INDEX.\n");
				continue;
			}
			p++;p++;
			if (!(p = strchr(p, '"'))) {
				printf("... ouch, no end of file.\n");
				continue;
			}
			p++;
			if (!(t = strchr(p, ' '))) {
				printf("... ouch, no space after index number.\n");
				continue;
			}
			*t = '\0';
			t++;
			if (!(tt = strchr(t, ' '))) {
				printf("... ouch, no space after index number.\n");
				continue;
			}
                        *tt= '\0'; 
			idx = 0;
			printf(" 0%d %s", idx, t);
			track->startsect = time2frames(t);
			track->start = track->startsect * SECTLEN;
			if (verbose)
				printf(" (startsect %ld ofs %ld)", track->startsect, track->start);
			if ((prevtrack) && (prevtrack->stopsect < 0)) {
				prevtrack->stopsect = track->startsect;
				prevtrack->stop = track->start - 1;
			}
		} else if ((p = strstr(s, "START"))) {
			if (!(t = strchr(p, ' '))) {
				printf("... ouch, no space after INDEX.\n");
				continue;
			}
			*t = '\0';
			t++;
			idx = 1;
			printf(" 0%d %s", idx, t);
			track->startsect += time2frames(t);
			track->start = track->startsect * SECTLEN;
			if (verbose)
				printf(" (startsect %ld ofs %ld)", track->startsect, track->start);
			if ((prevtrack) && (prevtrack->stopsect < 0)) {
				prevtrack->stopsect = track->startsect;
				prevtrack->stop = track->start - 1;
			}
		}
	}
	
	if (track) {
		fseek(binf, 0, SEEK_END);
		track->stop = ftell(binf);
		track->stopsect = track->stop / SECTLEN;
	}
	
	printf("\n\n");
	
	
	printf("Writing tracks:\n\n");
	for (track = tracks; (track); track = track->next)
		writetrack(binf, track, basefile, 0);
		
	fclose(binf);
	fclose(cuef);
	
	return 0;
}

