#include "colors.h"

#include <curses.h>

#ifndef _WIN32
#include <math.h>
#endif


namespace
  {
  short conv_rgb(int clr)
    {
    float frac = (float)clr / 255.f;
    return (short)(1000.f*frac);
    }

  struct rgb
    {
    rgb(int red, int green, int blue) : r(red), g(green), b(blue) {}
    int r, g, b;
    };

  void init_color(short id, rgb value)
    {
    ::init_color(id, conv_rgb(value.r), conv_rgb(value.g), conv_rgb(value.b));
    }

  rgb invert_color(rgb value)
    {
    return rgb(255 - value.r, 255 - value.g, 255 - value.b);
    }

  rgb get_text_color(rgb default_text_color, rgb background_color)
    {
    /*
    int default_score = abs(default_text_color.r - background_color.r) + abs(default_text_color.g - background_color.g) + abs(default_text_color.b - background_color.b);
    rgb inverted[8];
    int current_score = 0;
    int current_choice = 0;
    for (int i = 0; i < 8; ++i)
      {
      inverted[i] = invert_color(default_text_color, i);
      int inverted_score = abs(inverted[i].r - background_color.r) + abs(inverted[i].g - background_color.g) + abs(inverted[i].b - background_color.b);
      if (inverted_score > current_score)
        {
        current_score = inverted_score;
        current_choice = i;
        }
      }    
    return inverted[current_choice];
    */
    int default_score = abs(default_text_color.r - background_color.r) + abs(default_text_color.g - background_color.g) + abs(default_text_color.b - background_color.b);
    rgb inverted = invert_color(default_text_color);
    int inverted_score = abs(inverted.r - background_color.r) + abs(inverted.g - background_color.g) + abs(inverted.b - background_color.b);
    return inverted_score > default_score ? inverted : default_text_color;
    }

  struct lab
    {
    float L, A, B;
    };

  inline float f(float t)
    {
    return (t > 0.008856f) ? pow(t, 1.f / 3.f) : 7.787f*t + 16.f / 116.f;
    }

  inline float finv(float f)
    {
    float t = f * f*f;
    if (t > 0.008856f)
      return t;
    return (f - 16.f / 116.f) / 7.787f;
    }

  void RGB2LAB(int R, int G, int B, float *l, float *a, float *b)
    {
    float rf = float(R) / 255.f;
    float gf = float(G) / 255.f;
    float bf = float(B) / 255.f;

    float x = 0.412453f*rf + 0.357580f*gf + 0.180423f*bf;
    float y = 0.212671f*rf + 0.715160f*gf + 0.072169f*bf;
    float z = 0.019334f*rf + 0.119193f*gf + 0.950227f*bf;

    x /= 0.950456f;
    z /= 1.088754f;

    *l = (y > 0.008856f) ? 116.f*pow(y, 1.f / 3.f) - 16.f : 903.3f * y;
    *a = 500.f*(f(x) - f(y));
    *b = 200.f*(f(y) - f(z));
    }

  void LAB2RGB(float L, float A, float B, int *r, int *g, int *b)
    {
    float y = pow((L + 16.f) / 116.f, 3.f);
    if (y <= 0.008856f)
      y = L / 903.3f;
    float fy = f(y);
    float fx = A / 500.f + fy;
    float fz = fy - B / 200.f;

    float x = finv(fx);
    float z = finv(fz);

    x *= 0.950456f;
    z *= 1.088754f;

    float rf = float(3.24048134320053*x - 1.53715151627132*y - 0.49853632616889*z);
    float gf = float(-0.96925494999657*x + 1.87599000148989*y + 0.04155592655829*z);
    float bf = float(0.05564663913518*x - 0.20404133836651*y + 1.05731106964534*z);

    rf *= 255.f;
    gf *= 255.f;
    bf *= 255.f;

    if (rf > 255.f)
      rf = 255.f;
    if (rf < 0.f)
      rf = 0.f;
    if (gf > 255.f)
      gf = 255.f;
    if (gf < 0.f)
      gf = 0.f;
    if (bf > 255.f)
      bf = 255.f;
    if (bf < 0.f)
      bf = 0.f;

    *r = uint8_t(rf);
    *g = uint8_t(gf);
    *b = uint8_t(bf);
    }

  void rgb2lab(rgb in, lab& out)
    {
    RGB2LAB(in.r, in.g, in.b, &(out.L), &(out.A), &(out.B));
    }

  void lab2rgb(lab in, rgb& out)
    {
    LAB2RGB(in.L, in.A, in.B, &(out.r), &(out.g), &(out.b));
    }

  rgb change_intensity(rgb value, float intensity_change)
    {
    lab clr;
    rgb2lab(value, clr);
    if (clr.L >= 50.f) // light
      clr.L *= (1.f - intensity_change);
    else
      clr.L /= 2.f*(1.f - intensity_change);

    if (clr.L > 100.f)
      clr.L = 100.f;

    lab2rgb(clr, value);
    return value;
    }
  }

void init_colors(const settings& sett)
  {
  rgb text_color(sett.text_red, sett.text_green, sett.text_blue);
  rgb tag_text_color(sett.tag_text_red, sett.tag_text_green, sett.tag_text_blue);
  rgb body_color(sett.win_bg_red, sett.win_bg_green, sett.win_bg_blue);
  rgb tag_color(sett.tag_bg_red, sett.tag_bg_green, sett.tag_bg_blue);
  rgb scrb_color(sett.scrb_red, sett.scrb_green, sett.scrb_blue);
  rgb middle_color(sett.middle_red, sett.middle_green, sett.middle_blue);
  rgb right_color(sett.right_red, sett.right_green, sett.right_blue);
  rgb selection_color(sett.selection_red, sett.selection_green, sett.selection_blue);
  rgb selection_tag_color(sett.selection_tag_red, sett.selection_tag_green, sett.selection_tag_blue);

  init_color(1, text_color);
  init_color(2, body_color);
  init_color(3, tag_color);
  init_color(4, scrb_color);
  init_color(5, change_intensity(tag_color, 0.05f));
  init_color(6, change_intensity(tag_color, 0.5f));
  init_color(7, middle_color);
  init_color(8, right_color);
  init_color(9, selection_color);
  init_color(10, tag_text_color);
  init_color(11, selection_tag_color);  

  init_color(20, get_text_color(text_color, body_color));
  init_color(30, get_text_color(text_color, tag_color));

  init_color(50, get_text_color(tag_text_color, change_intensity(tag_color, 0.05f)));
  init_color(60, get_text_color(tag_text_color, change_intensity(tag_color, 0.5f)));

  init_color(70, get_text_color(text_color, middle_color));
  init_color(80, get_text_color(text_color, right_color));

  init_color(90, get_text_color(text_color, selection_color));


  init_color(11, selection_tag_color);
  init_color(110, get_text_color(text_color, selection_tag_color));


  init_pair(editor_window, 1, 2);
  init_pair(command_window, 10, 3);
  init_pair(scroll_bar, 4, 2);
  init_pair(scroll_bar_2, 5, 2);

  init_pair(top_window, 10, 3);
  init_pair(column_window, 10, 5);

  init_pair(window_icon, 4, 3);
  init_pair(modified_icon, 7, 3);
  init_pair(column_icon, 6, 5);

  init_pair(middle_drag, 70, 7);
  init_pair(right_drag, 80, 8);

  init_pair(selection, 90, 9);
  init_pair(selection_command, 110, 11);

  init_pair(highlight, 7, 2);
  init_pair(comment, 8, 2);

  init_pair(active_window, 4, 3);
  }