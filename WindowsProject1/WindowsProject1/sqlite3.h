/*
** SQLite3 Minimal Header
**
** Declares only the SQLite API functions used by this project.
** Compatible with the official SQLite amalgamation.
**
** REQUIRED BEFORE BUILDING:
**   1. Go to https://www.sqlite.org/download.html
**   2. Download "sqlite-amalgamation-XXXXXXXX.zip"
**   3. Extract sqlite3.c from the zip and place it next to this file
**      (replacing the placeholder sqlite3.c already in the project)
**   4. Rebuild
*/
#ifndef SQLITE3_H
#define SQLITE3_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite3        sqlite3;
typedef struct sqlite3_stmt   sqlite3_stmt;
typedef long long             sqlite3_int64;

#define SQLITE_OK     0
#define SQLITE_ERROR  1
#define SQLITE_BUSY   5
#define SQLITE_ROW  100
#define SQLITE_DONE 101

#define SQLITE_OPEN_READONLY   0x00000001
#define SQLITE_OPEN_READWRITE  0x00000002
#define SQLITE_OPEN_CREATE     0x00000004

typedef void (*sqlite3_destructor_type)(void*);
#define SQLITE_STATIC    ((sqlite3_destructor_type)0)
#define SQLITE_TRANSIENT ((sqlite3_destructor_type)(-1))

int            sqlite3_open_v2(const char* filename, sqlite3** ppDb, int flags, const char* zVfs);
int            sqlite3_close(sqlite3*);
int            sqlite3_exec(sqlite3*, const char* sql,
                            int(*callback)(void*, int, char**, char**),
                            void* arg, char** errmsg);
void           sqlite3_free(void*);
int            sqlite3_prepare_v2(sqlite3* db, const char* zSql, int nByte,
                                  sqlite3_stmt** ppStmt, const char** pzTail);
int            sqlite3_step(sqlite3_stmt*);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int iCol);
int            sqlite3_column_int(sqlite3_stmt*, int iCol);
int            sqlite3_finalize(sqlite3_stmt*);
int            sqlite3_bind_text(sqlite3_stmt*, int idx, const char* text, int n,
                                 void(*destructor)(void*));
int            sqlite3_bind_int(sqlite3_stmt*, int idx, int value);
sqlite3_int64  sqlite3_last_insert_rowid(sqlite3*);
int            sqlite3_changes(sqlite3*);
const char*    sqlite3_errmsg(sqlite3*);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE3_H */
