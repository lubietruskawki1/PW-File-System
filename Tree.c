#include "Tree.h"
#include "path_utils.h"
#include "HashMap.h"
#include "RW_lock.h"
#include "err.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_FOLDER_NAME_LENGTH 255
#define CUSTOM_ERROR -1

/* Opis metody synchronizacji:
W każdym wierzchołku (folderze) trzymam RW locka znanego nam z labów. Na haszmapie wyróżniamy dwa rodzaje
operacji:
    1. Czytelnicy - operacje niemodyfikujące mapy, jedynie odczytujące zawartość (np. get, size),
    2. Pisarze - operacja modyfikujące zawartość mapy (np. insert, remove).
Podróżując po ścieżce od korzenia do folderu, w którym chcemy zrobić jakąś operację, do każdego wierzchołka
wkładamy czytelnika lub pisarza - w zależności od tego, jaką operację chcemy w nim zrobić.
Oznacza to, że czytelnicy będą we wszystkich wierzchołkach, poza tym, w którym chcemy dokonać zmian - tam
będzie pisarz. Zauważmy, że wpusczeni czytelnicy zablokują ścieżkę i żaden pisarz, którego będzie chciał
wpuścić inny proces, nie będzie mógł dokonywać na niej zmian.
(W tree_list wystarczy pisarz, jako, że ta funkcja również jedynie odczytuje dane.)
Natomiast w tree_move znajdujemy Last Common Ancestora folderu źródłowego i docelowego i do niego wkładamy
pisarza - ten pisarz blokuje całe poddrzewo, więc mamy pewność, że nikt nam nie przeszkodzi w przeniesieniu
folderu. Oczywiście na ścieżce od LCA do korzenia wkładamy czytelników.
Po zakończeniu operacji lub w przypadku błędu wywołujemy protokoły końcowe wpuszczonych dotychczas czytelników
i pisarzy.
*/

struct Tree {
    HashMap* subfolders;
    Tree* parent; // wskaźnik do rodzica
    RW_lock* rw_lock;
};

Tree* tree_new() {
    Tree* new_tree = malloc(sizeof(Tree));
    CHECK_PTR(new_tree);
    new_tree->subfolders = hmap_new();
    new_tree->parent = NULL;
    new_tree->rw_lock = rw_lock_init();
    return new_tree;
}

void tree_free(Tree* tree) {
    const char* key;
    void* value;
    HashMapIterator it = hmap_iterator(tree->subfolders);
    while (hmap_next(tree->subfolders, &it, &key, &value)) {
        tree_free((Tree*)value);
    }
    hmap_free(tree->subfolders);
    rw_lock_destroy(tree->rw_lock);
    free(tree);
}

// Wypuszcza czytelników ze ścieżki od Tree folder (wyłącznie) do korzenia.
void release_readers(Tree* folder) {
    while (folder->parent != NULL) {
        folder = folder->parent;
        reader_postprotocol(folder->rw_lock);
    }
}

/* Wpuszcza czytelników na ścieżkę path, ustawia wskaźnik folder na ostatni folder na tej ścieżce.
   Jeśli któryś z folderów na ścieżce nie istnieje, wypuszcza wpuszczonych czytelników i zwraca false. */
bool let_readers_in(Tree** folder, const char* path) {
    HashMap* subfolders = (*folder)->subfolders;
    Tree* next_folder;

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    const char* subpath = path;

    while ((subpath = split_path(subpath, component))) {
        reader_preprotocol((*folder)->rw_lock);
        if ((next_folder = (Tree*)hmap_get(subfolders, component)) == NULL) {
            reader_postprotocol((*folder)->rw_lock);
            release_readers(*folder);
            return false;
        }
        *folder = next_folder;
        subfolders = (*folder)->subfolders;
    }
    return true;
}

char* tree_list(Tree* tree, const char* path) {
    if (!is_path_valid(path)) {
        return NULL;
    }

    Tree* folder = tree;
    if (!let_readers_in(&folder, path)) {
        return NULL;
    }

    reader_preprotocol(folder->rw_lock);
    char* list = make_map_contents_string(folder->subfolders);
    reader_postprotocol(folder->rw_lock);

    release_readers(folder);

    return list;
}


/* Wpuszcza czytelników na ścieżkę do rodzica od ścieżki path (bez ostatniego wierzchołka), ustawia
   wskaźnik folder na ostatni folder na ścieżce do rodzica i zapisuje jego nazwę w zmiennej subfolder_name.
   Jeśli któryś z folderów na ścieżce nie istnieje, wypuszcza wpuszczonych dotychczas czytelników i zwraca false. */
bool find_folder_and_let_readers_in(Tree** folder, const char* path, char* subfolder_name) {
    char* path_to_parent = make_path_to_parent(path, subfolder_name);

    if (!let_readers_in(&(*folder), path_to_parent)) {
        free(path_to_parent);
        return false;
    }
    free(path_to_parent);

    return true;
}

int tree_create(Tree* tree, const char* path) {
    if (!is_path_valid(path)) {
        return EINVAL;
    }

    if (is_a_root(path)) {
        return EEXIST;
    }

    Tree* parent = tree;
    char subfolder_name[MAX_FOLDER_NAME_LENGTH + 1];
    if (!find_folder_and_let_readers_in(&parent, path, subfolder_name)) {
        return ENOENT;
    }

    Tree *new_tree = tree_new();
    new_tree->parent = parent;

    writer_preprotocol(parent->rw_lock);
    bool success = hmap_insert(parent->subfolders, subfolder_name, new_tree);
    writer_postprotocol(parent->rw_lock);

    release_readers(parent);

    if (!success) {
        tree_free(new_tree);
        return EEXIST;
    }

    return 0;
}

int tree_remove(Tree* tree, const char* path) {
    if (!is_path_valid(path)) {
        return EINVAL;
    }

    if (is_a_root(path)) {
        return EBUSY;
    }

    Tree* parent = tree;
    char subfolder_name[MAX_FOLDER_NAME_LENGTH + 1];
    if (!find_folder_and_let_readers_in(&parent, path, subfolder_name)) {
        return ENOENT;
    }

    writer_preprotocol(parent->rw_lock);

    Tree* folder_to_be_removed = (Tree*)hmap_get(parent->subfolders, subfolder_name);
    if (folder_to_be_removed == NULL) {
        writer_postprotocol(parent->rw_lock);
        release_readers(parent);
        return ENOENT;
    }

    if (hmap_size(folder_to_be_removed->subfolders) > 0) {
        writer_postprotocol(parent->rw_lock);
        release_readers(parent);
        return ENOTEMPTY;
    }

    tree_free(folder_to_be_removed);

    hmap_remove(parent->subfolders, subfolder_name);

    writer_postprotocol(parent->rw_lock);
    release_readers(parent);

    return 0;
}

/* Ustawia wskaźnik folder na ostatni folder na ścieżce path i zapisuje jego nazwę w zmiennej subfolder_name.
   Jeśli któryś z folderów na ścieżce nie istnieje, wypuszcza pisarza z writer_location i czytelników powyżej,
   zwraca false. */
bool find_folder(Tree** folder, const char* path, char* subfolder_name, Tree* writer_location) {
    Tree* next_folder;
    HashMap* subfolders = (*folder)->subfolders;

    char* path_to_parent = make_path_to_parent(path, subfolder_name);
    const char* subpath = path_to_parent;

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    while ((subpath = split_path(subpath, component))) {
        if ((next_folder = (Tree*)hmap_get(subfolders, component)) == NULL) {
            writer_postprotocol(writer_location->rw_lock);
            release_readers(writer_location);
            free(path_to_parent);
            return false;
        }
        *folder = next_folder;
        subfolders = (*folder)->subfolders;
    }

    free(path_to_parent);

    return true;
}

int tree_move(Tree* tree, const char* source, const char* target) {
    if (!is_path_valid(source) || !is_path_valid(target)) {
        return EINVAL;
    }

    if (is_a_root(source)) {
        return EBUSY;
    }

    if (is_a_root(target)) {
        return EEXIST;
    }

    if (starts_with(target, source) && !are_the_same(target, source)) {
        return CUSTOM_ERROR;
    }

    Tree* last_common_ancestor = tree;
    char* last_common_ancestor_path = common_prefix(source, target);

    if (!is_a_root(last_common_ancestor_path)) {
        char component[MAX_FOLDER_NAME_LENGTH + 1];
        char* path_to_parent = make_path_to_parent(last_common_ancestor_path, component);

        if (!let_readers_in(&last_common_ancestor, path_to_parent)) {
            free(last_common_ancestor_path);
            free(path_to_parent);
            return ENOENT;
        }

        free(path_to_parent);
    }

    free(last_common_ancestor_path);

    writer_preprotocol(last_common_ancestor->rw_lock);

    Tree* source_folder = tree;
    char source_subfolder_name[MAX_FOLDER_NAME_LENGTH + 1];
    if (!find_folder(&source_folder, source,
                     source_subfolder_name, last_common_ancestor)) {
        return ENOENT;
    }

    Tree* folder_to_be_moved = (Tree*)hmap_get(source_folder->subfolders, source_subfolder_name);
    if (folder_to_be_moved == NULL) {
        writer_postprotocol(last_common_ancestor->rw_lock);
        release_readers(last_common_ancestor);
        return ENOENT;
    }

    if (are_the_same(source, target)) {
        writer_postprotocol(last_common_ancestor->rw_lock);
        release_readers(last_common_ancestor);
        return 0;
    }

    Tree* target_folder = tree;
    char target_subfolder_name[MAX_FOLDER_NAME_LENGTH + 1];
    if (!find_folder(&target_folder, target,
                     target_subfolder_name, last_common_ancestor)) {
        return ENOENT;
    }

    if (hmap_get(target_folder->subfolders, target_subfolder_name) != NULL) {
        writer_postprotocol(last_common_ancestor->rw_lock);
        release_readers(last_common_ancestor);
        return EEXIST;
    }

    hmap_remove(source_folder->subfolders, source_subfolder_name);

    folder_to_be_moved->parent = target_folder;
    hmap_insert(target_folder->subfolders, target_subfolder_name, folder_to_be_moved);

    writer_postprotocol(last_common_ancestor->rw_lock);
    release_readers(last_common_ancestor);

    return 0;
}