#include <SDL/SDL.h>

#include "simz80.h"
#include "zx81rom.h"

/* address of the pointer to the beginning of the display file */
#define D_FILE 0x400c

/* the z80 state */
static struct z80 z80;

/* the keyboard state and the memory */
static BYTE keyboard[ 9 ];
static BYTE memory[ 65536 ];

/* array to covert SDLK_* constants to row/col zx81 keyboard bits */
static BYTE sdlk2scan[ SDLK_LAST ];

/* the screen surface */
static SDL_Surface* screen;
/* the surface that holds the zx81 charset */
static SDL_Surface* charset;

/* fetches an opcode from memory */
BYTE z80_fetch( struct z80* z80, WORD a )
{
  /* opcodes fetched below 0x8000 are just read */
  if ( a < 0x8000 )
  {
    return memory[ a ];
  }

  /*
  opcodes fetched above 0x7fff are read modulo 0x8000 and as 0 if the 6th bit
  is reset
  */
  BYTE b = memory[ a & 0x7fff ];
  return ( b & 64 ) ? b : 0;
}

/* reads from memory */
BYTE z80_read( struct z80* z80, WORD a )
{
  return memory[ a ];
}

/* writes to memory */
void z80_write( struct z80* z80, WORD a, BYTE b )
{
  /* don't write to rom */
  if ( a >= 0x4000 )
  {
    memory[ a ] = b;
  }
}

/* reads from a port */
BYTE z80_in( struct z80* z80, WORD a )
{
  int i;
  
  /* any read where the 0th bit of the port is zero reads from the keyboard */
  if ( ( a & 1 ) == 0 )
  {
    /* get the keyboard row */
    a >>= 8;
    
    for ( i = 0; i < 8; i++ )
    {
      /* check the first zeroed bit to select the row */
      if ( ( a & 1 ) == 0 )
      {
        /* return the keyboard state for the row */
        return keyboard[ i ];
      }
      a >>= 1;
    }
  }
}

/* writes to a port */
void z80_out( struct z80* z80, WORD a, BYTE b )
{
  (void)z80;
  (void)a;
  (void)b;
}

/* creates a sdl surface with the zx81 character set */
static int create_charset( void )
{
  SDL_Surface* charset_rgb;
  Uint32 rmask, gmask, bmask, black, white;
  int i, addr, row, col, b;
  Uint32* pixel;
  
  /* create a rgb surface to hold 256 8x8 characters */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
  rmask = 0xff000000;
  gmask = 0x00ff0000;
  bmask = 0x0000ff00;
#else
  rmask = 0x000000ff;
  gmask = 0x0000ff00;
  bmask = 0x00ff0000;
#endif
  charset_rgb = SDL_CreateRGBSurface( SDL_SWSURFACE, 4096, 16, 32, rmask, gmask, bmask, 0 );
  
  if ( charset_rgb == NULL )
  {
    return 0;
  }
  
  /* map black and white colors */
  black = SDL_MapRGB( charset_rgb->format, 0, 0, 0 );
  white = SDL_MapRGB( charset_rgb->format, 255, 255, 255 );

  /* pixel points to the top-left pixel of the surface */
  pixel = (Uint32*)charset_rgb->pixels;
  /* addr points to the start of the characters bits */
  addr = 0x1e00;
  /* the ammount of uint32s to add to go up/down on line */
  int pitch = charset_rgb->pitch / 4;
  
  /* create the 128 characters (64 normal + 64 inverted) */
  for ( i = 0; i < 64; i++ )
  {
    for ( row = 0; row < 8; row++ )
    {
      b = rom[ addr++ ];
      
      for ( col = 0; col < 8; col++ )
      {
        if ( b & 128 )
        {
          pixel[            0 ] = black;
          pixel[            1 ] = black;
          pixel[ pitch +    0 ] = black;
          pixel[ pitch +    1 ] = black;
          
          pixel[         2048 ] = white;
          pixel[         2049 ] = white;
          pixel[ pitch + 2048 ] = white;
          pixel[ pitch + 2049 ] = white;
        }
        else
        {
          pixel[            0 ] = white;
          pixel[            1 ] = white;
          pixel[ pitch +    0 ] = white;
          pixel[ pitch +    1 ] = white;
          
          pixel[         2048 ] = black;
          pixel[         2049 ] = black;
          pixel[ pitch + 2048 ] = black;
          pixel[ pitch + 2049 ] = black;
        }
        /* advance pixel to the right */
        pixel += 2;
        b <<= 1;
      }
      /* advance pixel to the start of the next line */
      pixel += pitch * 2 - 16;
    }
    /* advance pixel to the top-left of the next character */
    pixel -= pitch * 16 - 16;
  }
  
  /* convert the rgb surface to a surface the same format of the screen */
  charset = SDL_DisplayFormat( charset_rgb );
  SDL_FreeSurface( charset_rgb );
  
  return 1;
}

/* setup the emulation state */
static void setup_emulation( void )
{
  memset( &z80, 0, sizeof( z80 ) );
  
  /* load rom with ghosting at 0x2000 */
  memcpy( memory + 0x0000, rom, 0x2000  );
  memcpy( memory + 0x2000, rom, 0x2000 );
  
  /* patch DISPLAY-5 to a return */
  memory[ 0x02b5 + 0x0000 ] = 0xc9;
  memory[ 0x02b5 + 0x2000 ] = 0xc9;
  
  /* setup the registers */
  z80.pc  = 0;
  z80.iff = 0;
  z80.af_sel = z80.regs_sel = 0;
  
  /* reset the keyboard state */
  memset( keyboard, 255, sizeof( keyboard ) );
  
  /* setup the key conversion table, 8 makes unsupported keys go to limbo */
  memset( sdlk2scan, 8 << 5, sizeof( sdlk2scan ) / sizeof( sdlk2scan[ 0 ] ) );
  
  /*
  for each supported key, set the row on the 3 most significant bits and the
  column on the 5 least significant ones
  */
  sdlk2scan[ SDLK_LSHIFT ] = 0 << 5 |  1;
  sdlk2scan[ SDLK_RSHIFT ] = 0 << 5 |  1;
  sdlk2scan[ SDLK_z ]      = 0 << 5 |  2;
  sdlk2scan[ SDLK_x ]      = 0 << 5 |  4;
  sdlk2scan[ SDLK_c ]      = 0 << 5 |  8;
  sdlk2scan[ SDLK_v ]      = 0 << 5 | 16;
  sdlk2scan[ SDLK_a ]      = 1 << 5 |  1;
  sdlk2scan[ SDLK_s ]      = 1 << 5 |  2;
  sdlk2scan[ SDLK_d ]      = 1 << 5 |  4;
  sdlk2scan[ SDLK_f ]      = 1 << 5 |  8;
  sdlk2scan[ SDLK_g ]      = 1 << 5 | 16;
  sdlk2scan[ SDLK_q ]      = 2 << 5 |  1;
  sdlk2scan[ SDLK_w ]      = 2 << 5 |  2;
  sdlk2scan[ SDLK_e ]      = 2 << 5 |  4;
  sdlk2scan[ SDLK_r ]      = 2 << 5 |  8;
  sdlk2scan[ SDLK_t ]      = 2 << 5 | 16;
  sdlk2scan[ SDLK_1 ]      = 3 << 5 |  1;
  sdlk2scan[ SDLK_2 ]      = 3 << 5 |  2;
  sdlk2scan[ SDLK_3 ]      = 3 << 5 |  4;
  sdlk2scan[ SDLK_4 ]      = 3 << 5 |  8;
  sdlk2scan[ SDLK_5 ]      = 3 << 5 | 16;
  sdlk2scan[ SDLK_0 ]      = 4 << 5 |  1;
  sdlk2scan[ SDLK_9 ]      = 4 << 5 |  2;
  sdlk2scan[ SDLK_8 ]      = 4 << 5 |  4;
  sdlk2scan[ SDLK_7 ]      = 4 << 5 |  8;
  sdlk2scan[ SDLK_6 ]      = 4 << 5 | 16;
  sdlk2scan[ SDLK_p ]      = 5 << 5 |  1;
  sdlk2scan[ SDLK_o ]      = 5 << 5 |  2;
  sdlk2scan[ SDLK_i ]      = 5 << 5 |  4;
  sdlk2scan[ SDLK_u ]      = 5 << 5 |  8;
  sdlk2scan[ SDLK_y ]      = 5 << 5 | 16;
  sdlk2scan[ SDLK_RETURN ] = 6 << 5 |  1;
  sdlk2scan[ SDLK_l ]      = 6 << 5 |  2;
  sdlk2scan[ SDLK_k ]      = 6 << 5 |  4;
  sdlk2scan[ SDLK_j ]      = 6 << 5 |  8;
  sdlk2scan[ SDLK_h ]      = 6 << 5 | 16;
  sdlk2scan[ SDLK_SPACE ]  = 7 << 5 |  1;
  sdlk2scan[ SDLK_PERIOD ] = 7 << 5 |  2;
  sdlk2scan[ SDLK_m ]      = 7 << 5 |  4;
  sdlk2scan[ SDLK_n ]      = 7 << 5 |  8;
  sdlk2scan[ SDLK_b ]      = 7 << 5 | 16;
}

static void run_some( void )
{
  int count;
  
  /*
  execute 100000 z80 instructions; the less instructions we execute here the
  slower the emulation gets, the more we execute the less responsive the
  keyboard gets
  */
  for ( count = 0; count < 100000; count++ )
  {
    z80_step( &z80 );
  }
}

static int consume_events( void )
{
  /* the event to process the window manager events */
  SDL_Event event;
  /* the resulting scan of a key */
  BYTE scan;
  
  /* empty the event queue */
  while ( SDL_PollEvent( &event ) )
  {
    switch ( event.type )
    {
    case SDL_KEYDOWN:
      /* key pressed, reset the corresponding bit in the keyboard state */
      if ( event.key.keysym.sym == SDLK_BACKSPACE )
      {
        keyboard[ 0 ] &= ~1;
        keyboard[ 4 ] &= ~1;
      }
      else
      {
        scan = sdlk2scan[ event.key.keysym.sym ];
        keyboard[ scan >> 5 ] &= ~( scan & 0x1f );
      }
      break;
      
    case SDL_KEYUP:
      /* key released, set the corresponding bit in the keyboard state */
      if ( event.key.keysym.sym == SDLK_BACKSPACE )
      {
        keyboard[ 0 ] |= 1;
        keyboard[ 4 ] |= 1;
      }
      else
      {
        scan = sdlk2scan[ event.key.keysym.sym ];
        keyboard[ scan >> 5 ] |= scan & 0x1f;
      }
      break;
      
    case SDL_QUIT:
      /* quit the emulation */
      return 0;
    }
  }
  
  return 1;
}

static void update_screen( void )
{
  /* a pointer to the display file */
  WORD d_file;
  /* rects to blit from the charset to the screen */
  SDL_Rect source, dest;
  /* counters to redraw the screen */
  int row, col;
  
  /* setup invariants of the rect to address characters in the charset */
  source.y = 0;
  source.w = 16;
  source.h = 16;
  
  /* get the pointer into the display file */
  d_file = memory[ D_FILE ] | memory[ D_FILE + 1 ] << 8;
  
  /*
  redraw the screen; we could maintain a copy of the display file to avoid
  unnecessary blits
  */
  dest.y = 0;

  for ( row = 0; row < 24; row++ )
  {
    dest.x = 0;
    
    for ( col = 0; col < 32; col++ )
    {
      source.x = memory[ ++d_file ] * 16;
      SDL_BlitSurface( charset, &source, screen, &dest );
      dest.x += 16;
    }
    
    /* skip the 0x76 at the end of the line */
    d_file++;
    dest.y += 16;
  }
  
  SDL_UpdateRect( screen, 0, 0, 0, 0 );
}

int main( int argc, char *argv[] )
{
  int dont_quit;
  
  /* create our 512x384 screen; the bpp will be the same as the desktop */
  screen = SDL_SetVideoMode( 512, 384, 0, SDL_SWSURFACE );

  if ( screen == NULL )
  {
    fprintf( stderr, "Unable to set 512x384 video: %s\n", SDL_GetError() );
    return 1;
  }

  /* create the characters */
  if ( !create_charset() )
  {
    SDL_FreeSurface( screen );
    fprintf( stderr, "Unable to create charset image: %s\n", SDL_GetError() );
    return 1;
  }
  
  
  /* initialize the state */
  setup_emulation();
  
  /* emulate! */
  do
	{
    run_some();
    dont_quit = consume_events();
    update_screen();
	}
  while ( dont_quit );
  
  SDL_FreeSurface( screen );

  return 0;
}
