compile :

gcc C-Pronote.c -o C-Pronote $(pkg-config --cflags --libs gtk+-3.0) -lsqlite3

execute :

./C-Pronote
