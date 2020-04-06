#include <SDL.h>
#include <curses.h>
#include <stdlib.h>
#include <time.h>
extern "C"
  {
#include <sdl2/pdcsdl.h>
  }

int main(int, char**)
  {
  char inp[60];
  int i, j, seed;

  seed = (int)time((time_t *)0);
  srand(seed);

  /* Initialize SDL */

  if (SDL_Init(SDL_INIT_VIDEO) < 0)
    exit(1);

  atexit(SDL_Quit);

  pdc_window = SDL_CreateWindow("PDCurses for SDL", SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED, 640, 480, 0);
  pdc_screen = SDL_GetWindowSurface(pdc_window);

  /* Initialize PDCurses */

  pdc_yoffset = 416;  /* 480 - 4 * 16 */

  initscr();
  start_color();
  scrollok(stdscr, TRUE);

  PDC_set_title("PDCurses for SDL");

  /* Do some SDL stuff */

  for (i = 640, j = 416; j; i -= 2, j -= 2)
    {
    SDL_Rect dest;

    dest.x = (640 - i) / 2;
    dest.y = (416 - j) / 2;
    dest.w = i;
    dest.h = j;

    SDL_FillRect(pdc_screen, &dest,
      SDL_MapRGB(pdc_screen->format, rand() % 256,
        rand() % 256, rand() % 256));
    }

  SDL_UpdateWindowSurface(pdc_window);

  /* Do some curses stuff */
  move(0, 0);
  init_pair(1, COLOR_WHITE + 8, COLOR_BLUE);
  bkgd(COLOR_PAIR(1));

  addstr("This is a demo of ");
  attron(A_UNDERLINE);
  addstr("PDCurses for SDL");
  attroff(A_UNDERLINE);
  addstr(".\nYour comments here: ");
  getnstr(inp, 59);
  addstr("Press any key to exit.");

  getch();
  endwin();

  return 0;
  }