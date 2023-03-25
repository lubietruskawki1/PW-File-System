#pragma once

/* wypisuje informacje o błędnym zakończeniu funkcji systemowej
i kończy działanie */
extern void syserr(const char* fmt, ...);

/* wypisuje informacje o błędzie i kończy działanie */
extern void fatal(const char* fmt, ...);

// Makro sprawdzające, czy wskaźnik nie jest pusty.
#define CHECK_PTR(p)    	\
	do {			    	\
		if ((p) == NULL) {	\
			exit(1);		\
		}					\
	} while (0)

// Makro do sprawdzania kodów błędów.
#define CHECK(x)                                                                  \
    do {                                                                          \
        int err = (x);                                                            \
        if (err != 0) {                                                           \
            fprintf(stderr, "Runtime error: %s returned %d in %s at %s:%d\n%s\n", \
                #x, err, __func__, __FILE__, __LINE__, strerror(err));            \
            exit(1);                                                              \
        }                                                                         \
    } while (0)
