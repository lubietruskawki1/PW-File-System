#pragma once

// Implementacja RW locka z laboratoryjnego zadania domowego.
typedef struct RW_lock RW_lock;

RW_lock* rw_lock_init();

void rw_lock_destroy(RW_lock* rw_lock);

// Protokół wstępny czytelnika.
void reader_preprotocol(RW_lock* rw_lock);

// Protokół końcowy czytelnika.
void reader_postprotocol(RW_lock* rw_lock);

// Protokół wstępny pisarza.
void writer_preprotocol(RW_lock* rw_lock);

// Protokół końcowy pisarza.
void writer_postprotocol(RW_lock* rw_lock);