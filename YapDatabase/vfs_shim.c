// Define this symbol when building to include a shim on SQLite's VFS that allows you to cause writes to the database to fail
// simply by changing the value of a global variable (see vfs_shim.c for info)
#ifdef HPY_SQLITE_VFS_SHIM

#include <stdio.h>
#include <string.h>

#include <sqlite3.h>

// NOTE: Setting this global variable to something other than 0 will cause writes to return the storage full status, which will
//       cause YapDatabase to throw an exception
int gSQLiteWriteFail = 0;


static int VFSShimOpen(sqlite3_vfs *vfs, const char *, sqlite3_file *file, int, int *);
static int VFSShimDelete(sqlite3_vfs *vfs, const char *zName, int syncDir);
static int VFSShimAccess(sqlite3_vfs *vfs, const char *zName, int flags, int *);
static int VFSShimFullPathname(sqlite3_vfs *vfs, const char *zName, int, char *zOut);
static void *VFSShimDlOpen(sqlite3_vfs *vfs, const char *zFilename);
static void VFSShimDlError(sqlite3_vfs *vfs, int nByte, char *zErrMsg);
static void (*VFSShimDlSym(sqlite3_vfs *vfs, void *p, const char *zSym))(void);
static void VFSShimDlClose(sqlite3_vfs *vfs, void *);
static int VFSShimRandomness(sqlite3_vfs *vfs, int nByte, char *zOut);
static int VFSShimSleep(sqlite3_vfs *vfs, int microseconds);
static int VFSShimCurrentTime(sqlite3_vfs *vfs, double *time);
static int VFSShimGetLastError(sqlite3_vfs *vfs, int code, char *message);
static int VFSShimCurrentTimeInt64(sqlite3_vfs *vfs, sqlite3_int64 *time);
static int VFSShimSetSystemCall(sqlite3_vfs *vfs, const char *name, sqlite3_syscall_ptr ptr);
static sqlite3_syscall_ptr VFSShimGetSystemCall(sqlite3_vfs *vfs, const char *name);
static const char *VFSShimNextSystemCall(sqlite3_vfs *vfs, const char *name);

static int VFSShimClose(sqlite3_file *file);
static int VFSShimRead(sqlite3_file *file, void *, int iAmt, sqlite3_int64 iOfst);
static int VFSShimWrite(sqlite3_file *file, const void *, int iAmt, sqlite3_int64 iOfst);
static int VFSShimTruncate(sqlite3_file *file, sqlite3_int64 size);
static int VFSShimSync(sqlite3_file *file, int flags);
static int VFSShimFileSize(sqlite3_file *file, sqlite3_int64 *pSize);
static int VFSShimLock(sqlite3_file *file, int);
static int VFSShimUnlock(sqlite3_file *file, int);
static int VFSShimCheckReservedLock(sqlite3_file *file, int *pResOut);
static int VFSShimFileControl(sqlite3_file *file, int op, void *pArg);
static int VFSShimSectorSize(sqlite3_file *file);
static int VFSShimDeviceCharacteristics(sqlite3_file *file);
static int VFSShimShmMap(sqlite3_file *file, int iPg, int pgsz, int, void volatile **);
static int VFSShimShmLock(sqlite3_file *file, int offset, int n, int flags);
static void VFSShimShmBarrier(sqlite3_file *file);
static int VFSShimShmUnmap(sqlite3_file *file, int deleteFlag);
static int VFSShimFetch(sqlite3_file *file, sqlite3_int64 iOfst, int iAmt, void **pp);
static int VFSShimUnfetch(sqlite3_file *file, sqlite3_int64 iOfst, void *p);


typedef struct {
    sqlite3_vfs shim;
    sqlite3_vfs *original;
} VFSShim;


typedef struct {
    sqlite3_file shim;
    sqlite3_file *original;
    char *name;
} VFSShimFile;


static VFSShim vfsShim = {
    {
        3,                      // iVersion
        0,                      // szOsFile
        1024,                   // mxPathname
        NULL,                   // pNext
        "vfsshim",              // zName
        NULL,                   // pAppData
        VFSShimOpen,            // xOpen
        VFSShimDelete,          // xDelete
        VFSShimAccess,          // xAccess
        VFSShimFullPathname,    // xFullPathname
        VFSShimDlOpen,          // xDlOpen
        VFSShimDlError,         // xDlError
        VFSShimDlSym,           // xDlSym
        VFSShimDlClose,         // xDlClose
        VFSShimRandomness,      // xRandomness
        VFSShimSleep,           // xSleep
        VFSShimCurrentTime,     // xCurrentTime
        VFSShimGetLastError,    // xGetLastError
        VFSShimCurrentTimeInt64,// xCurrentTimeInt64
        VFSShimSetSystemCall,   // xSetSystemCall
        VFSShimGetSystemCall,   // xGetSystemCall
        VFSShimNextSystemCall   // xNextSystemCall
    },
    0
};


static const sqlite3_io_methods vfsShimIOMethods = {
    3,                            // iVersion
    VFSShimClose,                 // xClose
    VFSShimRead,                  // xRead
    VFSShimWrite,                 // xWrite
    VFSShimTruncate,              // xTruncate
    VFSShimSync,                  // xSync
    VFSShimFileSize,              // xFileSize
    VFSShimLock,                  // xLock
    VFSShimUnlock,                // xUnlock
    VFSShimCheckReservedLock,     // xCheckReservedLock
    VFSShimFileControl,           // xFileControl
    VFSShimSectorSize,            // xSectorSize
    VFSShimDeviceCharacteristics, // xDeviceCharacteristics
    VFSShimShmMap,                // xShmMap
    VFSShimShmLock,               // xShmLock
    VFSShimShmBarrier,            // xShmBarrier
    VFSShimShmUnmap,              // xShmUnmap
    VFSShimFetch,                 // xFetch
    VFSShimUnfetch                // xUnfetch
};


static int VFSShimClose(sqlite3_file *file)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = SQLITE_OK;

    if (f->original->pMethods) {
        retVal = f->original->pMethods->xClose(f->original);
    }

    printf("SQLite: Closing %s\n", f->name);

    sqlite3_free(f->name);

    return retVal;
}


static int VFSShimRead(sqlite3_file *file, void *zBuf, int iAmt, sqlite_int64 iOfst)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xRead(f->original, zBuf, iAmt, iOfst);

    if (retVal == SQLITE_OK) {
        // Do stuff
    }

    return retVal;
}


static int VFSShimWrite(sqlite3_file *file, const void *data, int numBytes, sqlite_int64 offset)
{
    VFSShimFile *f = (VFSShimFile *)file;

    if (gSQLiteWriteFail) {
        printf("SQLite: FAILED write of %db to %s\n", numBytes, f->name);

        return SQLITE_FULL;
    }

    int retVal = f->original->pMethods->xWrite(f->original, data, numBytes, offset);

    if (retVal == SQLITE_OK) {
        printf("SQLite: Write %db to %s\n", numBytes, f->name);
    }

    return retVal;
}


static int VFSShimTruncate(sqlite3_file *file, sqlite_int64 size)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xTruncate(f->original, size);

    return retVal;
}


static int VFSShimSync(sqlite3_file *file, int flags)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xSync(f->original, flags);

    return retVal;
}


static int VFSShimFileSize(sqlite3_file *file, sqlite_int64 *pSize)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xFileSize(f->original, pSize);

    return retVal;
}


static int VFSShimLock(sqlite3_file *file, int eLock)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xLock(f->original, eLock);

    return retVal;
}


static int VFSShimUnlock(sqlite3_file *file, int eLock)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xUnlock(f->original, eLock);

    return retVal;
}


static int VFSShimCheckReservedLock(sqlite3_file *file, int *pResOut)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xCheckReservedLock(f->original, pResOut);

    return retVal;
}


static int VFSShimFileControl(sqlite3_file *file, int op, void *pArg)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xFileControl(f->original, op, pArg);

    if (op == SQLITE_FCNTL_VFSNAME && retVal == SQLITE_OK) {
        *(char**)pArg = sqlite3_mprintf("vfsshim/%z", *(char**)pArg);
    }

    return retVal;
}


static int VFSShimSectorSize(sqlite3_file *file)
{
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xSectorSize(f->original);

    return retVal;
}


static int VFSShimDeviceCharacteristics(sqlite3_file *file) {
    VFSShimFile *f = (VFSShimFile *)file;
    int retVal = f->original->pMethods->xDeviceCharacteristics(f->original);

    return retVal;
}


static int VFSShimShmMap(sqlite3_file *file, int iPg, int pgsz, int bExtend, void volatile **pp)
{
    VFSShimFile *f = (VFSShimFile *)file;

    return f->original->pMethods->xShmMap(f->original, iPg, pgsz, bExtend, pp);
}


static int VFSShimShmLock(sqlite3_file *file, int offset, int n, int flags)
{
    VFSShimFile *f = (VFSShimFile *)file;

    return f->original->pMethods->xShmLock(f->original, offset, n, flags);
}


static void VFSShimShmBarrier(sqlite3_file *file)
{
    VFSShimFile *f = (VFSShimFile *)file;

    f->original->pMethods->xShmBarrier(f->original);
}


static int VFSShimShmUnmap(sqlite3_file *file, int deleteFlag)
{
    VFSShimFile *f = (VFSShimFile *)file;

    return f->original->pMethods->xShmUnmap(f->original, deleteFlag);
}


static int VFSShimFetch(sqlite3_file *file, sqlite3_int64 iOfst, int iAmt, void **pp)
{
    VFSShimFile *f = (VFSShimFile *)file;

    return f->original->pMethods->xFetch(f->original, iOfst, iAmt, pp);
}


static int VFSShimUnfetch(sqlite3_file *file, sqlite3_int64 iOfst, void *pPage)
{
    VFSShimFile *f = (VFSShimFile *)file;

    return f->original->pMethods->xUnfetch(f->original, iOfst, pPage);
}


static int VFSShimOpen(sqlite3_vfs *vfs, const char *name, sqlite3_file *file, int flags, int *pOutFlags)
{
    VFSShim *v = (VFSShim *)vfs;
    VFSShimFile *f = (VFSShimFile*)file;

    const char *nameEnd = name + strlen(name);

    while (*nameEnd != '/' && nameEnd > name) {
        --nameEnd;
    }

    f->original = (sqlite3_file *)&f[1];
    f->name = sqlite3_malloc(strlen(nameEnd));
    strcpy(f->name, nameEnd);

    printf("SQLite: Opening %s\n", nameEnd);
    
    int retVal = v->original->xOpen(v->original, name, f->original, flags, pOutFlags);

    file->pMethods = retVal == SQLITE_OK ? &vfsShimIOMethods : NULL;

    return retVal;
}


static int VFSShimDelete(sqlite3_vfs *vfs, const char *path, int dirSync)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xDelete(v->original, path, dirSync);

    return retVal;
}


static int VFSShimAccess(sqlite3_vfs *vfs, const char *path, int flags, int *pResOut)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xAccess(v->original, path, flags, pResOut);

    return retVal;
}


static int VFSShimFullPathname(sqlite3_vfs *vfs, const char *path, int nOut, char *zOut)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xFullPathname(v->original, path, nOut, zOut);

    return retVal;
}


static void *VFSShimDlOpen(sqlite3_vfs *vfs, const char *path)
{
    VFSShim *v = (VFSShim *)vfs;
    void *retVal = v->original->xDlOpen(v->original, path);

    return retVal;
}


static void VFSShimDlError(sqlite3_vfs *vfs, int nByte, char *message)
{
    VFSShim *v = (VFSShim *)vfs;

    v->original->xDlError(v->original, nByte, message);
}


static void (*VFSShimDlSym(sqlite3_vfs *vfs, void *p, const char *zSym))(void)
{
    VFSShim *v = (VFSShim *)vfs;

    return v->original->xDlSym(v->original, p, zSym);
}


static void VFSShimDlClose(sqlite3_vfs *vfs, void *handle)
{
    VFSShim *v = (VFSShim *)vfs;

    v->original->xDlClose(v->original, handle);
}


static int VFSShimRandomness(sqlite3_vfs *vfs, int bytes, char *output)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xRandomness(v->original, bytes, output);

    return retVal;
}


static int VFSShimSleep(sqlite3_vfs *vfs, int microseconds)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xSleep(v->original, microseconds);

    return retVal;
}


static int VFSShimCurrentTime(sqlite3_vfs *vfs, double *time)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xCurrentTime(v->original, time);

    return retVal;
}


static int VFSShimGetLastError(sqlite3_vfs *vfs, int code, char *message)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xGetLastError(v->original, code, message);

    return retVal;
}


static int VFSShimCurrentTimeInt64(sqlite3_vfs *vfs, sqlite3_int64 *time)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xCurrentTimeInt64(v->original, time);

    return retVal;
}


static int VFSShimSetSystemCall(sqlite3_vfs *vfs, const char *name, sqlite3_syscall_ptr sysCallPtr)
{
    VFSShim *v = (VFSShim *)vfs;
    int retVal = v->original->xSetSystemCall(v->original, name, sysCallPtr);

    return retVal;
}


static sqlite3_syscall_ptr VFSShimGetSystemCall(sqlite3_vfs *vfs, const char *name)
{
    VFSShim *v = (VFSShim *)vfs;
    sqlite3_syscall_ptr retVal = v->original->xGetSystemCall(v->original, name);

    return retVal;
}


static const char *VFSShimNextSystemCall(sqlite3_vfs *vfs, const char *name)
{
    VFSShim *v = (VFSShim *)vfs;
    const char *retVal = v->original->xNextSystemCall(v->original, name);

    return retVal;
}


// Register the new VFS. Make arrangement to register the virtual table for each new database connection.
int RegisterVFSShim(void)
{
    vfsShim.original = sqlite3_vfs_find(NULL);
    vfsShim.shim.szOsFile = sizeof(VFSShimFile) + vfsShim.original->szOsFile;

    int retVal = sqlite3_vfs_register(&vfsShim.shim, 1);

    return retVal;
}

#endif //HPY_SQLITE_VFS_SHIM
