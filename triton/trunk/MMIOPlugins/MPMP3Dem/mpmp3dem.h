
#ifndef __MPMP3DEM_H__
#define __MPMP3DEM_H__

/*
 Get TAG message:
 Will return a pointer to mp3demuxMP3TAG_t in *pParm1
 */
#define MMIO_MP3DEMUX_MSG_GETTAG    0

/* Note, that every field is one character more than in the TAG. */
/* It's because to have a trailing zero at the end of strings every time. */
typedef struct mp3demuxMP3TAG_s
{
  char achTitle[31];
  char achArtist[31];
  char achAlbum[31];
  char achYear[5];
  char achComment[31];
  char chGenre;
} mp3demuxMP3TAG_t, *mp3demuxMP3TAG_p;

#endif
