#include <config.h>

/* cPlayback::GetAudioFlags() */
#define PLAYBACK_AUDIO_DESCRIPTION	0x01	/* audio description for the visually impaired */
#define PLAYBACK_AUDIO_DEPENDENT	0x02	/* only to be mixed with another track */

#if HAVE_ARM_HARDWARE
#include "../libarmbox/playback_libeplayer3.h"
#elif HAVE_MIPS_HARDWARE
#include "../libmipsbox/playback_libeplayer3.h"
#elif HAVE_GENERIC_HARDWARE
#include "../libgeneric-pc/playback_lib.h"
#else
#error no valid hardware defined
#endif
