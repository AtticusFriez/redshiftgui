/* redshift.c -- Main program source
   This file is part of Redshift.

   Redshift is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Redshift is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Redshift.  If not, see <http://www.gnu.org/licenses/>.

   Copyright (c) 2010  Jon Lund Steffensen <jonlst@gmail.com>
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <locale.h>

#ifdef HAVE_SYS_SIGNAL_H
# include <sys/signal.h>
#endif

#ifdef ENABLE_NLS
# include <libintl.h>
# define _(s) gettext(s)
#else
# define _(s) s
#endif

#include "solar.h"
#include "systemtime.h"

#define MIN(x,y)  ((x) < (y) ? (x) : (y))
#define MAX(x,y)  ((x) > (y) ? (x) : (y))

#include "redshift.h"

#if defined(ENABLE_GTK) || defined(ENABLE_WINGUI)
# include "gui/gui.h"
#endif

/* Enum of gamma adjustment methods */
typedef enum {
	GAMMA_METHOD_RANDR,
	GAMMA_METHOD_VIDMODE,
	GAMMA_METHOD_WINGDI,
	GAMMA_METHOD_MAX
} gamma_method_t;


/* Bounds for parameters. */
#define MIN_LAT   -90.0
#define MAX_LAT    90.0
#define MIN_LON  -180.0
#define MAX_LON   180.0
#define MIN_TEMP   1000
#define MAX_TEMP  10000
#define MIN_GAMMA   0.1
#define MAX_GAMMA  10.0

/* Default values for parameters. */
#define DEFAULT_DAY_TEMP    5500
#define DEFAULT_NIGHT_TEMP  3700
#define DEFAULT_GAMMA        1.0

/* Angular elevation of the sun at which the color temperature
   transition period starts and ends (in degress).
   Transition during twilight, and while the sun is lower than
   3.0 degrees above the horizon. */
#define TRANSITION_LOW     SOLAR_CIVIL_TWILIGHT_ELEV
#define TRANSITION_HIGH    3.0


#ifdef HAVE_SYS_SIGNAL_H

static volatile sig_atomic_t exiting = 0;
static volatile sig_atomic_t disable = 0;

/* Signal handler for exit signals */
static void
sigexit(int signo)
{
	exiting = 1;
}

/* Signal handler for disable signal */
static void
sigdisable(int signo)
{
	disable = 1;
}

#else /* ! HAVE_SYS_SIGNAL_H */

static int exiting = 0;
static int disable = 0;

#endif /* ! HAVE_SYS_SIGNAL_H */

/* Restore saved gamma ramps with the appropriate adjustment method. */
static void
gamma_state_restore(gamma_state_t *state, gamma_method_t method)
{
	switch (method) {
#ifdef ENABLE_RANDR
	case GAMMA_METHOD_RANDR:
		randr_restore(&state->randr);
		break;
#endif
#ifdef ENABLE_VIDMODE
	case GAMMA_METHOD_VIDMODE:
		vidmode_restore(&state->vidmode);
		break;
#endif
#ifdef ENABLE_WINGDI
	case GAMMA_METHOD_WINGDI:
		w32gdi_restore(&state->w32gdi);
		break;
#endif
	default:
		break;
	}
}

/* Free the state associated with the appropriate adjustment method. */
static void
gamma_state_free(gamma_state_t *state, gamma_method_t method)
{
	switch (method) {
#ifdef ENABLE_RANDR
	case GAMMA_METHOD_RANDR:
		randr_free(&state->randr);
		break;
#endif
#ifdef ENABLE_VIDMODE
	case GAMMA_METHOD_VIDMODE:
		vidmode_free(&state->vidmode);
		break;
#endif
#ifdef ENABLE_WINGDI
	case GAMMA_METHOD_WINGDI:
		w32gdi_free(&state->w32gdi);
		break;
#endif
	default:
		break;
	}
}

/* Set temperature with the appropriate adjustment method. */
static int
gamma_state_set_temperature(gamma_state_t *state, gamma_method_t method,
			    int temp, float gamma[3])
{
	switch (method) {
#ifdef ENABLE_RANDR
	case GAMMA_METHOD_RANDR:
		return randr_set_temperature(&state->randr, temp, gamma);
#endif
#ifdef ENABLE_VIDMODE
	case GAMMA_METHOD_VIDMODE:
		return vidmode_set_temperature(&state->vidmode, temp, gamma);
#endif
#ifdef ENABLE_WINGDI
	case GAMMA_METHOD_WINGDI:
		return w32gdi_set_temperature(&state->w32gdi, temp, gamma);
#endif
	default:
		break;
	}

	return -1;
}


/* Calculate color temperature for the specified solar elevation. */
static int
calculate_temp(double elevation, int temp_day, int temp_night,
	       int verbose)
{
	int temp = 0;
	if (elevation < TRANSITION_LOW) {
		temp = temp_night;
		if (verbose) printf(_("Period: Night\n"));
	} else if (elevation < TRANSITION_HIGH) {
		/* Transition period: interpolate */
		float a = (TRANSITION_LOW - elevation) /
			(TRANSITION_LOW - TRANSITION_HIGH);
		temp = (1.0-a)*temp_night + a*temp_day;
		if (verbose) {
			printf(_("Period: Transition (%.2f%% day)\n"), a*100);
		}
	} else {
		temp = temp_day;
		if (verbose) printf(_("Period: Daytime\n"));
	}

	return temp;
}


static void
print_help(const char *program_name)
{
	/* TRANSLATORS: help output 1
	   LAT is latitude, LON is longitude,
	   DAY is temperature at daytime,
	   NIGHT is temperature at night
	   no-wrap */
	printf(_("Usage: %s -l LAT:LON -t DAY:NIGHT [OPTIONS...]\n"),
		program_name);
	fputs("\n", stdout);

	/* TRANSLATORS: help output 2
	   no-wrap */
	fputs(_("Set color temperature of display"
		" according to time of day.\n"), stdout);
	fputs("\n", stdout);

	/* TRANSLATORS: help output 3
	   no-wrap */
	fputs(_("  -h\t\tDisplay this help message\n"
		"  -v\t\tVerbose output\n"), stdout);
	fputs("\n", stdout);	

	/* TRANSLATORS: help output 4
	   no-wrap */
	fputs(_("  -g R:G:B\tAdditional gamma correction to apply\n"
		"  -l LAT:LON\tYour current location\n"
		"  -m METHOD\tMethod to use to set color temperature"
		" (RANDR, VidMode or WinGDI)\n"
		"  -o\t\tOne shot mode (do not continously adjust"
		" color temperature)\n"
		"  -r\t\tDisable temperature transitions\n"
		"  -s SCREEN\tX screen to apply adjustments to\n"
		"  -c CRTC\tCRTC to apply adjustments to (RANDR only)\n"
		"  -t DAY:NIGHT\tColor temperature to set at daytime/night\n"),
	      stdout);
	fputs("\n", stdout);

	/* TRANSLATORS: help output 5 */
	printf(_("Default values:\n\n"
		 "  Daytime temperature: %uK\n"
		 "  Night temperature: %uK\n"),
	       DEFAULT_DAY_TEMP, DEFAULT_NIGHT_TEMP);

	fputs("\n", stdout);

	/* TRANSLATORS: help output 6 */
	printf("Please report bugs to <%s>\n", PACKAGE_BUGREPORT);
}

/* Parse arguments. Returns non-zero if options not valid. */
int parse_options(rs_opts *opts, int argc, char *argv[])
{
	int opt;
	// Initialize to default values
	opts->lat = NAN;
	opts->lon = NAN;
	opts->temp_day = DEFAULT_DAY_TEMP;
	opts->temp_night = DEFAULT_NIGHT_TEMP;
	opts->gamma[0] = DEFAULT_GAMMA;
	opts->gamma[1] = DEFAULT_GAMMA;
	opts->gamma[2] = DEFAULT_GAMMA;
	opts->method = -1;
	opts->screen_num = -1;
	opts->crtc_num = -1;
	opts->transition = 1;
	opts->one_shot = 0;
	opts->verbose = 0;
	char *s;

	while ((opt = getopt(argc, argv, "c:g:hl:m:ors:t:v")) != -1) {
		switch (opt) {
		case 'c':
			opts->crtc_num = atoi(optarg);
			break;
		case 'g':
			s = strchr(optarg, ':');
			if (s == NULL) {
				/* Use value for all channels */
				float g = atof(optarg);
				opts->gamma[0] = opts->gamma[1] = opts->gamma[2] = g;
			} else {
				/* Parse separate value for each channel */
				*(s++) = '\0';
				opts->gamma[0] = atof(optarg); /* Red */
				char *g_s = s;
				s = strchr(s, ':');
				if (s == NULL) {
  					fputs(_("Malformed gamma argument.\n"),
					      stderr);
					fputs(_("Try `-h' for more"
						" information.\n"), stderr);
					exit(EXIT_FAILURE);
				}

				*(s++) = '\0';
				opts->gamma[1] = atof(g_s); /* Blue */
				opts->gamma[2] = atof(s); /* Green */
			}
			break;
		case 'h':
			print_help(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		case 'l':
			s = strchr(optarg, ':');
			if (s == NULL) {
				fputs(_("Malformed location argument.\n"),
				      stderr);
				fputs(_("Try `-h' for more information.\n"),
				      stderr);
				exit(EXIT_FAILURE);
			}
			*(s++) = '\0';
			opts->lat = atof(optarg);
			opts->lon = atof(s);
			break;
		case 'm':
			if (strcmp(optarg, "randr") == 0 ||
			    strcmp(optarg, "RANDR") == 0) {
#ifdef ENABLE_RANDR
				opts->method = GAMMA_METHOD_RANDR;
#else
				fputs(_("RANDR method was not"
					" enabled at compile time.\n"),
				      stderr);
				exit(EXIT_FAILURE);
#endif
			} else if (strcmp(optarg, "vidmode") == 0 ||
				   strcmp(optarg, "VidMode") == 0) {
#ifdef ENABLE_VIDMODE
			    opts->method = GAMMA_METHOD_VIDMODE;
#else
				fputs(_("VidMode method was not"
					" enabled at compile time.\n"),
				      stderr);
				exit(EXIT_FAILURE);
#endif
			} else if (strcmp(optarg, "wingdi") == 0 ||
				   strcmp(optarg, "WinGDI") == 0) {
#ifdef ENABLE_WINGDI
				opts->method = GAMMA_METHOD_WINGDI;
#else
				fputs(_("WinGDI method was not"
					" enabled at compile time.\n"),
				      stderr);
				exit(EXIT_FAILURE);
#endif
			} else {
				/* TRANSLATORS: This refers to the method
				   used to adjust colors e.g VidMode */
				fprintf(stderr, _("Unknown method `%s'.\n"),
					optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'o':
			opts->one_shot = 1;
			break;
		case 'r':
			opts->transition = 0;
			break;
		case 's':
			opts->screen_num = atoi(optarg);
			break;
		case 't':
			s = strchr(optarg, ':');
			if (s == NULL) {
				fputs(_("Malformed temperature argument.\n"),
				      stderr);
				fputs(_("Try `-h' for more information.\n"),
				      stderr);
				exit(EXIT_FAILURE);
			}
			*(s++) = '\0';
			opts->temp_day = atoi(optarg);
			opts->temp_night = atoi(s);
			break;
		case 'v':
			opts->verbose = 1;
			break;
		case '?':
			fputs(_("Try `-h' for more information.\n"), stderr);
			exit(EXIT_FAILURE);
			break;
		}
	}

	/* Latitude and longitude must be set */
	if (isnan(opts->lat) || isnan(opts->lon)) {
		fputs(_("Latitude and longitude must be set.\n"), stderr);
		fputs(_("Try `-h' for more information.\n"), stderr);
		exit(EXIT_FAILURE);
	}

	if (opts->verbose) {
		/* TRANSLATORS: Append degree symbols if possible. */
		printf(_("Location: %f, %f\n"), opts->lat, opts->lon);
	}

	/* Latitude */
	if (opts->lat < MIN_LAT || opts->lat > MAX_LAT) {
		/* TRANSLATORS: Append degree symbols if possible. */
		fprintf(stderr,
			_("Latitude must be between %.1f and %.1f.\n"),
			MIN_LAT, MAX_LAT);
		exit(EXIT_FAILURE);
	}

	/* Longitude */
	if (opts->lon < MIN_LON || opts->lon > MAX_LON) {
		/* TRANSLATORS: Append degree symbols if possible. */
		fprintf(stderr,
			_("Longitude must be between %.1f and %.1f.\n"),
			MIN_LON, MAX_LON);
		exit(EXIT_FAILURE);
	}

	/* Color temperature at daytime */
	if (opts->temp_day < MIN_TEMP || opts->temp_day >= MAX_TEMP) {
		fprintf(stderr,
			_("Temperature must be between %uK and %uK.\n"),
			MIN_TEMP, MAX_TEMP);
		exit(EXIT_FAILURE);
	}

	/* Color temperature at night */
	if (opts->temp_night < MIN_TEMP || opts->temp_night >= MAX_TEMP) {
		fprintf(stderr,
			_("Temperature must be between %uK and %uK.\n"),
			MIN_TEMP, MAX_TEMP);
		exit(EXIT_FAILURE);
	}

	/* Gamma */
	if (opts->gamma[0] < MIN_GAMMA || opts->gamma[0] > MAX_GAMMA ||
	    opts->gamma[1] < MIN_GAMMA || opts->gamma[1] > MAX_GAMMA ||
	    opts->gamma[2] < MIN_GAMMA || opts->gamma[2] > MAX_GAMMA) {
		fprintf(stderr,
			_("Gamma value must be between %.1f and %.1f.\n"),
			MIN_GAMMA, MAX_GAMMA);
		exit(EXIT_FAILURE);
	}

	if (opts->verbose) {
		printf(_("Gamma: %.3f, %.3f, %.3f\n"),
		       opts->gamma[0], opts->gamma[1], opts->gamma[2]);
	}

	/* CRTC can only be selected for RANDR */
	if (opts->crtc_num > -1 && opts->method != GAMMA_METHOD_RANDR) {
		fprintf(stderr, _("CRTC can only be selected"
				  " with the RANDR method.\n"));
		exit(EXIT_FAILURE);
	}
	return 0;
}

/* Initialize gamma adjustment method. If method is negative
   try all methods until one that works is found. */
int init_method(int screen_num, int crtc_num, int *method, gamma_state_t *state)
{
	int r;
#ifdef ENABLE_RANDR
	if (*method < 0 || *method == GAMMA_METHOD_RANDR) {
		/* Initialize RANDR state */
		r = randr_init(&(state->randr), screen_num, crtc_num);
		if (r < 0) {
			fputs(_("Initialization of RANDR failed.\n"), stderr);
			if (*method < 0) {
				fputs(_("Trying other method...\n"), stderr);
			} else {
				exit(EXIT_FAILURE);
			}
		} else {
			*method = GAMMA_METHOD_RANDR;
		}
	}
#endif

#ifdef ENABLE_VIDMODE
	if (*method < 0 || *method == GAMMA_METHOD_VIDMODE) {
		/* Initialize VidMode state */
		r = vidmode_init(&(state->vidmode), screen_num);
		if (r < 0) {
			fputs(_("Initialization of VidMode failed.\n"),
			      stderr);
			if (*method < 0) {
				fputs(_("Trying other method...\n"), stderr);
			} else {
				exit(EXIT_FAILURE);
			}
		} else {
			*method = GAMMA_METHOD_VIDMODE;
		}
	}
#endif

#ifdef ENABLE_WINGDI
	if (*method < 0 || *method == GAMMA_METHOD_WINGDI) {
		/* Initialize WinGDI state */
		r = w32gdi_init(&(state->w32gdi));
		if (r < 0) {
			fputs(_("Initialization of WinGDI failed.\n"),
			      stderr);
			if (*method < 0) {
				fputs(_("Trying other method...\n"), stderr);
			} else {
				exit(EXIT_FAILURE);
			}
		} else {
			*method = GAMMA_METHOD_WINGDI;
		}
	}
#endif

	/* Failure if no methods were successful at this point. */
	if (*method < 0) {
		fputs(_("No more methods to try.\n"), stderr);
		exit(EXIT_FAILURE);
	}
	return 0;
}

/* Change gamma and exit. */
int do_oneshot(rs_opts *opts, gamma_state_t *state)
{
	double now;
	int r;

	r = systemtime_get_time(&now);
	if (r < 0) {
		fputs(_("Unable to read system time.\n"), stderr);
		gamma_state_free(state, opts->method);
		exit(EXIT_FAILURE);
	}

	/* Current angular elevation of the sun */
	double elevation = solar_elevation(now, opts->lat, opts->lon);

	if (opts->verbose) {
		/* TRANSLATORS: Append degree symbol if possible. */
		printf(_("Solar elevation: %f\n"), elevation);
	}

	/* Use elevation of sun to set color temperature */
	int temp = calculate_temp(elevation, opts->temp_day, opts->temp_night,
				  opts->verbose);

	if (opts->verbose) printf(_("Color temperature: %uK\n"), temp);

	/* Adjust temperature */
	r = gamma_state_set_temperature(state, opts->method, temp, opts->gamma);
	if (r < 0) {
		fputs(_("Temperature adjustment failed.\n"), stderr);
		gamma_state_free(state, opts->method);
		exit(EXIT_FAILURE);
	}
	return 0;
}

/* Change gamma continuously. */
int do_continous(rs_opts *opts, gamma_state_t *state)
{
	int r;
	/* Transition state */
	double short_trans_end = 0;
	int short_trans = 0;
	int short_trans_done = 0;

	/* Make an initial transition from 6500K */
	int short_trans_create = 1;
	int short_trans_begin = 1;
	int short_trans_len = 10;

	/* Amount of adjustment to apply. At zero the color
	   temperature will be exactly as calculated, and at one it
	   will be exactly 6500K. */
	float adjustment_alpha = 0.0;

#ifdef HAVE_SYS_SIGNAL_H
	struct sigaction sigact;
	sigset_t sigset;
	sigemptyset(&sigset);

	/* Install signal handler for INT and TERM signals */
	sigact.sa_handler = sigexit;
	sigact.sa_mask = sigset;
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);

	/* Install signal handler for USR1 singal */
	sigact.sa_handler = sigdisable;
	sigact.sa_mask = sigset;
	sigact.sa_flags = 0;
	sigaction(SIGUSR1, &sigact, NULL);
#endif /* HAVE_SYS_SIGNAL_H */

	/* Continously adjust color temperature */
	int done = 0;
	int disabled = 0;
	while (1) {
		/* Check to see if disable signal was caught */
		if (disable) {
			short_trans_create = 1;
			short_trans_len = 2;
			if (!disabled) {
				/* Transition to disabled state */
				short_trans_begin = 0;
				adjustment_alpha = 1.0;
				disabled = 1;
			} else {
				/* Transition back to enabled */
				short_trans_begin = 1;
				adjustment_alpha = 0.0;
				disabled = 0;
			}
			disable = 0;
		}

		/* Check to see if exit signal was caught */
		if (exiting) {
			if (done) {
				/* On second signal stop the
				   ongoing transition */
				short_trans = 0;
			} else {
				if (!disabled) {
					/* Make a short transition
					   back to 6500K */
					short_trans_create = 1;
					short_trans_begin = 0;
					short_trans_len = 2;
					adjustment_alpha = 1.0;
				}

				done = 1;
			}
			exiting = 0;
		}

		/* Read timestamp */
		double now;
		r = systemtime_get_time(&now);
		if (r < 0) {
			fputs(_("Unable to read system time.\n"),
				  stderr);
			gamma_state_free(state, opts->method);
			exit(EXIT_FAILURE);
		}

		/* Set up a new transition */
		if (short_trans_create) {
			if (opts->transition) {
				short_trans_end = now;
				short_trans_end += short_trans_len;
				short_trans = 1;
				short_trans_create = 0;
			} else {
				short_trans_done = 1;
			}
		}

		/* Current angular elevation of the sun */
		double elevation = solar_elevation(now, opts->lat, opts->lon);

		/* Use elevation of sun to set color temperature */
		int temp = calculate_temp(elevation, opts->temp_day,
					  opts->temp_night, opts->verbose);

		/* Ongoing short transition */
		if (short_trans) {
			double start = now;
			double end = short_trans_end;

			if (start > end) {
				/* Transisiton done */
				short_trans = 0;
				short_trans_done = 1;
			}

			/* Calculate alpha */
			adjustment_alpha = (end - start) /
				(float)short_trans_len;
			if (!short_trans_begin) {
				adjustment_alpha =
					1.0 - adjustment_alpha;
			}

			/* Clamp alpha value */
			adjustment_alpha =
				MAX(0.0, MIN(adjustment_alpha, 1.0));
		}

		/* Handle end of transition */
		if (short_trans_done) {
			if (disabled) {
				/* Restore saved gamma ramps */
				gamma_state_restore(state, opts->method);
			}
			short_trans_done = 0;
		}

		/* Interpolate between 6500K and calculated
		   temperature */
		temp = adjustment_alpha*6500 +
			(1.0-adjustment_alpha)*temp;

		/* Quit loop when done */
		if (done && !short_trans) break;

		if (opts->verbose) {
			printf(_("Color temperature: %uK\n"), temp);
		}

		/* Adjust temperature */
		if (!disabled || short_trans) {
			r = gamma_state_set_temperature(state,
							opts->method,
							temp, opts->gamma);
			if (r < 0) {
				fputs(_("Temperature adjustment"
					" failed.\n"), stderr);
				gamma_state_free(state, opts->method);
				exit(EXIT_FAILURE);
			}
		}

		/* Sleep for a while */
#ifndef _WIN32
		if (short_trans) usleep(100000);
		else usleep(5000000);
#else /* ! _WIN32 */
		if (short_trans) Sleep(100);
		else Sleep(5000);
#endif /* ! _WIN32 */
	}

	/* Restore saved gamma ramps */
	gamma_state_restore(state, opts->method);
	return 0;
}

int main(int argc, char *argv[])
{
#ifdef ENABLE_NLS
	/* Init locale */
	setlocale(LC_CTYPE, "");
	setlocale(LC_MESSAGES, "");

	/* Internationalisation */
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
#endif
	int r;
	rs_opts opts;
	gamma_state_t state;

	r = parse_options(&opts,argc,argv);
	if( r != 0)
		exit(EXIT_FAILURE);
	r = init_method(opts.screen_num,opts.crtc_num,
			&(opts.method),&state);
	if( r != 0)
		exit(EXIT_FAILURE);

	if (opts.one_shot) {
		if(do_oneshot(&opts,&state) != 0)
			exit(EXIT_FAILURE);
	} else {
#if defined(ENABLE_GTK) || defined(ENABLE_WINGUI)
		//if(do_continous(&opts,&state) != 0)
		//	exit(EXIT_FAILURE);
		redshift_gui(&opts,&state,argc,argv);
#endif
	}

	/* Clean up gamma adjustment state */
	gamma_state_free(&state, opts.method);

	return EXIT_SUCCESS;
}
