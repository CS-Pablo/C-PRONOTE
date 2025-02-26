/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   C-Pronote.c                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: csauron <csauron@students.42.fr>           +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/02/26 00:57:25 by csauron           #+#    #+#             */
/*   Updated: 2025/02/26 01:40:08 by csauron          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <gtk/gtk.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define DB_NAME "eleves.db"
#define CSV_FILENAME "eleves.csv"
#define MAX_NOM 50
#define MAX_EMAIL 100
#define MAX_TELEPHONE 20
#define MAX_GRADE 10
#define MAX_QUERY 1024
typedef struct {
    int id;
    char nom[MAX_NOM];
    int age;
    float taille;
    char email[MAX_EMAIL];
    char telephone[MAX_TELEPHONE];
    char grade[MAX_GRADE];
} Personne;

sqlite3 *db = NULL;

void log_error(const char *msg) {
    FILE *fp = fopen("log.txt", "a");
    if(fp) {
        time_t now = time(NULL);
        char time_str[26];
        ctime_r(&now, time_str);
        time_str[strcspn(time_str, "\n")] = '\0';
        fprintf(fp, "[%s] %s\n", time_str, msg);
        fclose(fp);
    }
}

bool initDB(sqlite3 **db) {
    char *errMsg = NULL;
    int rc = sqlite3_open(DB_NAME, db);
    if (rc != SQLITE_OK) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Erreur d'ouverture de la BD: %s", sqlite3_errmsg(*db));
        log_error(buffer);
        return false;
    }
    const char *sql =
        "CREATE TABLE IF NOT EXISTS eleves ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "nom TEXT NOT NULL, "
            "age INTEGER, "
            "taille REAL, "
            "email TEXT, "
            "telephone TEXT, "
            "grade TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS presences ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "eleve_id INTEGER, "
            "date TEXT, "
            "status TEXT, "         // 'present', 'absent', 'retard'
            "FOREIGN KEY(eleve_id) REFERENCES eleves(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS notes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "eleve_id INTEGER, "
            "matiere TEXT, "
            "note REAL, "
            "commentaire TEXT, "
            "date TEXT, "
            "FOREIGN KEY(eleve_id) REFERENCES eleves(id)"
        ");"
        "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "username TEXT UNIQUE, "
            "password_hash TEXT, "
            "role TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS logs ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "user_id INTEGER, "
            "action TEXT, "
            "timestamp TEXT, "
            "FOREIGN KEY(user_id) REFERENCES users(id)"
        ");";
    rc = sqlite3_exec(*db, sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log_error(errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool ajouterEleve(sqlite3 *db, const Personne *e) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO eleves (nom, age, taille, email, telephone, grade) VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Erreur de préparation: %s", sqlite3_errmsg(db));
        log_error(buffer);
        return false;
    }
    sqlite3_bind_text(stmt, 1, e->nom, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, e->age);
    sqlite3_bind_double(stmt, 3, e->taille);
    sqlite3_bind_text(stmt, 4, e->email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, e->telephone, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, e->grade, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Erreur d'insertion: %s", sqlite3_errmsg(db));
        log_error(buffer);
        return false;
    }
    return true;
}

char* listerElevesStr(sqlite3 *db) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT * FROM eleves;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error(sqlite3_errmsg(db));
        return NULL;
    }
    char *result = malloc(4096);
    if (!result) return NULL;
    strcpy(result, "+----+----------------------+-----+--------+---------------------------+----------------+--------+\n");
    strcat(result, "| ID | Nom                  | Age | Taille | Email                     | Téléphone      | Grade  |\n");
    strcat(result, "+----+----------------------+-----+--------+---------------------------+----------------+--------+\n");
    char line[256];
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(line, sizeof(line), "| %-2d | %-20s | %-3d | %-6.2f | %-25s | %-14s | %-6s |\n",
                 sqlite3_column_int(stmt, 0),
                 sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "",
                 sqlite3_column_int(stmt, 2),
                 sqlite3_column_double(stmt, 3),
                 sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "",
                 sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "",
                 sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "");
        strcat(result, line);
    }
    strcat(result, "+----+----------------------+-----+--------+---------------------------+----------------+--------+\n");
    sqlite3_finalize(stmt);
    return result;
}

bool modifierEleve(sqlite3 *db, int id, const Personne *e) {
    // Vérifie d'abord l'existence de l'élève
    sqlite3_stmt *check_stmt;
    const char *check_sql = "SELECT COUNT(*) FROM eleves WHERE id = ?;";
    if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, NULL) != SQLITE_OK) {
        log_error(sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_int(check_stmt, 1, id);
    if (sqlite3_step(check_stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(check_stmt, 0);
        sqlite3_finalize(check_stmt);
        if (count == 0) {
            return false;
        }
    } else {
        sqlite3_finalize(check_stmt);
        log_error(sqlite3_errmsg(db));
        return false;
    }
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE eleves SET nom=?, age=?, taille=?, email=?, telephone=?, grade=? WHERE id=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error(sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_text(stmt, 1, e->nom, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, e->age);
    sqlite3_bind_double(stmt, 3, e->taille);
    sqlite3_bind_text(stmt, 4, e->email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, e->telephone, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, e->grade, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        log_error(sqlite3_errmsg(db));
        return false;
    }
    return true;
}

bool supprimerEleve(sqlite3 *db, int id) {
    sqlite3_stmt *check_stmt;
    const char *check_sql = "SELECT COUNT(*) FROM eleves WHERE id = ?;";
    if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, NULL) != SQLITE_OK) {
        log_error(sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_int(check_stmt, 1, id);
    if (sqlite3_step(check_stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(check_stmt, 0);
        sqlite3_finalize(check_stmt);
        if (count == 0) {
            return false;
        }
    } else {
        sqlite3_finalize(check_stmt);
        log_error(sqlite3_errmsg(db));
        return false;
    }
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM eleves WHERE id=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error(sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        log_error(sqlite3_errmsg(db));
        return false;
    }
    return true;
}

bool exporterCSV(sqlite3 *db) {
    FILE *file = fopen(CSV_FILENAME, "w");
    if (!file) {
        log_error("Erreur: Impossible d'ouvrir le fichier CSV pour écriture.");
        return false;
    }
    fprintf(file, "ID,Nom,Age,Taille,Email,Telephone,Grade\n");
    sqlite3_stmt *stmt;
    const char *sql = "SELECT * FROM eleves;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error(sqlite3_errmsg(db));
        fclose(file);
        return false;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        fprintf(file, "%d,%s,%d,%.2f,%s,%s,%s\n",
                sqlite3_column_int(stmt, 0),
                sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "",
                sqlite3_column_int(stmt, 2),
                sqlite3_column_double(stmt, 3),
                sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "",
                sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "",
                sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "");
    }
    fclose(file);
    sqlite3_finalize(stmt);
    return true;
}

bool rechercherEleve(sqlite3 *db, const char *terme, char **resultStr) {
    sqlite3_stmt *stmt;
    char sql[MAX_QUERY];
    snprintf(sql, MAX_QUERY,
             "SELECT * FROM eleves WHERE nom LIKE '%%%s%%' OR email LIKE '%%%s%%' OR grade LIKE '%%%s%%';",
             terme, terme, terme);
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error(sqlite3_errmsg(db));
        return false;
    }
    char *result = malloc(4096);
    if (!result) return false;
    strcpy(result, "+----+----------------------+-----+--------+---------------------------+----------------+--------+\n");
    strcat(result, "| ID | Nom                  | Age | Taille | Email                     | Téléphone      | Grade  |\n");
    strcat(result, "+----+----------------------+-----+--------+---------------------------+----------------+--------+\n");
    char line[256];
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        snprintf(line, sizeof(line), "| %-2d | %-20s | %-3d | %-6.2f | %-25s | %-14s | %-6s |\n",
                 sqlite3_column_int(stmt, 0),
                 sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "",
                 sqlite3_column_int(stmt, 2),
                 sqlite3_column_double(stmt, 3),
                 sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "",
                 sqlite3_column_text(stmt, 5) ? (const char*)sqlite3_column_text(stmt, 5) : "",
                 sqlite3_column_text(stmt, 6) ? (const char*)sqlite3_column_text(stmt, 6) : "");
        strcat(result, line);
    }
    strcat(result, "+----+----------------------+-----+--------+---------------------------+----------------+--------+\n");
    sqlite3_finalize(stmt);
    if (!found) {
        free(result);
        *resultStr = NULL;
    } else {
        *resultStr = result;
    }
    return true;
}

void on_add_student_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Ajouter un Élève",
                                                    GTK_WINDOW(user_data),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Ajouter", GTK_RESPONSE_OK,
                                                    "_Annuler", GTK_RESPONSE_CANCEL,
                                                    NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(content_area), grid);
    
    //étiquettes et zones de saisie
    GtkWidget *label_nom = gtk_label_new("Nom:");
    GtkWidget *entry_nom = gtk_entry_new();
    GtkWidget *label_age = gtk_label_new("Âge:");
    GtkWidget *entry_age = gtk_entry_new();
    GtkWidget *label_taille = gtk_label_new("Taille (m):");
    GtkWidget *entry_taille = gtk_entry_new();
    GtkWidget *label_email = gtk_label_new("Email:");
    GtkWidget *entry_email = gtk_entry_new();
    GtkWidget *label_telephone = gtk_label_new("Téléphone:");
    GtkWidget *entry_telephone = gtk_entry_new();
    GtkWidget *label_grade = gtk_label_new("Grade:");
    GtkWidget *entry_grade = gtk_entry_new();
    
    // placement dans la grille
    gtk_grid_attach(GTK_GRID(grid), label_nom, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_nom, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_age, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_age, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_taille, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_taille, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_email, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_email, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_telephone, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_telephone, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_grade, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_grade, 1, 5, 1, 1);
    
    gtk_widget_show_all(dialog);
    int result = gtk_dialog_run(GTK_DIALOG(dialog));
    if(result == GTK_RESPONSE_OK) {
        Personne p;
        strncpy(p.nom, gtk_entry_get_text(GTK_ENTRY(entry_nom)), MAX_NOM);
        p.age = atoi(gtk_entry_get_text(GTK_ENTRY(entry_age)));
        p.taille = atof(gtk_entry_get_text(GTK_ENTRY(entry_taille)));
        strncpy(p.email, gtk_entry_get_text(GTK_ENTRY(entry_email)), MAX_EMAIL);
        strncpy(p.telephone, gtk_entry_get_text(GTK_ENTRY(entry_telephone)), MAX_TELEPHONE);
        strncpy(p.grade, gtk_entry_get_text(GTK_ENTRY(entry_grade)), MAX_GRADE);
        if (ajouterEleve(db, &p)) {
            GtkWidget *info = gtk_message_dialog_new(GTK_WINDOW(user_data),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_INFO,
                                                       GTK_BUTTONS_OK,
                                                       "Élève ajouté avec succès !");
            gtk_dialog_run(GTK_DIALOG(info));
            gtk_widget_destroy(info);
        } else {
            GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(user_data),
                                                        GTK_DIALOG_MODAL,
                                                        GTK_MESSAGE_ERROR,
                                                        GTK_BUTTONS_OK,
                                                        "Erreur lors de l'ajout de l'élève.");
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
        }
    }
    gtk_widget_destroy(dialog);
}

void on_list_students_clicked(GtkButton *button, gpointer user_data) {
    char *resultStr = listerElevesStr(db);
    if(resultStr) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(user_data),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   "%s", resultStr);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        free(resultStr);
    } else {
        GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(user_data),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "Erreur lors de la récupération des élèves.");
        gtk_dialog_run(GTK_DIALOG(error));
        gtk_widget_destroy(error);
    }
}


void on_delete_student_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Supprimer un Élève",
                                                    GTK_WINDOW(user_data),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Supprimer", GTK_RESPONSE_OK,
                                                    "_Annuler", GTK_RESPONSE_CANCEL,
                                                    NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Entrez l'ID de l'élève");
    gtk_container_add(GTK_CONTAINER(content_area), entry);
    gtk_widget_show_all(dialog);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if(response == GTK_RESPONSE_OK) {
        int id = atoi(gtk_entry_get_text(GTK_ENTRY(entry)));
        if (supprimerEleve(db, id)) {
            GtkWidget *info = gtk_message_dialog_new(GTK_WINDOW(user_data),
                                                       GTK_DIALOG_MODAL,
                                                       GTK_MESSAGE_INFO,
                                                       GTK_BUTTONS_OK,
                                                       "Élève supprimé avec succès !");
            gtk_dialog_run(GTK_DIALOG(info));
            gtk_widget_destroy(info);
        } else {
            GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(user_data),
                                                        GTK_DIALOG_MODAL,
                                                        GTK_MESSAGE_ERROR,
                                                        GTK_BUTTONS_OK,
                                                        "Erreur: Aucun élève trouvé avec cet ID.");
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
        }
    }
    gtk_widget_destroy(dialog);
}

void on_search_student_clicked(GtkButton *button, gpointer user_data) {
    // Boîte de dialogue pour saisir le terme de recherche
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Rechercher un Élève",
                                                    GTK_WINDOW(user_data),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Rechercher", GTK_RESPONSE_OK,
                                                    "_Annuler", GTK_RESPONSE_CANCEL,
                                                    NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Entrez un terme de recherche");
    gtk_container_add(GTK_CONTAINER(content_area), entry);
    gtk_widget_show_all(dialog);
    
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        const char *terme = gtk_entry_get_text(GTK_ENTRY(entry));
        
        // Création de la fenêtre qui affichera les résultats
        GtkWidget *result_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(result_window), "Résultats de recherche");
        gtk_window_set_default_size(GTK_WINDOW(result_window), 800, 400);
        
        // Création du modèle pour le TreeView
        enum { COL_ID, COL_NOM, COL_AGE, COL_TAILLE, COL_EMAIL, COL_TELEPHONE, COL_GRADE, NUM_COLS };
        GtkListStore *store = gtk_list_store_new(NUM_COLS,
                                                 G_TYPE_INT,
                                                 G_TYPE_STRING,
                                                 G_TYPE_INT,
                                                 G_TYPE_DOUBLE,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING,
                                                 G_TYPE_STRING);
        
        // Préparation de la requête pour récupérer les élèves correspondants
        char sql[MAX_QUERY];
        snprintf(sql, MAX_QUERY,
                 "SELECT * FROM eleves WHERE nom LIKE '%%%s%%' OR email LIKE '%%%s%%' OR grade LIKE '%%%s%%';",
                 terme, terme, terme);
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt, 0);
                const char *nom = sqlite3_column_text(stmt, 1) ? (const char *)sqlite3_column_text(stmt, 1) : "";
                int age = sqlite3_column_int(stmt, 2);
                double taille = sqlite3_column_double(stmt, 3);
                const char *email = sqlite3_column_text(stmt, 4) ? (const char *)sqlite3_column_text(stmt, 4) : "";
                const char *telephone = sqlite3_column_text(stmt, 5) ? (const char *)sqlite3_column_text(stmt, 5) : "";
                const char *grade = sqlite3_column_text(stmt, 6) ? (const char *)sqlite3_column_text(stmt, 6) : "";
                
                GtkTreeIter iter;
                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter,
                                   COL_ID, id,
                                   COL_NOM, nom,
                                   COL_AGE, age,
                                   COL_TAILLE, taille,
                                   COL_EMAIL, email,
                                   COL_TELEPHONE, telephone,
                                   COL_GRADE, grade,
                                   -1);
            }
            sqlite3_finalize(stmt);
        } else {
            log_error(sqlite3_errmsg(db));
        }
        
        // Création du TreeView et de ses colonnes
        GtkWidget *treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        g_object_unref(store); // Le modèle est maintenant référencé par le treeview
        
        // Fonction utilitaire pour ajouter une colonne
        void add_column(const gchar *title, int col_index) {
            GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title, renderer, "text", col_index, NULL);
            gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
        }
        add_column("ID", COL_ID);
        add_column("Nom", COL_NOM);
        add_column("Âge", COL_AGE);
        add_column("Taille", COL_TAILLE);
        add_column("Email", COL_EMAIL);
        add_column("Téléphone", COL_TELEPHONE);
        add_column("Grade", COL_GRADE);
        
        // Placement du TreeView dans une fenêtre avec un GtkScrolledWindow
        GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
        gtk_container_add(GTK_CONTAINER(scrolled_window), treeview);
        gtk_container_add(GTK_CONTAINER(result_window), scrolled_window);
        
        gtk_widget_show_all(result_window);
    }
    gtk_widget_destroy(dialog);
}


void on_export_csv_clicked(GtkButton *button, gpointer user_data) {
    if(exporterCSV(db)) {
        GtkWidget *info = gtk_message_dialog_new(GTK_WINDOW(user_data),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK,
                                                   "Exportation CSV réussie dans '%s'.", CSV_FILENAME);
        gtk_dialog_run(GTK_DIALOG(info));
        gtk_widget_destroy(info);
    } else {
        GtkWidget *error = gtk_message_dialog_new(GTK_WINDOW(user_data),
                                                   GTK_DIALOG_MODAL,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_OK,
                                                   "Erreur lors de l'exportation CSV.");
        gtk_dialog_run(GTK_DIALOG(error));
        gtk_widget_destroy(error);
    }
}


void on_quit_clicked(GtkButton *button, gpointer user_data) {
    gtk_main_quit();
}

GtkWidget* create_main_window() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Gestion des Élèves");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    
    GtkWidget *btn_add = gtk_button_new_with_label("Ajouter un Élève");
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_student_clicked), window);
    gtk_box_pack_start(GTK_BOX(vbox), btn_add, FALSE, FALSE, 0);
    
    GtkWidget *btn_list = gtk_button_new_with_label("Lister les Élèves");
    g_signal_connect(btn_list, "clicked", G_CALLBACK(on_list_students_clicked), window);
    gtk_box_pack_start(GTK_BOX(vbox), btn_list, FALSE, FALSE, 0);
    
    GtkWidget *btn_delete = gtk_button_new_with_label("Supprimer un Élève");
    g_signal_connect(btn_delete, "clicked", G_CALLBACK(on_delete_student_clicked), window);
    gtk_box_pack_start(GTK_BOX(vbox), btn_delete, FALSE, FALSE, 0);
    
    GtkWidget *btn_search = gtk_button_new_with_label("Rechercher un Élève");
    g_signal_connect(btn_search, "clicked", G_CALLBACK(on_search_student_clicked), window);
    gtk_box_pack_start(GTK_BOX(vbox), btn_search, FALSE, FALSE, 0);
    
    GtkWidget *btn_export = gtk_button_new_with_label("Exporter CSV");
    g_signal_connect(btn_export, "clicked", G_CALLBACK(on_export_csv_clicked), window);
    gtk_box_pack_start(GTK_BOX(vbox), btn_export, FALSE, FALSE, 0);
    
    GtkWidget *btn_quit = gtk_button_new_with_label("Quitter");
    g_signal_connect(btn_quit, "clicked", G_CALLBACK(on_quit_clicked), window);
    gtk_box_pack_start(GTK_BOX(vbox), btn_quit, FALSE, FALSE, 0);
    
    return window;
}

gboolean check_login(const char *username, const char *password) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT password_hash FROM users WHERE username = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_error("Échec de préparation de la requête de connexion");
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *stored_hash = sqlite3_column_text(stmt, 0);
        if (strcmp((const char*)stored_hash, password) == 0) {
            sqlite3_finalize(stmt);
            return TRUE;
        }
    }
    sqlite3_finalize(stmt);
    return FALSE;
}

void on_login_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *entry_username = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "entry_username"));
    GtkWidget *entry_password = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "entry_password"));
    const char *username = gtk_entry_get_text(GTK_ENTRY(entry_username));
    const char *password = gtk_entry_get_text(GTK_ENTRY(entry_password));
    
    if (check_login(username, password)) {
        GtkWidget *login_window = GTK_WIDGET(user_data);
        gtk_widget_destroy(login_window);
        
        GtkWidget *main_window = create_main_window();
        gtk_widget_show_all(main_window);
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "Nom d'utilisateur ou mot de passe incorrect.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}
/*fenêtre de connexion */
GtkWidget* create_login_window() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Connexion");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 200);

    
    GtkWidget *grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    
    GtkWidget *label_user = gtk_label_new("Nom d'utilisateur:");
    GtkWidget *entry_user = gtk_entry_new();
    GtkWidget *label_pass = gtk_label_new("Mot de passe:");
    GtkWidget *entry_pass = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry_pass), FALSE);
    
    GtkWidget *button_login = gtk_button_new_with_label("Se connecter");
    
    gtk_grid_attach(GTK_GRID(grid), label_user, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_user, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_pass, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_pass, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), button_login, 0, 2, 2, 1);
    
    g_object_set_data(G_OBJECT(button_login), "entry_username", entry_user);
    g_object_set_data(G_OBJECT(button_login), "entry_password", entry_pass);
    g_signal_connect(button_login, "clicked", G_CALLBACK(on_login_clicked), window);
    
    return window;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    if (!initDB(&db)) {
        fprintf(stderr, "Erreur: Impossible d'initialiser la base de données\n");
        return EXIT_FAILURE;
    }
    
    GtkWidget *login_window = create_login_window();
    gtk_widget_show_all(login_window);
    
    gtk_main();
    
    sqlite3_close(db);
    return EXIT_SUCCESS;
}

