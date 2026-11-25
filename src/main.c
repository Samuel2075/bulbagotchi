#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SAVE_PATH "/mnt/us/extensions/bulbagotchi/save.dat"
#define ASSETS_DIR "/mnt/us/extensions/bulbagotchi/assets"

typedef struct {
    int hunger;      // 0 = satisfeito, 100 = faminto
    int happiness;   // 0 = triste, 100 = muito feliz
    int energy;      // 0 = exausto, 100 = descansado
    int is_sleeping; // 0 = acordado, 1 = dormindo
    time_t last_seen;
} BulbaState;

BulbaState pet = {20, 80, 90, 0, 0};

GtkWidget *lbl_status;
GtkWidget *lbl_stats;
GtkWidget *img_bulba;

static int clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

void save_game(void) {
    pet.last_seen = time(NULL);
    FILE *f = fopen(SAVE_PATH, "wb");
    if (f) {
        fwrite(&pet, sizeof(BulbaState), 1, f);
        fclose(f);
    }
}

void apply_offline_progress(void) {
    if (pet.last_seen <= 0) return;
    time_t now = time(NULL);
    double elapsed = difftime(now, pet.last_seen);
    if (elapsed <= 0) return;

    // A cada 30 segundos offline equivale a ~6 ticks de 5s
    int ticks = (int)(elapsed / 5.0);
    if (ticks > 10000) ticks = 10000;

    for (int i = 0; i < ticks; i++) {
        if (pet.is_sleeping) {
            pet.energy = clamp_int(pet.energy + 5, 0, 100);
            pet.hunger = clamp_int(pet.hunger + 1, 0, 100);
            if (pet.energy == 100) pet.is_sleeping = 0;
        } else {
            pet.hunger = clamp_int(pet.hunger + 2, 0, 100);
            pet.energy = clamp_int(pet.energy - 1, 0, 100);
            if (pet.hunger > 80) {
                pet.happiness = clamp_int(pet.happiness - 2, 0, 100);
            }
        }
    }
}

void load_game(void) {
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

void set_sprite(const char *filename) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", ASSETS_DIR, filename);
    gtk_image_set_from_file(GTK_IMAGE(img_bulba), path);
}

void update_ui(void) {
    char stats_buf[192];
    snprintf(stats_buf, sizeof(stats_buf),
             "<span font='18' weight='bold'>Fome: %d%%   |   Humor: %d%%   |   Energia: %d%%</span>",
             pet.hunger, pet.happiness, pet.energy);
    gtk_label_set_markup(GTK_LABEL(lbl_stats), stats_buf);

    if (pet.is_sleeping) {
        gtk_label_set_markup(GTK_LABEL(lbl_status), "<span font='16'>Bulbasaur esta tirando uma soneca... Zzz</span>");
        set_sprite("bulba_sleep.png");
    } else if (pet.hunger > 75) {
        gtk_label_set_markup(GTK_LABEL(lbl_status), "<span font='16'>Bulba! (Ele parece estar com muita fome)</span>");
        set_sprite("bulba_idle.png");
    } else if (pet.happiness < 30) {
        gtk_label_set_markup(GTK_LABEL(lbl_status), "<span font='16'>Bulbasaur quer brincar um pouco...</span>");
        set_sprite("bulba_idle.png");
    } else {
        gtk_label_set_markup(GTK_LABEL(lbl_status), "<span font='16'>Bulbasaur esta observando o ambiente.</span>");
        set_sprite("bulba_idle.png");
    }
}

gboolean game_tick(gpointer data) {
    (void)data;
    if (pet.is_sleeping) {
        pet.energy = clamp_int(pet.energy + 5, 0, 100);
        pet.hunger = clamp_int(pet.hunger + 1, 0, 100);
        if (pet.energy == 100) pet.is_sleeping = 0;
    } else {
        pet.hunger = clamp_int(pet.hunger + 2, 0, 100);
        pet.energy = clamp_int(pet.energy - 1, 0, 100);
        if (pet.hunger > 80) {
            pet.happiness = clamp_int(pet.happiness - 2, 0, 100);
        }
    }
    save_game();
    update_ui();
    return TRUE;
}

void on_feed_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    if (pet.is_sleeping) {
        gtk_label_set_markup(GTK_LABEL(lbl_status), "<span font='16'>Shhh... ele esta dormindo!</span>");
        return;
    }
    pet.hunger = clamp_int(pet.hunger - 25, 0, 100);
    pet.happiness = clamp_int(pet.happiness + 5, 0, 100);
    set_sprite("bulba_eat.png");
    gtk_label_set_markup(GTK_LABEL(lbl_status), "<span font='16'>Nham! Bulbasaur adorou a Poke Puff!</span>");
    save_game();
}

void on_play_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    if (pet.is_sleeping) {
        gtk_label_set_markup(GTK_LABEL(lbl_status), "<span font='16'>Acorde o Bulbasaur primeiro para brincar.</span>");
        return;
    }
    if (pet.energy < 15) {
        gtk_label_set_markup(GTK_LABEL(lbl_status), "<span font='16'>Sem energia suficiente para brincar!</span>");
        return;
    }
    pet.happiness = clamp_int(pet.happiness + 20, 0, 100);
    pet.energy = clamp_int(pet.energy - 15, 0, 100);
    pet.hunger = clamp_int(pet.hunger + 10, 0, 100);
    save_game();
    update_ui();
}

void on_sleep_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    pet.is_sleeping = !pet.is_sleeping;
    save_game();
    update_ui();
}

void on_quit_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    save_game();
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    load_game();

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "L:A_N:application_ID:bulbagotchi_PC:N");
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GdkColor white;
    gdk_color_parse("#FFFFFF", &white);
    gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &white);

    GtkWidget *vbox = gtk_vbox_new(FALSE, 20);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 30);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *top_hbox = gtk_hbox_new(FALSE, 10);
    lbl_stats = gtk_label_new("");
    GtkWidget *btn_quit = gtk_button_new_with_label(" [ X ] Sair ");
    g_signal_connect(btn_quit, "clicked", G_CALLBACK(on_quit_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(top_hbox), lbl_stats, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(top_hbox), btn_quit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), top_hbox, FALSE, FALSE, 10);

    img_bulba = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(vbox), img_bulba, TRUE, TRUE, 10);

    lbl_status = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(vbox), lbl_status, FALSE, FALSE, 10);

    GtkWidget *btn_hbox = gtk_hbox_new(TRUE, 20);
    GtkWidget *btn_feed = gtk_button_new_with_label("Alimentar");
    GtkWidget *btn_play = gtk_button_new_with_label("Brincar");
    GtkWidget *btn_sleep = gtk_button_new_with_label("Dormir / Acordar");

    gtk_widget_set_size_request(btn_feed, -1, 80);
    gtk_widget_set_size_request(btn_play, -1, 80);
    gtk_widget_set_size_request(btn_sleep, -1, 80);

    g_signal_connect(btn_feed, "clicked", G_CALLBACK(on_feed_clicked), NULL);
    g_signal_connect(btn_play, "clicked", G_CALLBACK(on_play_clicked), NULL);
    g_signal_connect(btn_sleep, "clicked", G_CALLBACK(on_sleep_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_feed, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_play, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_hbox), btn_sleep, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), btn_hbox, FALSE, FALSE, 10);

    update_ui();
    g_timeout_add(5000, game_tick, NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
