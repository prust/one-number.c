#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <errno.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_GLFW_GL3_IMPLEMENTATION
#define NK_KEYSTATE_BASED_INPUT
#include "nuklear.h"
#include "nuklear_glfw_gl3.h"

#include "csv.h"

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024

#define STATUS_COL 0
#define DATE_COL 1
#define DESC_COL 2
#define DEBIT_COL 3
#define CREDIT_COL 4
#define NAME_COL 5

// safe string copying from https://stackoverflow.com/a/41885173/194758
#ifndef __APPLE__
char *strlcpy(char *dest, const char *src, size_t n) {
    if (size > 0) {
        size_t i;
        for (i = 0; i < size - 1 && src[i]; i++) {
             dest[i] = src[i];
        }
        dest[i] = '\0';
    }
    return dest;
}
#endif

static void error_callback(int e, const char *d) {
  printf("Error %d: %s\n", e, d);
}

#define MAX_DESC_LEN 40
#define DESC_BUFF_SIZE MAX_DESC_LEN + 1 // +1 for null-terminating byte


typedef struct Tran {
  int32_t amount; // amount is an integer (cents), to avoid floating point precision issues
  char desc[DESC_BUFF_SIZE];
  uint32_t date; // dates are represented as YYYYMMDD integers
} Tran;

Tran* transactions;
int transactions_len = 32; // start out small so array resizing is always exercised
int num_transactions = 0;

bool parsing_headers;
int curr_col = 0;

void col_cb(void *s, size_t len, void *data) {
  int col = curr_col;
  int year;
  int month;
  int day;
  char* str = s;
  char* end_ch;
  char* cents_end_ch;

  curr_col++;

  Tran* tran = &transactions[num_transactions];

  printf("%d: %s\n", col, str);
  if (col == DESC_COL) {
    strlcpy(tran->desc, str, DESC_BUFF_SIZE);
  }
  else if (col == DEBIT_COL) {
    errno = 0;
    // parse dollars first (* 100 to convert to cents)
    tran->amount = strtol(str, &end_ch, 10) * 100;
    if (errno) {
      fprintf(stderr, "amount parse error %d: %s (%s)\n", errno, strerror(errno), str);
      return;
    }

    // parse cents
    if (*end_ch != '.') {
      fprintf(stderr, "expected '.' in amount (%s)\n", end_ch);
      return;
    }
    // advance past decimal
    end_ch++;
    tran->amount += strtol(end_ch, &cents_end_ch, 10);
    if (errno) {
      fprintf(stderr, "cents parse error %d: %s\n", errno, strerror(errno));
      return;
    }
  }
  else if (col == DATE_COL) {
    errno = 0;
    month = strtol(str, &end_ch, 10);
    if (errno) {
      fprintf(stderr, "month parse error %d: %s (%s)\n", errno, strerror(errno), str);
      return;
    }
    if (*end_ch != '/') {
      fprintf(stderr, "expected '/' after month (%s)\n", end_ch);
      return;
    }
    end_ch++;
    
    day = strtol(end_ch, &end_ch, 10);
    if (errno) {
      fprintf(stderr, "date parse error %d: %s (%s)\n", errno, strerror(errno), str);
      return;
    }
    if (*end_ch != '/') {
      fprintf(stderr, "expected '/' after day (%s)\n", end_ch);
      return;
    }
    end_ch++;

    year = strtol(end_ch, &end_ch, 10);
    if (errno) {
      fprintf(stderr, "year parse error %d: %s (%s)\n", errno, strerror(errno), str);
      return;
    }
    // store dates as ints in YYYYMMDD format
    tran->date = year * 10000 + month * 100 + day;
  }
}

void row_cb(int c, void *data) {
  curr_col = 0;

  // parsing the headers is mostly a no-op, just set the flag so we know we're done
  if (parsing_headers) {
    parsing_headers = false;
    return;
  }

  num_transactions++;

  // if we've maxed out the space available, double it
  if (num_transactions == transactions_len) {
    printf("maxed out available size, doubling...\n");
    transactions_len *= 2;
    transactions = realloc(transactions, transactions_len * sizeof(Tran));
    
    if (!transactions) {
      fprintf(stderr, "Unable to reallocate space for %d transactions\n", transactions_len);
      exit(1);
    }
  }
}

void parseCSV(const char* path) {
  parsing_headers = true;
  FILE* file = fopen(path, "rb");
  char buf[1024];
  struct csv_parser parser;
  size_t bytes_read;

  if (csv_init(&parser, CSV_APPEND_NULL) != 0) {
    fprintf(stderr, "Error initializing CSV parser\n");
    exit(1);
  }

  while ((bytes_read = fread(buf, 1, 1024, file)) > 0) {
    if (csv_parse(&parser, buf, bytes_read, col_cb, row_cb, NULL) != bytes_read) {
      fprintf(stderr, "Error parsing the file: %s\n", csv_strerror(csv_error(&parser)));
      exit(1);
    }
  }
  csv_fini(&parser, col_cb, row_cb, NULL);
  fclose(file);
  csv_free(&parser);
}

int compare(const void *a, const void *b) {
  return ((Tran*)b)->amount - ((Tran*)a)->amount;
}

void drop_callback(GLFWwindow* window, int count, const char** paths) {
  for (int i = 0; i < count; i++) {
    parseCSV(paths[i]);
  }

  qsort(transactions, num_transactions, sizeof(Tran), compare);

  for (int i = 0; i < num_transactions; ++i)
    printf("%d\n", transactions[i].amount);
}

int main(void) {
  // init an array at the starting size
  transactions = malloc(transactions_len * sizeof(Tran));

  // necessary to get *nix to use commas in sprintf()
  setlocale(LC_NUMERIC, "");

  /* Platform */
  static GLFWwindow *win;
  int width = 0, height = 0;
  struct nk_context *ctx;
  struct nk_colorf bg;

  /* GLFW */
  glfwSetErrorCallback(error_callback);
  if (!glfwInit()) {
    fprintf(stdout, "[GFLW] failed to init!\n");
    exit(1);
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  win = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "One Number", NULL, NULL);
  glfwMakeContextCurrent(win);
  glfwGetWindowSize(win, &width, &height);

  /* OpenGL */
  glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
  glewExperimental = 1;
  if (glewInit() != GLEW_OK) {
    fprintf(stderr, "Failed to setup GLEW\n");
    exit(1);
  }

  ctx = nk_glfw3_init(win, NK_GLFW3_INSTALL_CALLBACKS);
  /* Load Fonts: if none of these are loaded a default font will be used  */
  /* Load Cursor: if you uncomment cursor loading please hide the cursor */
  struct nk_font_atlas *atlas;
  nk_glfw3_font_stash_begin(&atlas);
  struct nk_font_config config = nk_font_config(72);
  config.oversample_h = 4;
  config.oversample_v = 4;
  // pass in &config as the final parameter

  struct nk_font *museo_title = nk_font_atlas_add_from_file(atlas, "../MuseoSans_100.ttf", 72, &config);
  config.size = 20;
  struct nk_font *museo_bold = nk_font_atlas_add_from_file(atlas, "../MuseoSans_500.ttf", 20, &config);
  struct nk_font *museo = nk_font_atlas_add_from_file(atlas, "../MuseoSans_100.ttf", 20, &config);

  if (!museo || !museo_bold || !museo_title) {
    fprintf(stderr, "Museo font failed to load\n");
    exit(1);
  }

  nk_glfw3_font_stash_end();

  struct nk_color table[NK_COLOR_COUNT];
  table[NK_COLOR_TEXT] = nk_rgba(70, 70, 70, 255);
  table[NK_COLOR_WINDOW] = nk_rgba(255, 255, 255, 255);
  table[NK_COLOR_HEADER] = nk_rgba(175, 175, 175, 255);
  table[NK_COLOR_BORDER] = nk_rgba(0, 0, 0, 255);
  table[NK_COLOR_BUTTON] = nk_rgba(255, 255, 255, 255);
  table[NK_COLOR_BUTTON_HOVER] = nk_rgba(170, 170, 170, 255);
  table[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(160, 160, 160, 255);
  table[NK_COLOR_TOGGLE] = nk_rgba(150, 150, 150, 255);
  table[NK_COLOR_TOGGLE_HOVER] = nk_rgba(120, 120, 120, 255);
  table[NK_COLOR_TOGGLE_CURSOR] = nk_rgba(175, 175, 175, 255);
  table[NK_COLOR_SELECT] = nk_rgba(255, 255, 255, 255);
  table[NK_COLOR_SELECT_ACTIVE] = nk_rgba(200, 200, 200, 255);
  table[NK_COLOR_SLIDER] = nk_rgba(190, 190, 190, 255);
  table[NK_COLOR_SLIDER_CURSOR] = nk_rgba(80, 80, 80, 255);
  table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba(70, 70, 70, 255);
  table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba(60, 60, 60, 255);
  table[NK_COLOR_PROPERTY] = nk_rgba(175, 175, 175, 255);
  table[NK_COLOR_EDIT] = nk_rgba(150, 150, 150, 255);
  table[NK_COLOR_EDIT_CURSOR] = nk_rgba(0, 0, 0, 255);
  table[NK_COLOR_COMBO] = nk_rgba(255, 255, 255, 255);
  table[NK_COLOR_SCROLLBAR] = nk_rgba(180, 180, 180, 255);
  table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(140, 140, 140, 255);
  table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(150, 150, 150, 255);
  table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(160, 160, 160, 255);
  table[NK_COLOR_TAB_HEADER] = nk_rgba(180, 180, 180, 255);
  nk_style_from_table(ctx, table);

  glfwSetDropCallback(win, drop_callback);

  int curr_month = 9;
  int curr_year = 0;

  bg.r = 0.10f, bg.g = 0.18f, bg.b = 0.24f, bg.a = 1.0f;
  while (!glfwWindowShouldClose(win))
  {
    /* Input */
    glfwPollEvents();
    nk_glfw3_new_frame();

    /* GUI */
    int width, height;
    glfwGetWindowSize(win, &width, &height);

    char str_num_transactions[30];
    char str_total_amount[30];
    char str_date[30];
    char str_desc_date[60];
    int num_filtered_trans = 0;
    int start_filter = ((2019 - curr_year) * 10000) + ((curr_month + 1) * 100);
    int end_filter = start_filter + 100;
    int amount = 0;
    for (int i = 0; i < num_transactions; ++i) {
      int date = transactions[i].date;
      if (date > start_filter && date < end_filter) {
        num_filtered_trans++;
        amount += transactions[i].amount;
      }
    }
    int dollars = amount / 100;
    int cents = amount - (dollars * 100);

    nk_style_set_font(ctx, &museo_title->handle);
    nk_flags window_flags = 0;
    if (nk_begin(ctx, "Window", nk_rect(0, 0, width, height), window_flags)) {
      nk_layout_row_dynamic(ctx, 72, 1);
      nk_label(ctx, "One Number Finances", NK_TEXT_LEFT);
      
      nk_style_set_font(ctx, &museo_bold->handle);

      nk_layout_row_dynamic(ctx, 20, 2);
      const char *months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
      curr_month = nk_combo(ctx, months, NK_LEN(months), curr_month, 25, nk_vec2(200,200));

      const char *years[] = {"2019", "2018", "2017"};
      curr_year = nk_combo(ctx, years, NK_LEN(years), curr_year, 25, nk_vec2(200,200));

      nk_layout_row_dynamic(ctx, 20, 2);
      sprintf(str_total_amount, "Profit/Loss: $%'d.%02d", dollars, cents);
      nk_label(ctx, str_total_amount, NK_TEXT_LEFT);
      sprintf(str_num_transactions, "%d transactions (of %d)", num_filtered_trans, num_transactions);
      nk_label(ctx, str_num_transactions, NK_TEXT_RIGHT);

      for (int i = 0; i < num_transactions; ++i) {
        Tran* tran = &transactions[i];
        if (tran->date < start_filter || tran->date > end_filter)
          continue;

        nk_layout_row_begin(ctx, NK_STATIC, 20, 2);

        nk_style_set_font(ctx, &museo_bold->handle);
        dollars = tran->amount / 100;
        cents = tran->amount - (dollars * 100);
        sprintf(str_total_amount, "$%'d.%02d", dollars, cents);
        nk_layout_row_push(ctx, 80);
        nk_label(ctx, str_total_amount, NK_TEXT_RIGHT);
        
        nk_style_set_font(ctx, &museo->handle);
        int year = tran->date / 10000;
        int month = (tran->date - (year * 10000)) / 100;
        int day = tran->date - (year * 10000) - (month * 100);
        sprintf(str_desc_date, "%s (%d/%d)", tran->desc, month, day);
        nk_layout_row_push(ctx, width - 80 - 50);
        nk_label(ctx, str_desc_date, NK_TEXT_LEFT);

        nk_layout_row_end(ctx);
      }

    }
    nk_end(ctx);

    /* Draw */
    glfwGetWindowSize(win, &width, &height);
    glViewport(0, 0, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(bg.r, bg.g, bg.b, bg.a);
    /* IMPORTANT: `nk_glfw_render` modifies some global OpenGL state
     * with blending, scissor, face culling, depth test and viewport and
     * defaults everything back into a default state.
     * Make sure to either a.) save and restore or b.) reset your own state after
     * rendering the UI. */
    nk_glfw3_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
    glfwSwapBuffers(win);
  }
  nk_glfw3_shutdown();
  glfwTerminate();
  return 0;
}
