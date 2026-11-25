#include <ctype.h>
#include <dirent.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <pango/pango.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef SAVE_PATH
#define SAVE_PATH "/mnt/us/extensions/bulbagotchi/save.dat"
#endif

#ifndef ASSETS_DIR
#define ASSETS_DIR "/mnt/us/extensions/bulbagotchi/assets"
#endif

#define ROOT_DIR "/mnt/us"
#define DOCS_DIR "/mnt/us/documents"
#define ALT_DOCS_DIR "/mnt/us/docs"
#define DOWNLOADS_DIR "/mnt/us/downloads"

#define TICK_INTERVAL_MS 15000
#define MAX_SPRITE_W 320
#define MAX_SPRITE_H 320
#define MAX_BOOKS 96
#define BOOKS_PER_PAGE 4
#define MAX_PATH_LEN 512
#define MAX_TITLE_LEN 128
#define MAX_EXTRACTED_TEXT (2 * 1024 * 1024)
#define PALMDOC_MAX_RECORD_OUT 4096

#define PAGE_BREAK_CHAR '\f'
#define READER_FONT_STR "Serif 13"
#define READER_MARGIN_X 16
#define READER_MARGIN_Y 10
#define READER_ABOVE_LINES 3
#define READER_BELOW_LINES 5

typedef enum {
    UI_MODE_PET = 0,
    UI_MODE_BOOK_LIST = 1,
    UI_MODE_READER = 2
} UiMode;

typedef enum {
    BOOK_KIND_TEXT = 0,
    BOOK_KIND_PDF = 1,
    BOOK_KIND_EBOOK = 2
} BookKind;

typedef struct {
    int hunger;
    int happiness;
    int energy;
    int is_sleeping;
    time_t last_seen;
} BulbaState;

typedef struct {
    char path[MAX_PATH_LEN];
    char title[MAX_TITLE_LEN];
    BookKind kind;
} BookItem;

static BulbaState pet = {25, 75, 85, 0, 0};

static GtkWidget *main_window = NULL;
static GtkWidget *top_hbox = NULL;
static GtkWidget *lbl_status = NULL;
static GtkWidget *lbl_stats = NULL;
static GtkWidget *img_bulba = NULL;

static GtkWidget *book_list_box = NULL;
static GtkWidget *btn_book_slots[BOOKS_PER_PAGE] = {NULL, NULL, NULL, NULL};

static GtkWidget *reader_frame = NULL;
static GtkWidget *reader_view = NULL;
static GtkTextBuffer *reader_buffer = NULL;

static GtkWidget *btn_top_action = NULL;
static GtkWidget *btn_hbox = NULL;
static GtkWidget *btn_slot1 = NULL;
static GtkWidget *btn_slot2 = NULL;
static GtkWidget *btn_slot3 = NULL;

static GdkPixbuf *pix_idle = NULL;
static GdkPixbuf *pix_sleep = NULL;
static GdkPixbuf *pix_eat = NULL;

static gboolean already_quitting = FALSE;
static UiMode ui_mode = UI_MODE_PET;

static int current_sprite_mode = -1;
static int last_hunger_shown = -1;
static int last_happiness_shown = -1;
static int last_energy_shown = -1;
static char last_status_msg[320] = "";
static int ticks_since_save = 0;

static BookItem books[MAX_BOOKS];
static int book_count = 0;
static int book_list_page = 0;

static int current_book_idx = -1;
static char *reader_text_buf = NULL;
static size_t reader_text_len = 0;
static size_t reader_page_pos = 0;
static size_t reader_next_pos = 0;
static int current_page_num = 1;
static int max_page_reached = 1;
static int total_pages_est = 1;
static int book_has_readable_text = 0;

static int clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static void save_game(void) {
    pet.last_seen = time(NULL);
    FILE *f = fopen(SAVE_PATH, "wb");
    if (f) {
        fwrite(&pet, sizeof(BulbaState), 1, f);
        fflush(f);
        fclose(f);
    }
    ticks_since_save = 0;
}

static void step_simulation_tick(void) {
    if (pet.is_sleeping) {
        pet.energy = clamp_int(pet.energy + 4, 0, 100);
        pet.hunger = clamp_int(pet.hunger + 2, 0, 100);
        if (pet.energy == 100) {
            pet.is_sleeping = 0;
        }
    } else {
        pet.hunger = clamp_int(pet.hunger + 3, 0, 100);
        pet.energy = clamp_int(pet.energy - 2, 0, 100);
        pet.happiness = clamp_int(pet.happiness - 1, 0, 100);
        if (pet.hunger > 75) {
            pet.happiness = clamp_int(pet.happiness - 3, 0, 100);
            pet.energy = clamp_int(pet.energy - 1, 0, 100);
        }
    }
}

static void apply_offline_progress(void) {
    if (pet.last_seen <= 0) return;
    time_t now = time(NULL);
    double elapsed = difftime(now, pet.last_seen);
    if (elapsed <= 0) return;

    int ticks = (int)(elapsed / 5.0);
    if (ticks > 10000) ticks = 10000;

    for (int i = 0; i < ticks; i++) {
        step_simulation_tick();
    }
}

static void load_game(void) {
    FILE *f = fopen(SAVE_PATH, "rb");
    if (f) {
        size_t read_items = fread(&pet, sizeof(BulbaState), 1, f);
        fclose(f);
        if (read_items == 1) {
            pet.hunger = clamp_int(pet.hunger, 0, 100);
            pet.happiness = clamp_int(pet.happiness, 0, 100);
            pet.energy = clamp_int(pet.energy, 0, 100);
            pet.is_sleeping = pet.is_sleeping ? 1 : 0;
            apply_offline_progress();
        }
    }
    pet.last_seen = time(NULL);
}

static GdkPixbuf *load_scaled_pixbuf(const char *filename) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", ASSETS_DIR, filename);

    GdkPixbuf *raw = gdk_pixbuf_new_from_file(path, NULL);
    if (!raw) return NULL;

    int w = gdk_pixbuf_get_width(raw);
    int h = gdk_pixbuf_get_height(raw);
    if (w <= 0 || h <= 0) {
        g_object_unref(raw);
        return NULL;
    }

    if (w <= MAX_SPRITE_W && h <= MAX_SPRITE_H) {
        return raw;
    }

    double scale_w = (double)MAX_SPRITE_W / (double)w;
    double scale_h = (double)MAX_SPRITE_H / (double)h;
    double scale = (scale_w < scale_h) ? scale_w : scale_h;

    int target_w = (int)(w * scale);
    int target_h = (int)(h * scale);
    if (target_w < 1) target_w = 1;
    if (target_h < 1) target_h = 1;

    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(raw, target_w, target_h, GDK_INTERP_BILINEAR);
    g_object_unref(raw);
    return scaled ? scaled : NULL;
}

static void load_sprites(void) {
    pix_idle = load_scaled_pixbuf("bulba_idle.png");
    pix_sleep = load_scaled_pixbuf("bulba_sleep.png");
    pix_eat = load_scaled_pixbuf("bulba_eat.png");
}

static void apply_sprite_mode(int mode) {
    if (!img_bulba || mode == current_sprite_mode) return;

    GdkPixbuf *pb = pix_idle;
    if (mode == 1 && pix_sleep) pb = pix_sleep;
    else if (mode == 2 && pix_eat) pb = pix_eat;

    if (pb) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(img_bulba), pb);
        current_sprite_mode = mode;
    }
}

static void set_status_text(const char *msg) {
    if (!lbl_status || !msg) return;
    if (strcmp(last_status_msg, msg) == 0) return;

    snprintf(last_status_msg, sizeof(last_status_msg), "%s", msg);

    char *escaped = g_markup_escape_text(last_status_msg, -1);
    char markup[512];
    snprintf(markup, sizeof(markup), "<span size='large'>%s</span>", escaped ? escaped : "");
    gtk_label_set_markup(GTK_LABEL(lbl_status), markup);
    g_free(escaped);
}

static void refresh_stats_label(void) {
    if (!lbl_stats) return;
    if (pet.hunger == last_hunger_shown &&
        pet.happiness == last_happiness_shown &&
        pet.energy == last_energy_shown) {
        return;
    }

    last_hunger_shown = pet.hunger;
    last_happiness_shown = pet.happiness;
    last_energy_shown = pet.energy;

    char stats_buf[256];
    snprintf(stats_buf, sizeof(stats_buf),
             "<span size='small' weight='bold'>Humor: %d%% | Fome: %d%% | Energia: %d%%</span>",
             pet.happiness, pet.hunger, pet.energy);
    gtk_label_set_markup(GTK_LABEL(lbl_stats), stats_buf);
}

static int ends_with_ci(const char *str, const char *suffix) {
    size_t slen = strlen(str);
    size_t xlen = strlen(suffix);
    if (slen < xlen) return 0;
    const char *p = str + (slen - xlen);
    for (size_t i = 0; i < xlen; i++) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_ignored_dir_name(const char *name) {
    if (name[0] == '.') return 1;
    if (strcasecmp(name, "extensions") == 0) return 1;
    if (strcasecmp(name, "kual") == 0) return 1;
    if (strcasecmp(name, "koreader") == 0) return 1;
    if (strcasecmp(name, "system") == 0) return 1;
    if (strcasecmp(name, "audible") == 0) return 1;
    if (strcasecmp(name, "tmp") == 0) return 1;
    if (strcasecmp(name, "trash") == 0) return 1;
    if (strcasecmp(name, "screenshots") == 0) return 1;
    return 0;
}

static int detect_book_kind(const char *name, BookKind *out_kind) {
    if (name[0] == '.') return 0;
    if (ends_with_ci(name, ".txt") || ends_with_ci(name, ".md")) {
        *out_kind = BOOK_KIND_TEXT;
        return 1;
    }
    if (ends_with_ci(name, ".pdf")) {
        *out_kind = BOOK_KIND_PDF;
        return 1;
    }
    if (ends_with_ci(name, ".epub") || ends_with_ci(name, ".mobi") ||
        ends_with_ci(name, ".azw") || ends_with_ci(name, ".azw3") ||
        ends_with_ci(name, ".kfx") || ends_with_ci(name, ".fb2") ||
        ends_with_ci(name, ".djvu") || ends_with_ci(name, ".cbz") ||
        ends_with_ci(name, ".cbr")) {
        *out_kind = BOOK_KIND_EBOOK;
        return 1;
    }
    return 0;
}

static void format_clean_book_title(const char *filename, char *dst, size_t dst_size) {
    char base[MAX_TITLE_LEN];
    snprintf(base, sizeof(base), "%s", filename);

    char *dot = strrchr(base, '.');
    if (dot && dot != base) {
        *dot = '\0';
    }

    size_t j = 0;
    for (size_t i = 0; base[i] != '\0' && j + 1 < dst_size; i++) {
        unsigned char c = (unsigned char)base[i];
        if (c == '&') {
            if (j + 2 < dst_size) {
                dst[j++] = 'e';
            }
        } else if (c == '_') {
            dst[j++] = ' ';
        } else if ((c >= 32 && c < 127) || c >= 128) {
            dst[j++] = (char)c;
        } else {
            dst[j++] = ' ';
        }
    }
    dst[j] = '\0';
}

static int already_added_path(const char *full_path) {
    for (int i = 0; i < book_count; i++) {
        if (strcmp(books[i].path, full_path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void scan_dir_books(const char *dir_path, int max_depth, int depth) {
    if (depth > max_depth || book_count >= MAX_BOOKS) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && book_count < MAX_BOOKS) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char full_path[MAX_PATH_LEN];
        int n = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);
        if (n < 0 || n >= (int)sizeof(full_path)) continue;

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (depth < max_depth && !is_ignored_dir_name(ent->d_name)) {
                scan_dir_books(full_path, max_depth, depth + 1);
            }
        } else if (S_ISREG(st.st_mode)) {
            BookKind kind = BOOK_KIND_TEXT;
            if (detect_book_kind(ent->d_name, &kind) && !already_added_path(full_path)) {
                BookItem *item = &books[book_count];
                snprintf(item->path, sizeof(item->path), "%s", full_path);
                format_clean_book_title(ent->d_name, item->title, sizeof(item->title));
                item->kind = kind;
                book_count++;
            }
        }
    }

    closedir(dir);
}

static void refresh_books_list(void) {
    book_count = 0;
    book_list_page = 0;
    scan_dir_books(ROOT_DIR, 0, 0);
    scan_dir_books(DOCS_DIR, 2, 0);
    scan_dir_books(ALT_DOCS_DIR, 2, 0);
    scan_dir_books(DOWNLOADS_DIR, 2, 0);
}

static int get_total_book_pages(void) {
    int pages = (book_count + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
    return pages < 1 ? 1 : pages;
}

static void refresh_book_list_page_ui(void) {
    int total_pages = get_total_book_pages();
    if (book_list_page < 0) book_list_page = 0;
    if (book_list_page >= total_pages) book_list_page = total_pages - 1;

    int start = book_list_page * BOOKS_PER_PAGE;
    for (int i = 0; i < BOOKS_PER_PAGE; i++) {
        int idx = start + i;
        if (idx < book_count) {
            gtk_button_set_label(GTK_BUTTON(btn_book_slots[i]), books[idx].title);
            gtk_widget_set_sensitive(btn_book_slots[i], TRUE);
            gtk_widget_show(btn_book_slots[i]);
        } else {
            gtk_button_set_label(GTK_BUTTON(btn_book_slots[i]), "");
            gtk_widget_set_sensitive(btn_book_slots[i], FALSE);
            gtk_widget_hide(btn_book_slots[i]);
        }
    }
}

static void free_reader_buffer_data(void) {
    if (reader_text_buf) {
        free(reader_text_buf);
        reader_text_buf = NULL;
    }
    reader_text_len = 0;
    reader_page_pos = 0;
    reader_next_pos = 0;
    current_page_num = 1;
    max_page_reached = 1;
    total_pages_est = 1;
    book_has_readable_text = 0;
}

static int escape_shell_single_quotes(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size < 3) return 0;
    size_t w = 0;
    dst[w++] = '\'';
    for (size_t i = 0; src[i] != '\0'; i++) {
        if (src[i] == '\'') {
            if (w + 4 >= dst_size) return 0;
            dst[w++] = '\'';
            dst[w++] = '\\';
            dst[w++] = '\'';
            dst[w++] = '\'';
        } else {
            if (w + 2 >= dst_size) return 0;
            dst[w++] = src[i];
        }
    }
    dst[w++] = '\'';
    dst[w] = '\0';
    return 1;
}

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

static size_t append_utf8_codepoint(char *dst, size_t cap, size_t pos, unsigned int cp) {
    if (pos + 5 >= cap) return pos;
    if (cp == 0 || cp == 0xFEFF) return pos;
    if (cp < 0x80) {
        dst[pos++] = (char)cp;
    } else if (cp < 0x800) {
        dst[pos++] = (char)(0xC0 | (cp >> 6));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        dst[pos++] = (char)(0xE0 | (cp >> 12));
        dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        dst[pos++] = (char)(0xF0 | (cp >> 18));
        dst[pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[pos++] = (char)(0x80 | (cp & 0x3F));
    }
    return pos;
}

static void decode_html_entities_inplace(char *buf, size_t *len) {
    size_t r = 0, w = 0;
    size_t cap = *len + 1;

    while (r < *len) {
        if (buf[r] == '&') {
            if (strncmp(buf + r, "&amp;", 5) == 0) {
                buf[w++] = '&'; r += 5; continue;
            }
            if (strncmp(buf + r, "&lt;", 4) == 0) {
                buf[w++] = '<'; r += 4; continue;
            }
            if (strncmp(buf + r, "&gt;", 4) == 0) {
                buf[w++] = '>'; r += 4; continue;
            }
            if (strncmp(buf + r, "&quot;", 6) == 0) {
                buf[w++] = '"'; r += 6; continue;
            }
            if (strncmp(buf + r, "&#39;", 5) == 0 || strncmp(buf + r, "&apos;", 6) == 0) {
                buf[w++] = '\'';
                r += (buf[r + 1] == '#') ? 5 : 6;
                continue;
            }
            if (strncmp(buf + r, "&nbsp;", 6) == 0 || strncmp(buf + r, "&#160;", 6) == 0) {
                buf[w++] = ' '; r += 6; continue;
            }
            if (buf[r + 1] == '#') {
                size_t k = r + 2;
                unsigned int val = 0;
                int ok = 0;
                if (k < *len && (buf[k] == 'x' || buf[k] == 'X')) {
                    k++;
                    while (k < *len && isxdigit((unsigned char)buf[k])) {
                        char h = buf[k++];
                        unsigned int d = isdigit((unsigned char)h) ? (unsigned int)(h - '0')
                                       : (unsigned int)(tolower((unsigned char)h) - 'a' + 10);
                        val = val * 16 + d;
                        ok = 1;
                    }
                } else {
                    while (k < *len && isdigit((unsigned char)buf[k])) {
                        val = val * 10 + (unsigned int)(buf[k++] - '0');
                        ok = 1;
                    }
                }
                if (ok && k < *len && buf[k] == ';') {
                    w = append_utf8_codepoint(buf, cap, w, val);
                    r = k + 1;
                    continue;
                }
            }
        }
        buf[w++] = buf[r++];
    }
    buf[w] = '\0';
    *len = w;
}

static int tag_name_matches_ci(const char *p, size_t n, const char *target) {
    size_t tlen = strlen(target);
    if (n != tlen) return 0;
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)target[i])) return 0;
    }
    return 1;
}

static void strip_html_clean_inplace(char *buf, size_t *len) {
    size_t r = 0, w = 0;
    int in_style = 0;
    int in_script = 0;

    while (r < *len) {
        if (buf[r] == PAGE_BREAK_CHAR) {
            if (w > 0 && buf[w - 1] != PAGE_BREAK_CHAR) {
                buf[w++] = PAGE_BREAK_CHAR;
            }
            r++;
            continue;
        }

        if (buf[r] == '<') {
            size_t close_pos = r + 1;
            while (close_pos < *len && buf[close_pos] != '>') {
                close_pos++;
            }
            if (close_pos >= *len) break;

            const char *tag_inner = buf + r + 1;
            size_t tag_inner_len = (size_t)(close_pos - (r + 1));
            int is_end_tag = 0;
            if (tag_inner_len > 0 && tag_inner[0] == '/') {
                is_end_tag = 1;
                tag_inner++;
                tag_inner_len--;
            }

            size_t name_len = 0;
            while (name_len < tag_inner_len &&
                   !isspace((unsigned char)tag_inner[name_len]) &&
                   tag_inner[name_len] != '/') {
                name_len++;
            }

            if (tag_name_matches_ci(tag_inner, name_len, "style")) {
                in_style = is_end_tag ? 0 : 1;
            } else if (tag_name_matches_ci(tag_inner, name_len, "script")) {
                in_script = is_end_tag ? 0 : 1;
            } else if (!in_style && !in_script) {
                int is_para_break = tag_name_matches_ci(tag_inner, name_len, "p") ||
                                    tag_name_matches_ci(tag_inner, name_len, "div") ||
                                    tag_name_matches_ci(tag_inner, name_len, "h1") ||
                                    tag_name_matches_ci(tag_inner, name_len, "h2") ||
                                    tag_name_matches_ci(tag_inner, name_len, "h3") ||
                                    tag_name_matches_ci(tag_inner, name_len, "h4") ||
                                    tag_name_matches_ci(tag_inner, name_len, "li") ||
                                    tag_name_matches_ci(tag_inner, name_len, "tr");
                int is_line_break = tag_name_matches_ci(tag_inner, name_len, "br");

                if (is_para_break && is_end_tag) {
                    if (w > 0 && buf[w - 1] != '\n' && buf[w - 1] != PAGE_BREAK_CHAR) {
                        buf[w++] = '\n';
                    }
                    if (w > 0 && buf[w - 1] == '\n' && (w < 2 || buf[w - 2] != '\n')) {
                        buf[w++] = '\n';
                    }
                } else if (is_para_break || is_line_break) {
                    if (w > 0 && buf[w - 1] != '\n' && buf[w - 1] != PAGE_BREAK_CHAR) {
                        buf[w++] = '\n';
                    }
                } else if (w > 0 && buf[w - 1] != ' ' && buf[w - 1] != '\n' && buf[w - 1] != PAGE_BREAK_CHAR) {
                    buf[w++] = ' ';
                }
            }
            r = close_pos + 1;
            continue;
        }

        if (in_style || in_script) {
            r++;
            continue;
        }

        char c = buf[r++];
        if (c == '\r') continue;
        if (c == '\n' || c == '\t') {
            if (w > 0 && buf[w - 1] != ' ' && buf[w - 1] != '\n' && buf[w - 1] != PAGE_BREAK_CHAR) {
                buf[w++] = ' ';
            }
        } else {
            buf[w++] = c;
        }
    }

    buf[w] = '\0';
    *len = w;
    decode_html_entities_inplace(buf, len);
}

static void compact_readable_text_inplace(char *buf, size_t *len) {
    size_t w = 0;
    int space_pending = 0;
    int newlines_seen = 0;

    for (size_t i = 0; i < *len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == '\r') continue;
        if (c == (unsigned char)PAGE_BREAK_CHAR) {
            space_pending = 0;
            newlines_seen = 0;
            while (w > 0 && (buf[w - 1] == '\n' || buf[w - 1] == ' ')) {
                w--;
            }
            if (w > 0 && buf[w - 1] != PAGE_BREAK_CHAR) {
                buf[w++] = PAGE_BREAK_CHAR;
            }
        } else if (c == '\n') {
            space_pending = 0;
            if (w > 0 && buf[w - 1] != PAGE_BREAK_CHAR && newlines_seen < 2) {
                buf[w++] = '\n';
                newlines_seen++;
            }
        } else if (c == ' ' || c == '\t' || c == 0 || (c < 32) || c == 127) {
            if (w > 0 && buf[w - 1] != '\n' && buf[w - 1] != PAGE_BREAK_CHAR) {
                space_pending = 1;
            }
        } else {
            if (space_pending && w > 0 &&
                buf[w - 1] != '\n' && buf[w - 1] != ' ' && buf[w - 1] != PAGE_BREAK_CHAR) {
                buf[w++] = ' ';
            }
            space_pending = 0;
            newlines_seen = 0;
            buf[w++] = (char)c;
        }
    }
    while (w > 0 && (buf[w - 1] == PAGE_BREAK_CHAR || buf[w - 1] == '\n' || buf[w - 1] == ' ')) {
        w--;
    }
    buf[w] = '\0';
    *len = w;
}

static int extract_opf_spine_order(const char *esc_path, char entries[][256], int max_entries) {
    char opf_cmd[MAX_PATH_LEN * 2 + 240];
    snprintf(opf_cmd, sizeof(opf_cmd),
             "unzip -p %s $(unzip -Z1 %s 2>/dev/null | grep -iE '\\.opf$' | head -n 1) 2>/dev/null",
             esc_path, esc_path);

    FILE *pipe = popen(opf_cmd, "r");
    if (!pipe) return 0;

    char *opf_buf = (char *)malloc(256 * 1024);
    if (!opf_buf) {
        pclose(pipe);
        return 0;
    }
    size_t n = fread(opf_buf, 1, 256 * 1024 - 1, pipe);
    pclose(pipe);
    opf_buf[n] = '\0';

    if (n < 30) {
        free(opf_buf);
        return 0;
    }

    /* Coleta pares id -> href do <manifest> */
    typedef struct {
        char id[96];
        char href[224];
    } ManifestItem;

    ManifestItem *manifest = (ManifestItem *)malloc(sizeof(ManifestItem) * 256);
    int manifest_count = 0;
    if (!manifest) {
        free(opf_buf);
        return 0;
    }

    char *p = opf_buf;
    while (*p && manifest_count < 256) {
        char *item_tag = strstr(p, "<item ");
        if (!item_tag) item_tag = strstr(p, "<item\n");
        if (!item_tag) item_tag = strstr(p, "<item\t");
        if (!item_tag) break;
        char *end_tag = strchr(item_tag, '>');
        if (!end_tag) break;

        size_t tlen = (size_t)(end_tag - item_tag);
        if (tlen < 512) {
            char tag_copy[520];
            memcpy(tag_copy, item_tag, tlen);
            tag_copy[tlen] = '\0';

            char *id_pos = strstr(tag_copy, "id=\"");
            char *href_pos = strstr(tag_copy, "href=\"");
            if (id_pos && href_pos) {
                id_pos += 4;
                href_pos += 6;
                char *id_end = strchr(id_pos, '"');
                char *href_end = strchr(href_pos, '"');
                if (id_end && href_end) {
                    size_t ilen = (size_t)(id_end - id_pos);
                    size_t hlen = (size_t)(href_end - href_pos);
                    if (ilen > 0 && ilen < sizeof(manifest[0].id) &&
                        hlen > 0 && hlen < sizeof(manifest[0].href)) {
                        memcpy(manifest[manifest_count].id, id_pos, ilen);
                        manifest[manifest_count].id[ilen] = '\0';
                        memcpy(manifest[manifest_count].href, href_pos, hlen);
                        manifest[manifest_count].href[hlen] = '\0';
                        manifest_count++;
                    }
                }
            }
        }
        p = end_tag + 1;
    }

    int count = 0;
    char *s = strstr(opf_buf, "<spine");
    if (s) {
        char *s_end = strstr(s, "</spine>");
        char *cur = s;
        while (cur && (!s_end || cur < s_end) && count < max_entries) {
            char *ir = strstr(cur, "<itemref");
            if (!ir || (s_end && ir >= s_end)) break;
            char *ir_end = strchr(ir, '>');
            if (!ir_end) break;

            char *idref_pos = strstr(ir, "idref=\"");
            if (idref_pos && idref_pos < ir_end) {
                idref_pos += 7;
                char *idref_end = strchr(idref_pos, '"');
                if (idref_end && idref_end < ir_end) {
                    size_t rlen = (size_t)(idref_end - idref_pos);
                    if (rlen > 0 && rlen < 96) {
                        char idref[96];
                        memcpy(idref, idref_pos, rlen);
                        idref[rlen] = '\0';

                        for (int m = 0; m < manifest_count; m++) {
                            if (strcmp(manifest[m].id, idref) == 0) {
                                /* Remove ancora #... se houver */
                                char href_clean[224];
                                snprintf(href_clean, sizeof(href_clean), "%s", manifest[m].href);
                                char *hash = strchr(href_clean, '#');
                                if (hash) *hash = '\0';

                                if (ends_with_ci(href_clean, ".xhtml") ||
                                    ends_with_ci(href_clean, ".html") ||
                                    ends_with_ci(href_clean, ".htm")) {
                                    snprintf(entries[count], 256, "*%s", href_clean);
                                    count++;
                                }
                                break;
                            }
                        }
                    }
                }
            }
            cur = ir_end + 1;
        }
    }

    free(manifest);
    free(opf_buf);
    return count;
}

static size_t extract_epub_text_via_unzip(const char *epub_path, char *out, size_t out_cap) {
    char esc_path[MAX_PATH_LEN * 2 + 32];
    if (!escape_shell_single_quotes(epub_path, esc_path, sizeof(esc_path))) return 0;

    char entries[128][256];
    int entry_count = extract_opf_spine_order(esc_path, entries, 128);

    if (entry_count == 0) {
        char list_cmd[MAX_PATH_LEN * 2 + 160];
        snprintf(list_cmd, sizeof(list_cmd),
                 "unzip -Z1 %s 2>/dev/null | grep -iE '\\.(xhtml|html|htm)$' | LC_ALL=C sort",
                 esc_path);

        FILE *lpipe = popen(list_cmd, "r");
        if (!lpipe) return 0;

        char line[256];
        while (entry_count < 128 && fgets(line, sizeof(line), lpipe) != NULL) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
                line[--l] = '\0';
            }
            if (l > 0 && l < sizeof(entries[0])) {
                snprintf(entries[entry_count], sizeof(entries[0]), "%s", line);
                entry_count++;
            }
        }
        pclose(lpipe);
    }

    if (entry_count == 0) return 0;

    char *raw_html = (char *)malloc(MAX_EXTRACTED_TEXT + 1);
    if (!raw_html) return 0;
    size_t raw_used = 0;
    raw_html[0] = '\0';
    int added_sections = 0;

    for (int i = 0; i < entry_count && raw_used + 2048 < MAX_EXTRACTED_TEXT; i++) {
        char esc_entry[600];
        if (!escape_shell_single_quotes(entries[i], esc_entry, sizeof(esc_entry))) continue;

        char ext_cmd[MAX_PATH_LEN * 2 + 680];
        snprintf(ext_cmd, sizeof(ext_cmd),
                 "unzip -p %s %s 2>/dev/null",
                 esc_path, esc_entry);
        FILE *epipe = popen(ext_cmd, "r");
        if (!epipe) continue;
        size_t before = raw_used;
        size_t n = fread(raw_html + raw_used, 1, MAX_EXTRACTED_TEXT - raw_used - 8, epipe);
        pclose(epipe);
        if (n > 0) {
            raw_used += n;
            char *sec_start = raw_html + before;
            size_t sec_len = raw_used - before;
            size_t check_len = sec_len;
            strip_html_clean_inplace(sec_start, &check_len);
            compact_readable_text_inplace(sec_start, &check_len);

            if (check_len > 0) {
                if (added_sections > 0 && (size_t)(sec_start - raw_html) + check_len + 4 < MAX_EXTRACTED_TEXT) {
                    memmove(sec_start + 1, sec_start, check_len + 1);
                    sec_start[0] = PAGE_BREAK_CHAR;
                    raw_used = (size_t)(sec_start - raw_html) + 1 + check_len;
                } else {
                    raw_used = (size_t)(sec_start - raw_html) + check_len;
                }
                raw_html[raw_used] = '\0';
                added_sections++;
            } else {
                raw_used = before;
                raw_html[raw_used] = '\0';
            }
        }
    }

    if (raw_used < 20) {
        free(raw_html);
        return 0;
    }

    size_t copy_len = (raw_used < out_cap - 1) ? raw_used : (out_cap - 1);
    memcpy(out, raw_html, copy_len);
    out[copy_len] = '\0';
    free(raw_html);
    return copy_len;
}

static size_t palmdoc_decompress_record(const uint8_t *in, size_t in_len,
                                        uint8_t *out, size_t out_cap) {
    size_t i = 0;
    size_t o = 0;

    while (i < in_len && o < out_cap) {
        uint8_t b = in[i++];
        if (b == 0) {
            out[o++] = 0;
        } else if (b <= 8) {
            size_t lit = (size_t)b;
            for (size_t k = 0; k < lit && i < in_len && o < out_cap; k++) {
                out[o++] = in[i++];
            }
        } else if (b < 0x80) {
            out[o++] = b;
        } else if (b < 0xC0) {
            if (i >= in_len) break;
            uint8_t b2 = in[i++];
            uint16_t pair = (uint16_t)(((uint16_t)b << 8) | (uint16_t)b2);
            size_t dist = (size_t)((pair >> 3) & 0x07FFu);
            size_t length = (size_t)((pair & 0x07u) + 3u);
            if (dist == 0 || dist > o) {
                break;
            }
            for (size_t k = 0; k < length && o < out_cap; k++) {
                out[o] = out[o - dist];
                o++;
            }
        } else {
            out[o++] = ' ';
            if (o < out_cap) {
                out[o++] = (uint8_t)(b ^ 0x80u);
            }
        }
    }

    return o;
}

static size_t extract_mobi_text(const char *mobi_path, char *out, size_t out_cap) {
    if (!mobi_path || !out || out_cap < 32) return 0;
    out[0] = '\0';

    FILE *f = fopen(mobi_path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize_long = ftell(f);
    if (fsize_long <= 100) {
        fclose(f);
        return 0;
    }
    size_t fsize = (size_t)fsize_long;
    if (fsize > 16 * 1024 * 1024) {
        fsize = 16 * 1024 * 1024;
    }
    rewind(f);

    uint8_t *file_buf = (uint8_t *)malloc(fsize);
    if (!file_buf) {
        fclose(f);
        return 0;
    }

    size_t nread = fread(file_buf, 1, fsize, f);
    fclose(f);
    if (nread < 90) {
        free(file_buf);
        return 0;
    }

    uint16_t num_records = read_be16(file_buf + 76);
    size_t rec_table_offset = 78;
    size_t rec_table_bytes = (size_t)num_records * 8;
    if (num_records < 2 || rec_table_offset + rec_table_bytes + 16 > nread) {
        free(file_buf);
        return 0;
    }

    uint32_t rec0_off = read_be32(file_buf + rec_table_offset);
    uint32_t rec1_off = read_be32(file_buf + rec_table_offset + 8);
    if ((size_t)rec0_off + 16 > nread || (size_t)rec1_off >= nread || rec1_off <= rec0_off) {
        free(file_buf);
        return 0;
    }

    const uint8_t *palmdoc = file_buf + rec0_off;
    uint16_t compression = read_be16(palmdoc + 0);
    uint32_t text_length = read_be32(palmdoc + 4);
    uint16_t text_record_count = read_be16(palmdoc + 8);
    uint16_t encryption = read_be16(palmdoc + 12);

    if (encryption != 0 || text_record_count == 0 || (compression != 1 && compression != 2)) {
        free(file_buf);
        return 0;
    }

    size_t max_rec = (size_t)text_record_count;
    if (max_rec >= (size_t)num_records) {
        max_rec = (size_t)num_records - 1;
    }

    size_t max_collect = (out_cap < MAX_EXTRACTED_TEXT) ? (out_cap - 1) : MAX_EXTRACTED_TEXT;
    if ((size_t)text_length > 0 && (size_t)text_length < max_collect) {
        max_collect = (size_t)text_length;
    }

    char *raw_html = (char *)malloc(max_collect + PALMDOC_MAX_RECORD_OUT + 1);
    uint8_t *rec_out = (compression == 2) ? (uint8_t *)malloc(PALMDOC_MAX_RECORD_OUT) : NULL;
    if (!raw_html || (compression == 2 && !rec_out)) {
        free(raw_html);
        free(rec_out);
        free(file_buf);
        return 0;
    }

    size_t raw_used = 0;
    for (size_t r = 1; r <= max_rec && raw_used < max_collect; r++) {
        size_t entry_off = rec_table_offset + r * 8;
        uint32_t start_off = read_be32(file_buf + entry_off);
        size_t end_off = nread;
        if (r + 1 < (size_t)num_records) {
            uint32_t next_off = read_be32(file_buf + entry_off + 8);
            if (next_off >= start_off && (size_t)next_off <= nread) {
                end_off = (size_t)next_off;
            }
        }
        if ((size_t)start_off >= end_off || end_off > nread) {
            continue;
        }
        size_t chunk_len = end_off - (size_t)start_off;
        const uint8_t *rec_src = file_buf + start_off;

        if (compression == 1) {
            size_t copy_n = chunk_len;
            if (raw_used + copy_n > max_collect) {
                copy_n = max_collect - raw_used;
            }
            memcpy(raw_html + raw_used, rec_src, copy_n);
            raw_used += copy_n;
        } else {
            size_t dec_len = palmdoc_decompress_record(rec_src, chunk_len,
                                                       rec_out, PALMDOC_MAX_RECORD_OUT);
            if (dec_len > 0) {
                size_t copy_n = dec_len;
                if (raw_used + copy_n > max_collect) {
                    copy_n = max_collect - raw_used;
                }
                memcpy(raw_html + raw_used, rec_out, copy_n);
                raw_used += copy_n;
            }
        }
    }

    free(rec_out);
    free(file_buf);
    raw_html[raw_used] = '\0';

    if (raw_used < 20) {
        free(raw_html);
        return 0;
    }

    strip_html_clean_inplace(raw_html, &raw_used);
    compact_readable_text_inplace(raw_html, &raw_used);

    size_t copy_len = (raw_used < out_cap - 1) ? raw_used : (out_cap - 1);
    memcpy(out, raw_html, copy_len);
    out[copy_len] = '\0';
    free(raw_html);
    return copy_len;
}

static size_t extract_pdf_raw_strings(const char *pdf_path, char *out, size_t out_cap) {
    FILE *f = fopen(pdf_path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0) {
        fclose(f);
        return 0;
    }
    if (fsize > 2 * 1024 * 1024) {
        fsize = 2 * 1024 * 1024;
    }
    rewind(f);

    char *raw = (char *)malloc((size_t)fsize + 1);
    if (!raw) {
        fclose(f);
        return 0;
    }

    size_t nread = fread(raw, 1, (size_t)fsize, f);
    fclose(f);
    raw[nread] = '\0';

    size_t out_len = 0;
    size_t i = 0;
    int last_was_space = 1;
    int printable_count = 0;

    while (i < nread && out_len + 4 < out_cap) {
        if (raw[i] == '(') {
            i++;
            while (i < nread && raw[i] != ')' && out_len + 4 < out_cap) {
                if (raw[i] == '\\' && i + 1 < nread) {
                    i++;
                    char esc = raw[i];
                    if (esc == 'n' || esc == 'r' || esc == 't') {
                        if (!last_was_space) {
                            out[out_len++] = ' ';
                            last_was_space = 1;
                        }
                    } else {
                        unsigned char c = (unsigned char)esc;
                        if ((c >= 32 && c < 127) || c >= 128) {
                            out[out_len++] = (char)c;
                            last_was_space = 0;
                            if (isalpha(c)) printable_count++;
                        }
                    }
                    i++;
                } else {
                    unsigned char c = (unsigned char)raw[i];
                    if ((c >= 32 && c < 127) || c >= 128) {
                        out[out_len++] = (char)c;
                        last_was_space = 0;
                        if (isalpha(c)) printable_count++;
                    } else if (c == '\n' || c == '\r') {
                        if (!last_was_space) {
                            out[out_len++] = ' ';
                            last_was_space = 1;
                        }
                    }
                    i++;
                }
            }
            if (!last_was_space && out_len + 2 < out_cap) {
                out[out_len++] = ' ';
                last_was_space = 1;
            }
        }
        i++;
    }

    free(raw);
    out[out_len] = '\0';
    compact_readable_text_inplace(out, &out_len);

    if (printable_count < 25) {
        out[0] = '\0';
        return 0;
    }
    return out_len;
}

static size_t compute_page_end_by_layout(size_t start_pos) {
    if (!reader_text_buf || start_pos >= reader_text_len) {
        return reader_text_len;
    }

    int view_w = reader_view ? reader_view->allocation.width : 0;
    int view_h = reader_view ? reader_view->allocation.height : 0;
    if (view_w <= 0) view_w = gdk_screen_width();
    if (view_h <= 0) view_h = gdk_screen_height() - 56;
    if (view_w <= 0) view_w = 600;
    if (view_h <= 0) view_h = 740;

    int usable_w = view_w - (READER_MARGIN_X * 2) - 8;
    int usable_h = view_h - (READER_MARGIN_Y * 2) - 8;
    if (usable_w < 120) usable_w = 120;
    if (usable_h < 120) usable_h = 120;

    PangoLayout *layout = gtk_widget_create_pango_layout(reader_view, "");
    if (!layout) {
        size_t end = start_pos + 550;
        if (end > reader_text_len) end = reader_text_len;
        return end;
    }

    PangoFontDescription *fdesc = pango_font_description_from_string(READER_FONT_STR);
    if (fdesc) {
        pango_layout_set_font_description(layout, fdesc);
        pango_font_description_free(fdesc);
    }
    pango_layout_set_width(layout, usable_w * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_spacing(layout, (READER_ABOVE_LINES + READER_BELOW_LINES) * PANGO_SCALE);

    /* Mede ate o proximo PAGE_BREAK_CHAR ou ate um bloco razoavel */
    size_t max_scan = reader_text_len - start_pos;
    if (max_scan > 4096) max_scan = 4096;

    size_t section_limit = max_scan;
    for (size_t k = 0; k < max_scan; k++) {
        if (reader_text_buf[start_pos + k] == PAGE_BREAK_CHAR) {
            section_limit = k;
            break;
        }
    }

    if (section_limit == 0) {
        g_object_unref(layout);
        return start_pos;
    }

    /* Cresce por fronteiras de palavra/quebra ate estourar a altura util da tela */
    size_t best_end = 0;
    size_t cursor = 0;

    while (cursor < section_limit) {
        size_t next_break = cursor + 1;
        while (next_break < section_limit) {
            char c = reader_text_buf[start_pos + next_break];
            if (c == ' ' || c == '\n') {
                break;
            }
            next_break++;
        }

        char *tmp = (char *)malloc(next_break + 1);
        if (!tmp) break;
        memcpy(tmp, reader_text_buf + start_pos, next_break);
        tmp[next_break] = '\0';

        if (!g_utf8_validate(tmp, -1, NULL)) {
            free(tmp);
            cursor = next_break;
            continue;
        }

        pango_layout_set_text(layout, tmp, -1);
        free(tmp);

        int px_w = 0, px_h = 0;
        pango_layout_get_pixel_size(layout, &px_w, &px_h);

        if (px_h > usable_h && best_end > 0) {
            break;
        }

        best_end = next_break;
        if (px_h > usable_h) {
            break;
        }
        cursor = next_break;
    }

    g_object_unref(layout);

    if (best_end == 0) {
        best_end = section_limit;
    }

    return start_pos + best_end;
}

static void load_book_into_reader(int idx) {
    free_reader_buffer_data();
    if (idx < 0 || idx >= book_count) return;

    current_book_idx = idx;
    BookItem *item = &books[idx];

    reader_text_buf = (char *)malloc(MAX_EXTRACTED_TEXT + 1);
    if (!reader_text_buf) return;
    reader_text_buf[0] = '\0';

    if (item->kind == BOOK_KIND_TEXT) {
        FILE *f = fopen(item->path, "rb");
        if (f) {
            size_t n = fread(reader_text_buf, 1, MAX_EXTRACTED_TEXT, f);
            fclose(f);
            reader_text_buf[n] = '\0';
            reader_text_len = n;
            compact_readable_text_inplace(reader_text_buf, &reader_text_len);
            if (reader_text_len > 0) {
                book_has_readable_text = 1;
            }
        }
        if (!book_has_readable_text) {
            reader_text_len = (size_t)snprintf(reader_text_buf, MAX_EXTRACTED_TEXT,
                "Arquivo vazio ou ilegivel.");
        }
    } else if (item->kind == BOOK_KIND_PDF) {
        char esc_path[MAX_PATH_LEN * 2 + 32];
        if (escape_shell_single_quotes(item->path, esc_path, sizeof(esc_path))) {
            char cmd[MAX_PATH_LEN * 2 + 128];
            snprintf(cmd, sizeof(cmd), "pdftotext %s - 2>/dev/null", esc_path);
            FILE *pipe = popen(cmd, "r");
            if (pipe) {
                size_t n = fread(reader_text_buf, 1, MAX_EXTRACTED_TEXT, pipe);
                int status = pclose(pipe);
                if (status == 0 && n >= 25) {
                    reader_text_buf[n] = '\0';
                    reader_text_len = n;
                    compact_readable_text_inplace(reader_text_buf, &reader_text_len);
                    if (reader_text_len >= 25) book_has_readable_text = 1;
                }
            }
        }
        if (!book_has_readable_text) {
            reader_text_len = extract_pdf_raw_strings(item->path, reader_text_buf, MAX_EXTRACTED_TEXT);
            if (reader_text_len >= 25) book_has_readable_text = 1;
        }
        if (!book_has_readable_text) {
            reader_text_len = (size_t)snprintf(reader_text_buf, MAX_EXTRACTED_TEXT,
                "PDF sem camada de texto extraivel neste leitor.\n\nUse .epub (sem DRM) ou .txt.");
        }
    } else {
        if (ends_with_ci(item->path, ".epub")) {
            reader_text_len = extract_epub_text_via_unzip(item->path, reader_text_buf, MAX_EXTRACTED_TEXT);
            if (reader_text_len >= 30) {
                book_has_readable_text = 1;
            }
        } else if (ends_with_ci(item->path, ".mobi") || ends_with_ci(item->path, ".azw")) {
            reader_text_len = extract_mobi_text(item->path, reader_text_buf, MAX_EXTRACTED_TEXT);
            if (reader_text_len >= 30) {
                book_has_readable_text = 1;
            } else {
                reader_text_len = (size_t)snprintf(reader_text_buf, MAX_EXTRACTED_TEXT,
                    "MOBI com Huffman/KF8 ou DRM nao suportado diretamente.\n\nConverta para .epub (sem DRM) ou .txt.");
            }
        }
        if (!book_has_readable_text && reader_text_len == 0) {
            reader_text_len = (size_t)snprintf(reader_text_buf, MAX_EXTRACTED_TEXT,
                "Formato protegido ou comprimido nao suportado diretamente.\n\nUse .epub (sem DRM) ou .txt.");
        }
    }

    total_pages_est = 1;
    current_page_num = 1;
    max_page_reached = 1;
    reader_page_pos = 0;
    reader_next_pos = 0;
}

static void force_eink_redraw(GtkWidget *widget) {
    if (!widget || !main_window) return;
    gtk_widget_queue_draw(main_window);
    if (main_window->window) {
        gdk_window_invalidate_rect(main_window->window, NULL, TRUE);
        gdk_window_process_updates(main_window->window, TRUE);
    }
}

static void render_current_reader_page(void) {
    if (!reader_buffer || !reader_text_buf) return;

    while (reader_page_pos < reader_text_len &&
           (reader_text_buf[reader_page_pos] == PAGE_BREAK_CHAR ||
            reader_text_buf[reader_page_pos] == '\n' ||
            reader_text_buf[reader_page_pos] == ' ')) {
        reader_page_pos++;
    }

    if (reader_page_pos >= reader_text_len) {
        reader_page_pos = 0;
    }

    size_t page_end = compute_page_end_by_layout(reader_page_pos);
    if (page_end < reader_page_pos) page_end = reader_page_pos;
    size_t chunk = page_end - reader_page_pos;

    reader_next_pos = page_end;
    while (reader_next_pos < reader_text_len &&
           (reader_text_buf[reader_next_pos] == PAGE_BREAK_CHAR ||
            reader_text_buf[reader_next_pos] == ' ' ||
            reader_text_buf[reader_next_pos] == '\n')) {
        reader_next_pos++;
    }

    char *page_buf = (char *)malloc(chunk + 32);
    if (!page_buf) return;
    if (chunk > 0) {
        memcpy(page_buf, reader_text_buf + reader_page_pos, chunk);
    }
    page_buf[chunk] = '\0';

    if (!g_utf8_validate(page_buf, -1, NULL)) {
        const char *end_valid = NULL;
        g_utf8_validate(page_buf, -1, &end_valid);
        size_t valid_len = end_valid ? (size_t)(end_valid - page_buf) : 0;
        char *valid = (char *)malloc(valid_len + 1);
        if (valid) {
            if (valid_len > 0) {
                memcpy(valid, page_buf, valid_len);
            }
            valid[valid_len] = '\0';
            gtk_text_buffer_set_text(reader_buffer, valid_len > 0 ? valid : "", -1);
            free(valid);
        }
    } else {
        gtk_text_buffer_set_text(reader_buffer, chunk > 0 ? page_buf : "", -1);
    }
    free(page_buf);

    force_eink_redraw(reader_view);
}

static void sync_widgets_for_mode(void) {
    if (ui_mode == UI_MODE_PET) {
        gtk_window_unfullscreen(GTK_WINDOW(main_window));
        if (top_hbox) gtk_widget_show(top_hbox);
        gtk_widget_show(lbl_status);
        gtk_widget_show(img_bulba);
        gtk_widget_hide(book_list_box);
        gtk_widget_hide(reader_frame);
        if (btn_hbox) gtk_widget_show(btn_hbox);

        gtk_widget_set_sensitive(btn_slot1, TRUE);
        gtk_widget_set_sensitive(btn_slot2, TRUE);
        gtk_widget_set_sensitive(btn_slot3, TRUE);

        gtk_button_set_label(GTK_BUTTON(btn_top_action), "Sair");
        gtk_button_set_label(GTK_BUTTON(btn_slot1), "Alimentar");
        gtk_button_set_label(GTK_BUTTON(btn_slot2), "Brincar");
        gtk_button_set_label(GTK_BUTTON(btn_slot3), "Dormir/Acordar");
    } else if (ui_mode == UI_MODE_BOOK_LIST) {
        gtk_window_unfullscreen(GTK_WINDOW(main_window));
        if (top_hbox) gtk_widget_show(top_hbox);
        gtk_widget_show(lbl_status);
        gtk_widget_hide(img_bulba);
        gtk_widget_hide(reader_frame);
        gtk_widget_show(book_list_box);
        if (btn_hbox) gtk_widget_show(btn_hbox);

        int total_pages = get_total_book_pages();
        gtk_widget_set_sensitive(btn_slot1, book_list_page > 0);
        gtk_widget_set_sensitive(btn_slot2, book_list_page + 1 < total_pages);
        gtk_widget_set_sensitive(btn_slot3, TRUE);

        gtk_button_set_label(GTK_BUTTON(btn_top_action), "Voltar");
        gtk_button_set_label(GTK_BUTTON(btn_slot1), "Pag. Anterior");
        gtk_button_set_label(GTK_BUTTON(btn_slot2), "Prox. Pagina");
        gtk_button_set_label(GTK_BUTTON(btn_slot3), "Menu");

        refresh_book_list_page_ui();
    } else if (ui_mode == UI_MODE_READER) {
        gtk_window_unfullscreen(GTK_WINDOW(main_window));
        if (top_hbox) gtk_widget_show(top_hbox);
        gtk_widget_hide(lbl_status);
        gtk_widget_hide(img_bulba);
        gtk_widget_hide(book_list_box);
        if (btn_hbox) gtk_widget_hide(btn_hbox);

        gtk_button_set_label(GTK_BUTTON(btn_top_action), "Voltar");
        gtk_widget_show(reader_frame);
        gtk_widget_show(reader_view);
    }

    force_eink_redraw(main_window);
}

static void update_ui(void) {
    refresh_stats_label();

    if (ui_mode == UI_MODE_PET) {
        if (pet.is_sleeping) {
            set_status_text("Bulba esta tirando uma soneca... Zzz");
            apply_sprite_mode(1);
        } else if (pet.hunger > 70) {
            set_status_text("Bulba parece estar com muita fome");
            apply_sprite_mode(0);
        } else if (pet.happiness < 45) {
            set_status_text("Bulba está zangada. Abra Livros e vire paginas para anima-la.");
            apply_sprite_mode(0);
        } else {
            set_status_text("Bulba esta observando o ambiente.");
            apply_sprite_mode(0);
        }
    } else if (ui_mode == UI_MODE_BOOK_LIST) {
        int total_pages = get_total_book_pages();
        char msg[256];
        if (book_count == 0) {
            snprintf(msg, sizeof(msg), "Nenhum livro encontrado na raiz, docs ou downloads.");
        } else {
            snprintf(msg, sizeof(msg), "Estante (Pag %d/%d) | Humor: %d%%",
                     book_list_page + 1, total_pages, pet.happiness);
        }
        set_status_text(msg);
    }
}

static void open_book_reader(int idx) {
    if (idx < 0 || idx >= book_count) return;
    load_book_into_reader(idx);
    ui_mode = UI_MODE_READER;
    sync_widgets_for_mode();
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    render_current_reader_page();
    update_ui();
}

static void open_book_selection(void) {
    refresh_books_list();
    ui_mode = UI_MODE_BOOK_LIST;
    sync_widgets_for_mode();
    update_ui();
}

static void return_to_pet_mode(void) {
    free_reader_buffer_data();
    current_book_idx = -1;
    ui_mode = UI_MODE_PET;
    sync_widgets_for_mode();
    update_ui();
}

static void flip_next_page_and_reward(void) {
    if (!book_has_readable_text) return;

    if (reader_next_pos < reader_text_len) {
        reader_page_pos = reader_next_pos;
        current_page_num++;
    } else {
        reader_page_pos = 0;
        current_page_num = 1;
    }

    if (current_page_num > max_page_reached) {
        max_page_reached = current_page_num;
        int gain = (pet.happiness < 35) ? 4 : 3;
        pet.happiness = clamp_int(pet.happiness + gain, 0, 100);
        pet.energy = clamp_int(pet.energy - 2, 0, 100);
        pet.hunger = clamp_int(pet.hunger + 1, 0, 100);
    }

    render_current_reader_page();
    refresh_stats_label();
    save_game();
}

static void flip_prev_page(void) {
    if (!book_has_readable_text || current_page_num <= 1) return;

    /* Recua ate ~600 bytes atras e recalcula pela quebra real de pagina */
    size_t search_start = (reader_page_pos > 700) ? (reader_page_pos - 700) : 0;
    while (search_start > 0 && reader_text_buf[search_start - 1] != PAGE_BREAK_CHAR) {
        search_start--;
    }

    size_t pos = search_start;
    size_t prev_pos = search_start;
    while (pos < reader_page_pos) {
        prev_pos = pos;
        size_t next_end = compute_page_end_by_layout(pos);
        if (next_end <= pos) break;
        pos = next_end;
        while (pos < reader_text_len &&
               (reader_text_buf[pos] == PAGE_BREAK_CHAR ||
                reader_text_buf[pos] == ' ' ||
                reader_text_buf[pos] == '\n')) {
            pos++;
        }
    }

    reader_page_pos = prev_pos;
    current_page_num--;
    render_current_reader_page();
}

static gboolean on_reader_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)user_data;
    if (ui_mode != UI_MODE_READER) return FALSE;
    if (!event || event->type != GDK_BUTTON_PRESS) return FALSE;

    int w = widget->allocation.width;
    if (w <= 0) {
        w = gdk_screen_width();
    }

    if (event->x < (double)w * 0.35) {
        flip_prev_page();
    } else {
        flip_next_page_and_reward();
    }
    return TRUE;
}

static gboolean on_main_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget; (void)user_data;
    if (!event) return FALSE;

    if (ui_mode == UI_MODE_READER) {
        if (event->keyval == GDK_KEY_Escape || event->keyval == GDK_KEY_BackSpace) {
            open_book_selection();
            return TRUE;
        }
        if (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Page_Up) {
            flip_prev_page();
            return TRUE;
        }
        if (event->keyval == GDK_KEY_Right || event->keyval == GDK_KEY_Page_Down || event->keyval == GDK_KEY_space) {
            flip_next_page_and_reward();
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean game_tick(gpointer data) {
    (void)data;
    if (already_quitting) return FALSE;

    step_simulation_tick();
    update_ui();

    ticks_since_save++;
    if (ticks_since_save >= 4) {
        save_game();
    }
    return TRUE;
}

static void on_book_slot_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    int slot = GPOINTER_TO_INT(user_data);
    int idx = book_list_page * BOOKS_PER_PAGE + slot;
    if (idx >= 0 && idx < book_count) {
        open_book_reader(idx);
    }
}

static void on_slot1_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;

    if (ui_mode == UI_MODE_PET) {
        if (pet.is_sleeping) {
            set_status_text("Shhh... ele esta dormindo!");
            return;
        }
        pet.hunger = clamp_int(pet.hunger - 20, 0, 100);
        pet.happiness = clamp_int(pet.happiness + 3, 0, 100);
        apply_sprite_mode(2);
        set_status_text("Nham! Bulba adorou a Poke Puff!");
        refresh_stats_label();
        save_game();
    } else if (ui_mode == UI_MODE_BOOK_LIST) {
        if (book_list_page > 0) {
            book_list_page--;
            refresh_book_list_page_ui();
            gtk_widget_set_sensitive(btn_slot1, book_list_page > 0);
            gtk_widget_set_sensitive(btn_slot2, book_list_page + 1 < get_total_book_pages());
            update_ui();
        }
    }
}

static void on_slot2_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;

    if (ui_mode == UI_MODE_PET) {
        if (pet.is_sleeping) {
            set_status_text("Acorde o Bulba primeiro para brincar.");
            return;
        }
        open_book_selection();
    } else if (ui_mode == UI_MODE_BOOK_LIST) {
        if (book_list_page + 1 < get_total_book_pages()) {
            book_list_page++;
            refresh_book_list_page_ui();
            gtk_widget_set_sensitive(btn_slot1, book_list_page > 0);
            gtk_widget_set_sensitive(btn_slot2, book_list_page + 1 < get_total_book_pages());
            update_ui();
        }
    }
}

static void on_slot3_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;

    if (ui_mode == UI_MODE_PET) {
        pet.is_sleeping = !pet.is_sleeping;
        update_ui();
        save_game();
    } else if (ui_mode == UI_MODE_BOOK_LIST) {
        return_to_pet_mode();
    }
}

static void on_top_action_clicked(GtkButton *button, gpointer user_data) {
    (void)button; (void)user_data;

    if (ui_mode == UI_MODE_READER) {
        open_book_selection();
        return;
    }
    if (ui_mode == UI_MODE_BOOK_LIST) {
        return_to_pet_mode();
        return;
    }

    if (already_quitting) return;
    already_quitting = TRUE;
    save_game();
    if (gtk_main_level() > 0) {
        gtk_main_quit();
    }
}

static void style_kindle_reader_view(GtkWidget *tv) {
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), READER_MARGIN_X);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), READER_MARGIN_X);
    gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(tv), READER_ABOVE_LINES);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(tv), READER_BELOW_LINES);

    PangoFontDescription *font_desc = pango_font_description_from_string(READER_FONT_STR);
    if (font_desc) {
        gtk_widget_modify_font(tv, font_desc);
        pango_font_description_free(font_desc);
    }

    GdkColor bg_paper;
    GdkColor fg_ink;
    gdk_color_parse("#FFFFFF", &bg_paper);
    gdk_color_parse("#111111", &fg_ink);
    gtk_widget_modify_base(tv, GTK_STATE_NORMAL, &bg_paper);
    gtk_widget_modify_text(tv, GTK_STATE_NORMAL, &fg_ink);

    gtk_widget_add_events(tv, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(tv, "button-press-event", G_CALLBACK(on_reader_button_press), NULL);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    load_game();
    load_sprites();

    int sw = gdk_screen_width();
    int sh = gdk_screen_height();
    if (sw <= 0 || sw > 4000) sw = 600;
    if (sh <= 0 || sh > 4000) sh = 800;

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "L:A_N:application_ID:bulbagotchi_PC:T_O:U");
    gtk_window_set_default_size(GTK_WINDOW(main_window), sw, sh);
    gtk_window_set_keep_above(GTK_WINDOW(main_window), TRUE);
    g_signal_connect(main_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(main_window, "key-press-event", G_CALLBACK(on_main_key_press), NULL);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    top_hbox = gtk_hbox_new(FALSE, 6);
    lbl_stats = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(lbl_stats), PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment(GTK_MISC(lbl_stats), 0.0f, 0.5f);

    btn_top_action = gtk_button_new_with_label("Sair");
    g_signal_connect(btn_top_action, "clicked", G_CALLBACK(on_top_action_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(top_hbox), lbl_stats, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(top_hbox), btn_top_action, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), top_hbox, FALSE, FALSE, 2);

    lbl_status = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(lbl_status), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(lbl_status), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify(GTK_LABEL(lbl_status), GTK_JUSTIFY_CENTER);
    gtk_misc_set_alignment(GTK_MISC(lbl_status), 0.5f, 0.5f);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_status, FALSE, FALSE, 2);

    img_bulba = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(vbox), img_bulba, TRUE, TRUE, 4);

    book_list_box = gtk_vbox_new(TRUE, 8);
    for (int i = 0; i < BOOKS_PER_PAGE; i++) {
        btn_book_slots[i] = gtk_button_new_with_label("");
        gtk_widget_set_size_request(btn_book_slots[i], -1, 64);
        g_signal_connect(btn_book_slots[i], "clicked",
                         G_CALLBACK(on_book_slot_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(book_list_box), btn_book_slots[i], TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(vbox), book_list_box, TRUE, TRUE, 4);

    reader_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(reader_frame), GTK_SHADOW_NONE);
    reader_view = gtk_text_view_new();
    style_kindle_reader_view(reader_view);
    reader_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(reader_view));
    gtk_container_add(GTK_CONTAINER(reader_frame), reader_view);
    gtk_box_pack_start(GTK_BOX(vbox), reader_frame, TRUE, TRUE, 0);

    btn_hbox = gtk_hbox_new(TRUE, 6);
    btn_slot1 = gtk_button_new_with_label("Alimentar");
    btn_slot2 = gtk_button_new_with_label("Brincar");
    btn_slot3 = gtk_button_new_with_label("Dormir/Acordar");

    gtk_widget_set_size_request(btn_slot1, -1, 56);
    gtk_widget_set_size_request(btn_slot2, -1, 56);
    gtk_widget_set_size_request(btn_slot3, -1, 56);
    gtk_widget_set_size_request(btn_top_action, -1, 36);

    g_signal_connect(btn_slot1, "clicked", G_CALLBACK(on_slot1_clicked), NULL);
    g_signal_connect(btn_slot2, "clicked", G_CALLBACK(on_slot2_clicked), NULL);
    g_signal_connect(btn_slot3, "clicked", G_CALLBACK(on_slot3_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_slot1, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_slot2, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_slot3, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), btn_hbox, FALSE, FALSE, 2);

    gtk_widget_show_all(main_window);
    sync_widgets_for_mode();
    update_ui();
    g_timeout_add(TICK_INTERVAL_MS, game_tick, NULL);

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    gtk_window_present(GTK_WINDOW(main_window));

    gtk_main();

    save_game();
    free_reader_buffer_data();

    if (pix_idle) g_object_unref(pix_idle);
    if (pix_sleep) g_object_unref(pix_sleep);
    if (pix_eat) g_object_unref(pix_eat);

    return 0;
}